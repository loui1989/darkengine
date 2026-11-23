#include "material.h"
#include "material_parser.hpp"
#include <resman.h>
#include <resapi.h>
#include <reshelp.h>
#include <storeapi.h>
#include <imgrstyp.h>
#include <resthred.h>
#include <bitmap.h>
#include <config.h>
#include <mprintf.h>
#include <render_backend.h>

#include <cstdio>
#include <memory>
#include <unordered_map>
#include <cmath>

namespace
{
struct Entry
{
   DarkMaterial::Material material;
   IResManHelper *manager = nullptr; // resource manager outlives loaded image data
   std::string path;
   void *renderData = nullptr;
   void (*releaseRenderData)(void *) = nullptr;
   void ReleaseRenderData()
   {
      if (renderData && releaseRenderData)
         releaseRenderData(renderData);
      renderData = nullptr;
   }
   IRes *illumination = nullptr;
   grs_bitmap *illuminationBitmap = nullptr;
   bool mesh = false;
   bool illuminationRead = false;
   bool uvTraced = false;
   ~Entry()
   {
      ReleaseRenderData();
      if (illumination)
      {
         if (illuminationBitmap)
            illumination->Unlock();
         illumination->Release();
      }
   }
};
auto &entries()
{
   static auto *map = new std::unordered_map<const grs_bitmap *, std::shared_ptr<Entry>>;
   return *map;
}
auto &terrain()
{
   // Each frame retains its physical base projection dimensions even when
   // it shares root-authored settings or has several CPU mip levels.
   struct Projection
   {
      float width;
      float height;
   };
   static auto *map = new std::unordered_map<const grs_bitmap *, Projection>;
   return *map;
}
void ApplyFlags(const sTextureMaterial &s, grs_bitmap *bitmap)
{
   if (s.forceOpaque == 1)
      bitmap->flags &= ~(BMF_TRANS | BMF_TLUC);
   else if (s.fullAlpha == 1)
   {
      bitmap->flags |= BMF_TLUC;
      bitmap->flags &= ~BMF_TRANS;
   }
   else if (s.alphaKey == 1)
   {
      bitmap->flags |= BMF_TRANS;
      bitmap->flags &= ~BMF_TLUC;
   }
}
#ifdef DARK_RT
// Every loaded image's name from its resource root ("fam/mech/dal01"), for the
// path tracer: its material maps are found and made by it (rt/rt_materials.h).
auto &sources()
{
   static auto *map = new std::unordered_map<const grs_bitmap *, std::string>;
   return *map;
}
std::string SourceName(const char *canonPath, const char *name)
{
   std::string full = canonPath ? canonPath : "";
   if (!full.empty() && full.back() != '/' && full.back() != '\\')
      full += '/';
   full += name ? name : "";
   for (char &c : full)
      c = c == '\\' ? '/' : (char)tolower((unsigned char)c);
   // fam.crf/mech/x -> fam/mech/x
   for (size_t at; (at = full.find(".crf/")) != std::string::npos; )
      full.erase(at, 4);
   static const char *const roots[] = {"/fam/", "/obj/", "/mesh/", "/bitmap/", "/intrface/",
                                       "/book/", "/books/", "/pal/"};
   size_t best = std::string::npos;
   for (const char *r : roots)
      for (size_t at = full.find(r); at != std::string::npos; at = full.find(r, at + 1))
         if (best == std::string::npos || at > best)
            best = at;
   std::string out;
   if (best != std::string::npos)
      out = full.substr(best + 1);
   else if (full.compare(0, 4, "fam/") == 0 || full.compare(0, 4, "obj/") == 0 ||
            full.compare(0, 5, "mesh/") == 0)
      out = full;
   else
   {
      // the last two parts
      const size_t last = full.rfind('/');
      const size_t prev = last == std::string::npos || last == 0 ? std::string::npos
                                                                 : full.rfind('/', last - 1);
      out = prev == std::string::npos ? full : full.substr(prev + 1);
   }
   const size_t dot = out.rfind('.');
   if (dot != std::string::npos && out.find('/', dot) == std::string::npos)
      out.erase(dot);
   return out;
}
#endif

bool Read(IRes *resource, std::string &text)
{
   if (!resource)
      return false;
   const auto size = resource->GetSize();
   bool success = false;
   if (size > 0 && size <= 1024 * 1024)
   {
      const auto *data = static_cast<const char *>(resource->Lock());
      if (data)
      {
         text.assign(data, size);
         resource->Unlock();
         success = true;
      }
   }
   resource->Release();
   return success;
}
} // namespace
void ResMaterialLoad(IResManHelper *helper, IRes *image, grs_bitmap *bitmap)
{
   cAutoResThreadLock lock;
#ifdef DARK_RT
   {
      char *canon = nullptr;
      image->GetCanonPath(&canon);
      sources()[bitmap] = SourceName(canon, image->GetName());
      if (canon)
         Free(canon);
   }
#endif
   auto *manager = static_cast<cResMan *>(helper);
   const std::string name = std::string(image->GetName()) + ".mtl";
   std::string initial;
   if (!Read(manager->BindMaterial(image, name.c_str()), initial))
      return;
   auto entry = std::make_shared<Entry>();
   entry->manager = helper;
   char *path = nullptr;
   image->GetCanonPath(&path);
   if (path)
   {
      entry->path = path;
      Free(path);
   }
   entry->mesh = DarkMaterial::Lower(entry->path).compare(0, 4, "mesh") == 0;
   const auto reader = [&](const std::string &file, std::string &text)
   {
      if (!stricmp(file.c_str(), name.c_str()))
      {
         text = initial;
         return true;
      }
      return Read(manager->BindMaterial(image, file.c_str()), text);
   };
   const auto condition = [](const std::string &key)
   {
      if (!config_is_defined(key.c_str()))
         return false;
      int value = 1;
      config_get_int(key.c_str(), &value);
      return value != 0;
   };
   entry->material = DarkMaterial::Parser(reader, condition).Parse(name);
   auto &s = entry->material.settings;
   if (s.terrainWidth > bitmap->w || s.terrainHeight > bitmap->h)
      s.terrainWidth = s.terrainHeight = 0;
   ApplyFlags(s, bitmap);
   for (const auto &directive : entry->material.unsupported)
      mprintf("Material %s%s: unsupported %s\n", entry->path.c_str(), name.c_str(),
              directive.c_str());
   RenderBackendTrace("material source=%s%s terrain=%dx%d tile=%.4fx%.4f illumination=%s",
                      entry->path.c_str(), name.c_str(), s.terrainWidth, s.terrainHeight, s.tileU,
                      s.tileV, entry->material.illumination.c_str());
   entries()[bitmap] = std::move(entry);
}
extern "C" const sTextureMaterial *ResMaterialGet(const grs_bitmap *bitmap)
{
   cAutoResThreadLock lock;
   auto found = entries().find(bitmap);
   return found == entries().end() ? nullptr : &found->second->material.settings;
}
extern "C" void ResMaterialClone(const grs_bitmap *source, grs_bitmap *destination)
{
   cAutoResThreadLock lock;
#ifdef DARK_RT
   {
      const auto name = sources().find(source);
      if (name != sources().end())
         sources()[destination] = name->second;
   }
#endif
   auto found = entries().find(source);
   if (found != entries().end())
   {
      const auto entry = found->second;
      entries()[destination] = entry;
      ApplyFlags(entry->material.settings, destination);
   }
   else
      entries().erase(destination);
   const auto projection = terrain().find(source);
   if (projection != terrain().end() && !terrain().count(destination))
      terrain()[destination] = projection->second;
}
extern "C" void ResMaterialForget(const grs_bitmap *bitmap)
{
   cAutoResThreadLock lock;
#ifdef DARK_RT
   sources().erase(bitmap);
#endif
   terrain().erase(bitmap);
   entries().erase(bitmap);
}
extern "C" grs_bitmap *ResMaterialIllumination(const grs_bitmap *bitmap)
{
   cAutoResThreadLock lock;
   auto found = entries().find(bitmap);
   if (found == entries().end())
      return nullptr;
   // Loading another resource can rehash this registry.
   auto entry = found->second;
   if (!entry->illuminationRead)
   {
      entry->illuminationRead = true;
      if (entry->material.illumination.empty() || entry->material.settings.illumination <= 0 ||
          entry->mesh)
         return nullptr;
      IResMan *manager = nullptr;
      entry->manager->QueryInterface(IID_IResMan, reinterpret_cast<void **>(&manager));
      if (!manager)
         return nullptr;
      const auto &name = entry->material.illumination;
      // Texture references in material files are resource-root relative.
      entry->illumination = manager->Bind(name.c_str(), RESTYPE_IMAGE, nullptr, nullptr, 0);
      manager->Release();
      if (entry->illumination)
         entry->illuminationBitmap = static_cast<grs_bitmap *>(entry->illumination->Lock());
   }
   return entry->illuminationBitmap;
}

extern "C" void ResMaterialSetTerrain(const grs_bitmap *bitmap, const grs_bitmap *base)
{
   cAutoResThreadLock lock;
   // Portal projection uses wlog, which describes row bytes, not texel width.
   terrain()[bitmap] = {std::ldexp(1.0f, base->wlog), float(base->h)};
}
extern "C" int ResMaterialIsTerrain(const grs_bitmap *bitmap)
{
   cAutoResThreadLock lock;
   return terrain().count(bitmap) != 0;
}
extern "C" void ResMaterialUvScale(const grs_bitmap *bitmap, float *u, float *v)
{
   cAutoResThreadLock lock;
   *u = *v = 1;
   auto found = entries().find(bitmap);
   if (found == entries().end())
      return;
   auto &entry = *found->second;
   const auto &s = entry.material.settings;
   // tile_factor is not defined for deformable MESH textures.
   if (!entry.mesh)
   {
      *u = s.tileU;
      *v = s.tileV;
   }
   const auto projection = terrain().find(bitmap);
   if (projection != terrain().end() && s.terrainWidth)
   {
      *u *= projection->second.width / s.terrainWidth;
      *v *= projection->second.height / s.terrainHeight;
   }
   if (!entry.uvTraced)
   {
      entry.uvTraced = true;
      RenderBackendTrace("material-uv path=%s terrain=%d scale=%.4fx%.4f", entry.path.c_str(),
                         int(terrain().count(bitmap) != 0), *u, *v);
   }
}
extern "C" void *ResMaterialRenderData(const grs_bitmap *bitmap)
{
   cAutoResThreadLock lock;
   auto found = entries().find(bitmap);
   return found == entries().end() ? nullptr : found->second->renderData;
}
extern "C" void ResMaterialSetRenderData(const grs_bitmap *bitmap, void *data,
                                         void (*release)(void *))
{
   cAutoResThreadLock lock;
   auto found = entries().find(bitmap);
   if (found == entries().end())
   {
      if (data)
         release(data);
      return;
   }
   auto &entry = *found->second;
   entry.ReleaseRenderData();
   entry.renderData = data;
   entry.releaseRenderData = release;
}
extern "C" void ResMaterialReleaseRenderData()
{
   cAutoResThreadLock lock;
   for (auto &entry : entries())
      entry.second->ReleaseRenderData();
}

extern "C" void ResMaterialReset()
{
   cAutoResThreadLock lock;
   // Unlocking dependent images can recursively call ResMaterialForget.
   // Detach the registry first so those callbacks see an empty registry.
   std::unordered_map<const grs_bitmap *, std::shared_ptr<Entry>> detached;
   detached.swap(entries());
   terrain().clear();
   detached.clear();
}

#ifdef DARK_RT
extern "C" int ResImageSourceInfo(const grs_bitmap *bitmap, char *name, int nameSize,
                                  float *illumination, char *illuminationMap, int mapSize)
{
   cAutoResThreadLock lock;
   if (name && nameSize > 0)
      name[0] = 0;
   if (illumination)
      *illumination = 0;
   if (illuminationMap && mapSize > 0)
      illuminationMap[0] = 0;
   const auto found = sources().find(bitmap);
   if (found == sources().end())
      return 0;
   if (name && nameSize > 0)
      snprintf(name, nameSize, "%s", found->second.c_str());
   const auto entry = entries().find(bitmap);
   if (entry != entries().end() && entry->second->material.settings.illumination > 0 &&
       !entry->second->material.illumination.empty())
   {
      if (illumination)
         *illumination = entry->second->material.settings.illumination;
      if (illuminationMap && mapSize > 0)
         snprintf(illuminationMap, mapSize, "%s", entry->second->material.illumination.c_str());
   }
   return 1;
}
#endif
