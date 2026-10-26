# Keep real SDK headers out of miniwin's compatibility-header translation units.
# Never link this target back to miniwin: that would inherit include/miniwin.
add_library(miniwin-d3d11 STATIC "${CMAKE_CURRENT_LIST_DIR}/native.cpp")
target_compile_definitions(miniwin-d3d11 PRIVATE USE_D3D11 WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(miniwin-d3d11 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/internal")
target_link_libraries(miniwin-d3d11 PRIVATE SDL3::SDL3 d3d11 dxgi)
target_link_libraries(miniwin PRIVATE miniwin-d3d11)

cmake_host_system_information(RESULT _d3d11_kits_root
  QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots" VALUE KitsRoot10)
if(NOT _d3d11_kits_root)
  set(_d3d11_kits_root "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
endif()
file(TO_CMAKE_PATH "${_d3d11_kits_root}" _d3d11_kits_root)
set(_d3d11_fxc_hints)
foreach(_sdk IN ITEMS ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION} ${CMAKE_SYSTEM_VERSION} $ENV{WindowsSDKVersion})
  string(REGEX REPLACE "[/\\]+$" "" _sdk "${_sdk}")
  list(APPEND _d3d11_fxc_hints "${_d3d11_kits_root}/bin/${_sdk}/x64" "${_d3d11_kits_root}/bin/${_sdk}/x86")
endforeach()
find_program(RACERS_D3D11_FXC_BIN NAMES fxc.exe fxc HINTS ${_d3d11_fxc_hints})
if(NOT RACERS_D3D11_FXC_BIN)
  message(FATAL_ERROR "The D3D11 backend requires Windows SDK fxc.exe. Run from a Visual Studio developer prompt or set RACERS_D3D11_FXC_BIN.")
endif()
set(_d3d11_generated "${CMAKE_CURRENT_BINARY_DIR}/d3d11")
file(MAKE_DIRECTORY "${_d3d11_generated}")
foreach(_stage IN ITEMS vs ps)
  set(_header "${_d3d11_generated}/scene_${_stage}.h")
  add_custom_command(OUTPUT "${_header}"
    COMMAND "${RACERS_D3D11_FXC_BIN}" /nologo /WX /T ${_stage}_4_0 /E ${_stage}_main
      /Vn g_d3d11_${_stage} /Fh "${_header}" "${CMAKE_CURRENT_LIST_DIR}/scene.hlsl"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/scene.hlsl"
    COMMENT "Compiling native D3D11 ${_stage} (Shader Model 4.0)"
    VERBATIM)
  target_sources(miniwin-d3d11 PRIVATE "${_header}")
endforeach()
target_include_directories(miniwin-d3d11 PRIVATE "${_d3d11_generated}")
