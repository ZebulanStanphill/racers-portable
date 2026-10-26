# Temporary Xbox diagnostic. This target must not inherit miniwin's include/miniwin:
# its windows.h, objbase.h, etc. deliberately replace the real Windows SDK headers.
add_library(miniwin-d3dprobe STATIC "${CMAKE_CURRENT_LIST_DIR}/probe.cpp")
target_include_directories(miniwin-d3dprobe PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/internal")
target_compile_definitions(miniwin-d3dprobe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(miniwin-d3dprobe PRIVATE SDL3::SDL3 d3d12 d3d11 dxgi runtimeobject OneCore)
target_link_libraries(miniwin PRIVATE miniwin-d3dprobe)

cmake_host_system_information(RESULT _probe_kits_root
  QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots" VALUE KitsRoot10)
if(NOT _probe_kits_root)
  set(_probe_kits_root "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
endif()
file(TO_CMAKE_PATH "${_probe_kits_root}" _probe_kits_root)
set(_probe_sdk_hints)
foreach(_sdk_version IN ITEMS ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION} ${CMAKE_SYSTEM_VERSION} $ENV{WindowsSDKVersion})
  string(REGEX REPLACE "[/\\]+$" "" _sdk_version "${_sdk_version}")
  list(APPEND _probe_sdk_hints
    "${_probe_kits_root}/bin/${_sdk_version}/x64" "${_probe_kits_root}/bin/${_sdk_version}/x86")
endforeach()
find_program(RACERS_PROBE_FXC_BIN NAMES fxc.exe fxc HINTS ${_probe_sdk_hints} REQUIRED)
find_program(RACERS_PROBE_DXC_BIN NAMES dxc.exe dxc HINTS ${_probe_sdk_hints} REQUIRED)
message(STATUS "D3D probe FXC: ${RACERS_PROBE_FXC_BIN}")
message(STATUS "D3D probe DXC (validated DXIL, no -Vd): ${RACERS_PROBE_DXC_BIN}")

set(_probe_generated "${CMAKE_CURRENT_BINARY_DIR}/d3dprobe")
file(MAKE_DIRECTORY "${_probe_generated}")
foreach(_format IN ITEMS dxbc dxil)
  foreach(_stage IN ITEMS vs ps)
    set(_header "${_probe_generated}/probe_${_stage}_${_format}.h")
    set(_symbol "g_probe_${_stage}_${_format}")
    if(_format STREQUAL "dxbc")
      set(_compiler "${RACERS_PROBE_FXC_BIN}")
      set(_args /nologo /T ${_stage}_5_1 /E ${_stage}_main /D PROBE_DXIL=0 /Vn ${_symbol} /Fh "${_header}")
    else()
      set(_compiler "${RACERS_PROBE_DXC_BIN}")
      # Keep validation enabled: an SDK DXC uses its adjacent dxil.dll (or its
      # internal validator). A missing validator / compilation failure is fatal.
      set(_args -WX -T ${_stage}_6_0 -E ${_stage}_main -D PROBE_DXIL=1 -Vn ${_symbol} -Fh "${_header}")
    endif()
    add_custom_command(OUTPUT "${_header}"
      COMMAND "${_compiler}" ${_args} "${CMAKE_CURRENT_LIST_DIR}/probe.hlsl"
      DEPENDS "${CMAKE_CURRENT_LIST_DIR}/probe.hlsl"
      COMMENT "Compiling D3D probe ${_stage} ${_format}"
      VERBATIM)
    target_sources(miniwin-d3dprobe PRIVATE "${_header}")
  endforeach()
endforeach()
target_include_directories(miniwin-d3dprobe PRIVATE "${_probe_generated}")
