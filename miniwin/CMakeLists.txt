add_library(miniwin STATIC
  # Core Win32
  src/windows/windows.cpp
  src/windows/handles.cpp
  src/windows/files.cpp
  src/windows/registry.cpp
  src/windows/mmsystem.cpp
  src/windows/events.cpp
  src/windows/guids.cpp

  # DirectDraw
  src/ddraw/ddraw.cpp
  src/ddraw/ddsurface.cpp
  src/ddraw/ddpalette.cpp

  # Direct3D (immediate mode)
  src/d3d/d3d.cpp
  src/d3d/d3ddevice.cpp

  # Render backend selection/dispatch (always compiled)
  src/d3d/backends/backends.cpp
  src/d3d/backends/widescreen.cpp
  src/d3d/backends/overlay.cpp

  # DirectSound
  src/dsound/dsound.cpp

  # DirectInput
  src/dinput/dinput.cpp
  src/dinput/touch.cpp
)

# Xbox (UWP) app support: finding the game files without a command line, and a log file.
if(WINDOWS_STORE)
  target_sources(miniwin PRIVATE src/windows/uwpdatadir.cpp src/windows/uwplog.cpp)
endif()

# Render backends are selected at build time per platform, mirroring isle-portable:
# each entry becomes a USE_* compile definition, and the matching source is compiled
# only when its entry is present.
list(APPEND GRAPHICS_BACKENDS USE_SDL_GPU)

# Native D3D11 also supports Xbox One's feature level 10.x hardware.
if(WIN32)
  list(APPEND GRAPHICS_BACKENDS USE_D3D11)
  target_sources(miniwin PRIVATE src/d3d/backends/d3d11/renderer.cpp)
  include(src/d3d/backends/d3d11/backend.cmake)
endif()

# Desktop OpenGL 3.3 core. Reached through SDL_GL_GetProcAddress, so no OpenGL library
# is linked (SDL loads it) — unlike isle's OpenGL 1.x backend. Not available on the web
# (WebGL/GLES only) or typical console/handheld targets.
if(NOT (EMSCRIPTEN OR ANDROID OR IOS OR VITA OR NINTENDO_SWITCH OR NINTENDO_3DS OR WINDOWS_STORE))
  list(APPEND GRAPHICS_BACKENDS USE_OPENGL3)
endif()

# OpenGL ES 3 / WebGL2. Also loaded through SDL_GL_GetProcAddress (no GLES library is
# linked; SDL supplies GLES/ANGLE natively and WebGL2 under Emscripten). This is the web
# target's only backend and an optional desktop/mobile one; excluded only where SDL has
# no GL/GLES at all.
if(NOT (VITA OR NINTENDO_SWITCH OR NINTENDO_3DS OR WINDOWS_STORE))
  list(APPEND GRAPHICS_BACKENDS USE_OPENGLES3)
endif()

# SDL_GPU has no web backend; drop it there (the GLES3 backend covers web).
if(EMSCRIPTEN)
  list(REMOVE_ITEM GRAPHICS_BACKENDS USE_SDL_GPU)
endif()

if(USE_SDL_GPU IN_LIST GRAPHICS_BACKENDS)
  target_sources(miniwin PRIVATE src/d3d/backends/sdlgpu/renderer.cpp)
endif()

if(USE_OPENGL3 IN_LIST GRAPHICS_BACKENDS)
  target_sources(miniwin PRIVATE src/d3d/backends/opengl3/renderer.cpp)
endif()

if(USE_OPENGLES3 IN_LIST GRAPHICS_BACKENDS)
  target_sources(miniwin PRIVATE src/d3d/backends/opengles3/renderer.cpp)
endif()

target_compile_definitions(miniwin PUBLIC MINIWIN ${GRAPHICS_BACKENDS})

target_include_directories(miniwin
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include/miniwin"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/internal"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d/backends"
)

# POSIX shims for MSVC-specific CRT headers (<io.h>); real Windows provides these natively.
if (NOT WIN32)
  target_include_directories(miniwin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/shims")
endif()

target_link_libraries(miniwin PRIVATE SDL3::SDL3 miniaudio)

# Offline SDL_GPU shader (re)generation — developer/CI only. The committed header
# src/d3d/backends/sdlgpu/shaders/generated/miniwin_sdlgpu_shaders.h is what the build
# compiles; this target refreshes it from the GLSL sources when glslang + SDL_shadercross
# are on PATH. DXIL (D3D12) needs a DXC-enabled shadercross, which has no macOS prebuilt,
# so it is filled in by a Linux CI job; SPIR-V + MSL are produced everywhere. It also
# refreshes the Direct3D 11 HLSL in generated/d3d11/ (see the UWP block below).
if(USE_SDL_GPU IN_LIST GRAPHICS_BACKENDS)
  find_program(RACERS_GLSLANG_BIN NAMES glslangValidator glslang)
  find_program(RACERS_SHADERCROSS_BIN NAMES shadercross)
  if(RACERS_GLSLANG_BIN AND RACERS_SHADERCROSS_BIN)
    set(_sdlgpu_shaders "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d/backends/sdlgpu/shaders")
    add_custom_target(sdlgpu-shaders
      COMMAND python3 "${_sdlgpu_shaders}/gen_shaders.py"
        "${_sdlgpu_shaders}/src"
        "${_sdlgpu_shaders}/generated/miniwin_sdlgpu_shaders.h"
        "${RACERS_GLSLANG_BIN}" "${RACERS_SHADERCROSS_BIN}"
      COMMENT "Regenerating SDL_GPU shader bytecode header"
      VERBATIM
    )
  endif()
endif()

# Direct3D 11 shaders for UWP. SDL3-uwp's only SDL_GPU driver there is Direct3D 11, which
# takes Shader Model 5.0 DXBC. Only Microsoft's FXC makes DXBC, and it runs only on
# Windows, so rather than committing blobs (the shader regeneration job runs on Linux)
# this compiles the generator's HLSL in src/d3d/backends/sdlgpu/shaders/generated/d3d11/
# with the Windows SDK's fxc.exe. No other build gets DXBC: desktop Windows's D3D12
# driver also accepts DXBC, but it must keep using DXIL.
if(WINDOWS_STORE AND USE_SDL_GPU IN_LIST GRAPHICS_BACKENDS)
  # fxc.exe is on PATH in a Visual Studio developer prompt; otherwise look in the Windows
  # Kits 10 bin directory of the SDK being built against.
  cmake_host_system_information(RESULT _kits_root
    QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots" VALUE KitsRoot10)
  if(NOT _kits_root)
    set(_kits_root "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
  endif()
  file(TO_CMAKE_PATH "${_kits_root}" _kits_root)
  set(_fxc_hints)
  foreach(_sdk_version IN ITEMS ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION} ${CMAKE_SYSTEM_VERSION})
    list(APPEND _fxc_hints "${_kits_root}/bin/${_sdk_version}/x64" "${_kits_root}/bin/${_sdk_version}/x86")
  endforeach()
  find_program(RACERS_FXC_BIN NAMES fxc HINTS ${_fxc_hints})
  if(NOT RACERS_FXC_BIN)
    message(FATAL_ERROR
      "fxc.exe was not found. The UWP build needs it (from the Windows SDK) to compile its "
      "Direct3D 11 shaders. Install the Windows SDK, run CMake from a Visual Studio "
      "developer prompt, or set RACERS_FXC_BIN to fxc.exe's path.")
  endif()

  set(_d3d11_hlsl_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d/backends/sdlgpu/shaders/generated/d3d11")
  set(_dxbc_dir "${CMAKE_CURRENT_BINARY_DIR}/sdlgpu_dxbc")
  file(MAKE_DIRECTORY "${_dxbc_dir}")
  # Each header declares `const BYTE g_scene_<stage>_dxbc[]` (entry point "main").
  foreach(_shader IN ITEMS "vert;vs_5_0" "frag;ps_5_0")
    list(GET _shader 0 _stage)
    list(GET _shader 1 _profile)
    set(_hlsl "${_d3d11_hlsl_dir}/scene.${_stage}.hlsl")
    set(_dxbc_header "${_dxbc_dir}/scene_${_stage}_dxbc.h")
    add_custom_command(OUTPUT "${_dxbc_header}"
      COMMAND "${RACERS_FXC_BIN}" /nologo /T ${_profile} /E main /Vn g_scene_${_stage}_dxbc
        /Fh "${_dxbc_header}" "${_hlsl}"
      DEPENDS "${_hlsl}"
      COMMENT "Compiling scene.${_stage}.hlsl to Shader Model 5.0 DXBC"
      VERBATIM
    )
    target_sources(miniwin PRIVATE "${_dxbc_header}")
  endforeach()
  target_include_directories(miniwin PRIVATE "${_dxbc_dir}")
  target_compile_definitions(miniwin PRIVATE MINIWIN_SDLGPU_DXBC)
endif()
