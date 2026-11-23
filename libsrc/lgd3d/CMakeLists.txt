add_library(dark_lgd3d STATIC
  legacy_driver.cpp
  legacy_textures.cpp
  legacy_primitives.c
  legacy_texture_manager.c
  vulkan.cpp
  vulkan_context.cpp
  vulkan_render_backend.cpp
  vulkan_scene_renderer.cpp
)
dark_configure_library(dark_lgd3d)

target_link_libraries(dark_lgd3d PRIVATE Vulkan::Vulkan)
if(ANDROID)
  target_include_directories(dark_lgd3d SYSTEM PRIVATE
    "${DARK_VULKAN_HEADERS_SOURCE}/include")
endif()
if(DEFINED DARK_VULKAN_SHADER_DIR)
  target_compile_definitions(dark_lgd3d PRIVATE
    DARK_VULKAN_SHADER_DIR="${DARK_VULKAN_SHADER_DIR}")
endif()
if(TARGET dark_vulkan_shaders)
  add_dependencies(dark_lgd3d dark_vulkan_shaders)
endif()

if(DARK_RT)
  # The path tracer (cmake/RayTracing.cmake): it takes over from the Vulkan
  # renderer when THIEF_RT=1 and its library is found at run time.
  target_sources(dark_lgd3d PRIVATE rt/rtgl1.cpp)
  target_include_directories(dark_lgd3d PRIVATE "${DARK_RTGL1_INCLUDE}")
  # (the material maps' PNGs: rt/rt_materials.h)
  target_link_libraries(dark_lgd3d PRIVATE ZLIB::ZLIBSTATIC SDL3::SDL3)
  set_source_files_properties(rt/rtgl1.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:MSVC>:/std:c++20>$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-std=c++20>")
  if(NOT WIN32)
    target_link_libraries(dark_lgd3d PRIVATE ${CMAKE_DL_LIBS})
  endif()
endif()
