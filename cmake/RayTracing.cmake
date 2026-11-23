# Optional path-traced renderer: RTGL1 (https://github.com/sultim-t/RayTracedGL1).
#
# Off by default. Set DARK_RTGL1_INCLUDE to the folder that holds RTGL1/RTGL1.h to
# compile the path tracer in (DARK_RT). The RTGL1 library itself is loaded at run
# time from the "rt" folder (see libsrc/lgd3d/rt/rtgl1.cpp), and the game uses it
# when THIEF_RT=1 is set; otherwise the Vulkan renderer runs as usual.
#
#   macOS: RTGL1 built with its native Metal backend (RG_WITH_METAL), as in
#          https://github.com/loui1989/RayTracedGL1 (branch thief-mac).
#   Windows / Linux: RTGL1's Vulkan backend.
set(DARK_RTGL1_INCLUDE "" CACHE PATH
  "RTGL1 include folder (the one holding RTGL1/RTGL1.h): compiles in the path tracer")

set(DARK_RT OFF)
if(DARK_RTGL1_INCLUDE)
  if(NOT EXISTS "${DARK_RTGL1_INCLUDE}/RTGL1/RTGL1.h")
    message(FATAL_ERROR
      "DARK_RTGL1_INCLUDE (${DARK_RTGL1_INCLUDE}) has no RTGL1/RTGL1.h")
  endif()
  if(ANDROID)
    message(FATAL_ERROR "The path tracer is not supported on Android")
  endif()
  set(DARK_RT ON)
  target_compile_definitions(dark_engine_config INTERFACE DARK_RT=1)
  message(STATUS "Path tracer: RTGL1 (${DARK_RTGL1_INCLUDE})")
endif()
