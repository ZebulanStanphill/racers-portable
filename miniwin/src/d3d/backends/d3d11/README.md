# Native Direct3D 11

`--renderer d3d11` selects this separate Windows backend. It follows SDL_GPU in
fallback order and runs on hardware feature levels 10.0 and newer, including
Xbox One's 10.1 device. Desktop Windows builds compile it too. Non-Windows builds
compile neither the wrapper nor the native target and do not need FXC.

`renderer.cpp` implements `MiniwinRenderBackend` against miniwin's D3D6 types.
`native.cpp` owns D3D11/DXGI objects in a separate static-library target without
miniwin's Win32 compatibility include paths. `native.h` is their SDK-free boundary;
the wrapper checks the TL vertex byte layout at compile time. SDK FXC compiles
`scene.hlsl` to `vs_4_0` / `ps_4_0` headers during the Windows build. Missing FXC is
a configuration error (`RACERS_D3D11_FXC_BIN` can specify its path).

The hardware device requests 11.1, 11.0, 10.1, and 10.0, retrying without 11.1 on
`E_INVALIDARG`. No compute, UAV, or constant-buffer offset features are used.
Constant, vertex, and index data use dynamic buffers with `WRITE_DISCARD`.

All immediate-context calls, resource updates, draws, resize operations and
`Present(1, 0)` run on the game thread. Device creation also runs there, except
for the availability probe (which creates/destroys an independent device on its
caller). SDL window queries and swapchain creation run synchronously on SDL's
main/UI thread, keeping CoreWindow access there. The device is not created with
`SINGLETHREADED`; no context calls occur during the UI-thread callback. UWP uses
`SDL_PROP_WINDOW_WINRT_WINDOW_POINTER` (`SDL.window.winrt.window`) with
`CreateSwapChainForCoreWindow`; desktop uses the HWND property and
`CreateSwapChainForHwnd`. Both use two BGRA8 flip-sequential buffers. Window size
is sampled before a frame's first operation; resizes take effect before drawing,
so an immediate-mode frame is never discarded by a present-time target resize.
On UWP, an in-game renderer choice is saved for the next manual app launch.

The persistent offscreen scene uses native drawable resolution or the original
logical resolution according to `--resolution`. TL clip reconstruction uses
`z = sz * w`, perspective-correct interpolation, and BGRA vertex-color swizzling.
The fragment shader reproduces SDL_GPU's texture/alpha operations, specular add,
alpha compare, and color-key discard. Cached blend/depth/cull states and four
linear/point, wrap/clamp samplers follow the same state mapping. Texture mipmaps
use `GenerateMips`. D3D11 requires alpha equivalents of color blend factors in
the alpha equation; these preserve the component-wise SDL_GPU result.

`BeginScene` prepares targets; `EndScene` needs no submission. Draws execute in
order on the immediate context. Full clears use the view clear APIs; rectangle
clears draw an unblended depth-1 quad with independent color/depth writes
(SDL_GPU currently ignores the clear rectangle). `Present` bilinearly composites
the scene into the letterbox/stretch viewport, emits the alpha-blended touch
overlay in drawable-pixel coordinates, presents with vsync, and invokes the frame
dump hook. `ReadBackbuffer` copies the scene to a staging texture, blocks on its
read map, respects row pitch, and returns an RGBA SDL surface, excluding the
overlay and black bars just like SDL_GPU.

Windows compilation and Xbox output still require the MSVC/UWP CI builds and
hardware testing. Compare intro video, menus, races, mipmaps, culling, overlays,
resize and `RACERS_DUMP_*` captures with SDL_GPU on a Windows PC. Device removal
is logged; automatic device-loss recovery is not implemented (matching the
existing backend's lifetime model).
