# Optional path tracer (RTGL1)

ThiefRunner can render with a real-time path tracer instead of the Vulkan
renderer: traced lights and shadows, bounce light, reflections on polished and
wet surfaces, god rays through the mission fog, a traced light gem, Garrett's
own shadow (which guards can notice), and material maps made from the game's
textures. It is carried over from the Thief RT macOS port
(https://github.com/loui1989/thief-rt-mac) and uses RTGL1
(https://github.com/sultim-t/RayTracedGL1), in the version that port ships:
https://github.com/loui1989/RayTracedGL1, branch `thief`.

It is off unless asked for twice: the build has to include it
(`DARK_RTGL1_INCLUDE`), and the game has to be started with `THIEF_RT=1`.
Without either, nothing changes: the Vulkan renderer runs as before, and a
build without it has none of its code.

The port was written with Claude (Anthropic's AI model), working with the
port's author.

## Requirements

- **Windows x64**: a GPU with Vulkan ray queries
  (`VK_KHR_ray_query`, `VK_KHR_ray_tracing_position_fetch`): NVIDIA RTX 20
  series or newer, AMD RX 6000 or newer, with a current driver. Upscaling is
  AMD FSR2, or NVIDIA DLSS (and DLSS Ray Reconstruction) when RTGL1 is built
  with the DLSS SDK.
- **macOS**: Apple silicon, with RTGL1's native Metal backend; upscaling with
  MetalFX.
- Linux builds of RTGL1's Vulkan backend are not ready yet. 32-bit Windows and
  Android are not supported.

## Building on Windows

With the [Windows prerequisites](../README.md#windows) installed, and Python 3
for RTGL1's shader script, from a Developer PowerShell for VS with
`C:\msys64\usr\bin` on `PATH`:

```powershell
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File tools\rt\build-windows.ps1
```

The script clones RTGL1 beside this repository (`..\RayTracedGL1`), builds
`RTGL1.dll` and its shaders, builds the game in `build-win64-rt`, and puts the
`rt` folder next to `thief-runner.exe` with `thief-rt.cmd`, which starts the
game with the path tracer and writes its log to `thief-rt.log`. With `-Dlss`
it also clones the NVIDIA DLSS SDK (https://github.com/NVIDIA/DLSS) and builds
DLSS into RTGL1. `Get-Help tools\rt\build-windows.ps1 -Detailed` lists the
other options.

By hand, the steps are:

```powershell
git clone --recursive -b thief https://github.com/loui1989/RayTracedGL1 ..\RayTracedGL1
cmake -S ..\RayTracedGL1 -B ..\RayTracedGL1\build-win64 -A x64 -DRG_WITH_SURFACE_WIN32=ON `
  -DRG_WITH_EXAMPLES=OFF -DRG_WITH_IMGUI=OFF -DRG_WITH_DX12=OFF -DRG_WITH_NATIVE_DLSS=OFF
cmake --build ..\RayTracedGL1\build-win64 --config Release --target RayTracedGL1
cd ..\RayTracedGL1\Source\Shaders; python GenerateShaders.py -r; cd -
cmake -S . -B build-win64-rt -A x64 -DDARK_RTGL1_INCLUDE=..\RayTracedGL1\Include
cmake --build build-win64-rt --config Release --target thief2
```

and then the `rt` folder beside `thief-runner.exe`:

| In `rt` | From RTGL1 (or this repository) |
| --- | --- |
| `bin\RTGL1.dll`, `bin\ffx_*.dll` | `Build\bin\RTGL1.dll`, `Source\bin\ffx_*.dll` |
| `shaders\*.spv` | `Build\shaders\*.spv` |
| `*.ktx2` | `Tools\*.ktx2` |
| `glass\*.png` | this repository's `assets\rt\glass` |

## Building on macOS

Build RTGL1's `thief` branch with its Metal backend (its shaders are compiled
with glslang and translated with SPIRV-Cross):

```sh
brew install spirv-cross glslang
git clone --recursive -b thief https://github.com/loui1989/RayTracedGL1
cmake -S RayTracedGL1 -B RayTracedGL1/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DRG_WITH_METAL=ON -DRG_WITH_EXAMPLES=OFF
ninja -C RayTracedGL1/build RayTracedGL1
```

then configure the game with `-DDARK_RTGL1_INCLUDE=<RayTracedGL1>/Include`, and
copy RTGL1's `Build/bin` (`libRTGL1.dylib` and its `shaders`) to `rt/bin`
beside `thief-runner` (or in the app bundle's `Contents/Resources/rt`), with
the data files as above.

## Running

- `THIEF_RT=1` turns the path tracer on; `THIEF_RT_DIR` names the `rt` folder
  when it is not beside the executable. When the library or the GPU support is
  missing, the game says so in its log and uses the Vulkan renderer.
- **F10** opens the path tracer's settings: a quality preset, the upscaler and
  render resolution, and the lighting, fog, material and light-gem options.
  They are saved to `rt_settings.txt` in the game's settings folder
  (`%APPDATA%\Dark Engine Restored` on Windows).
  A few are only read from that file, not shown in the panel:
  `highlight` (how brightly the object you can use glows, 1.1 by default),
  `heat_haze` (the shimmer over fires, 0 off).
- A crash writes its call stack, with function names and source lines, to the
  log and to `darkengine-diagnostics.log` in the game folder (the Windows build
  script keeps the symbols, `thief-runner.pdb`, beside the executable).
- `THIEF_RT_UPSCALER=fsr2` keeps AMD FSR2 when DLSS is available;
  `THIEF_RT_RR=1` turns on DLSS Ray Reconstruction.
- The material maps are made from the game's textures the first time each one
  is seen and kept in `rt/mat_dev`; `rt/materials.txt` says what each texture
  family is made of (it is written with the defaults when missing).

## Where the code is

- `cmake/RayTracing.cmake`: the `DARK_RTGL1_INCLUDE` option, defining `DARK_RT`.
- `libsrc/lgd3d/rt/`: the renderer (`rtgl1.cpp`): the scene sent to RTGL1,
  lights, materials, the settings panel. `libsrc/lgd3d/render_rt.h` is its
  interface to the engine.
- `libsrc/lgd3d/vulkan.cpp`, `vulkan_render_backend.cpp`: the Vulkan runtime
  hands the frame to the path tracer when it is on.
- `src/portal`, `src/render`, `libsrc/r3d`, `libsrc/md`, `libsrc/mm`: the
  world, objects, sky, particles and lights captured for it, all in
  `#ifdef DARK_RT` blocks.
