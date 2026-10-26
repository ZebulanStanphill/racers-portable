# racers-portable — agent notes

Cross-platform port (SDL3) of the [LEGO Racers decompilation](https://github.com/isledecomp/racers), modeled on [isle-portable](https://github.com/isledecomp/isle-portable). Windows/Linux/macOS today; emscripten deferred.

## Ground rules

- **The decompilation is the source of truth.** `LEGORacers/`, `GolDP/`, `common/` and `util/` come from the upstream decomp (git remote `racers`) and must stay mergeable: keep reccmp annotations (`// FUNCTION:`, `// GLOBAL:`, …) intact and do not rename or reorganize decomp code here.
- **Upstream-first policy.** Any change the original 32-bit binary could still byte-match (type modeling, renames, restructuring) is made in the decomp repo first — verified there with a full reccmp report diff — and arrives here by merge/rebase. Only genuine portability work is made directly in this repo.
- **Keep the touched-file surface tiny.** Rewritten app boundary: `LEGORacers/src/app/main.cpp`, `win32golapp.cpp`, `LEGORacers/src/video/` (player, AVI demuxer). Everything else in decomp directories should compile unmodified against the `miniwin/` headers; pointer-width fixes are a few lines each and tagged `// 64-bit compatibility:`. Platform hooks in decomp-adjacent code are tagged `// [library:xxx]` (window, filesystem, synchronization, input, audio, 2d, 3d, video, config).
- All platform work lives in `miniwin/` (DirectDraw/D3D6-IM, DirectSound, DirectInput 5 and a small Win32 surface over SDL3 + miniaudio) and `3rdparty/` (vendored FFmpeg n4.4.4 Indeo 5 decoder behind a minimal shim; `Indeo5_Open/Decode/Close`).
- `clang-format` everything you touch (CI-enforced; the vendored FFmpeg sources under `3rdparty/ffmpeg-indeo5/` except the wrapper are exempt and must stay byte-unmodified).

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

`DOWNLOAD_DEPENDENCIES=ON` (default) fetches SDL3 at a pinned release; with it OFF, a system SDL3 is found via `find_package`. A game installation (LEGO.JAM, `*.TUN`, Indeo 5 `*.AVI`) is required to run: `./build/LEGORacers --path <gamedir>`.

## Testing aids (environment variables)

- `RACERS_AUTOKEY="ms:scancode[:holdMs],..."` — scripted key injection (SDL scancodes; Alt/Ctrl/Shift chord state is tracked).
- `RACERS_TOUCH=1` — mouse-as-finger testing: SDL mouse→touch synthesis feeds the touch layer so drag-steering/buttons/taps work without a touchscreen (touch-synthesized menu clicks are suppressed for these fingers to avoid doubling the real mouse).
- `RACERS_AUTOTOUCH="ms:d|m|u:nx:ny[:finger],..."` — scripted touch injection (normalized window coords; hold taps ≥50 ms so the per-frame edge polling sees them; `finger` defaults to 1, so a concurrent second finger needs an explicit `:2`).
- `RACERS_DUMP_EVERY=ms:prefix` / `RACERS_DUMP_FRAME=N:path` — periodic / single-frame screenshots.
- `RACERS_WATCHDOG=<ms>` — reports game-thread stalls with the miniwin phase they occurred in; slow operations (>30 ms) log themselves.
- `RACERS_GL_STATS=1` — per-100-frame draw stats and frame-time min/avg/max (run with `SDL_LOGGING="*=info"`).
- `RACERS_VSYNC=1`, `RACERS_WINDOW_TITLE=...` — vsync A/B lever; distinct window title for scripted test instances.

## Player-facing options

`--path <dir>`, `--language <index>`, `--renderer sdlgpu|opengl3|opengles3|d3d11`, `--scale letterbox|stretch`, `--resolution native|original`, `--widescreen on|off`, plus the original arguments (`-window`, `-novideo`, …). No `-window` means fullscreen. Alt+Enter toggles fullscreen.

Renderers: SDL3 GPU (default), OpenGL 3.3, OpenGL ES 3 / WebGL2, and native Direct3D 11 (Windows, feature level 10.0+). The Xbox build falls back from SDL_GPU to D3D11 on Xbox One; Series X|S keep SDL_GPU when available. D3D11 uses build-time Windows SDK FXC Shader Model 4.0 shaders and an isolated SDK target to avoid miniwin header clashes. See `miniwin/src/d3d/backends/d3d11/README.md` for threading and implementation notes.

Widescreen (`--widescreen on|off`, Options → Video, persisted like the renderer): active only when the window is wider than 4:3, otherwise byte-identical to off. Game coordinates stay 640x480; each submitted draw is tagged UI (default: menus, loading, movies, HUD, pause — mapped into a centred 4:3 region) or world (the race scene and screen fades, mapped across the full width) at submission, so SDL_GPU's deferred replay never reads global state. Race cameras get the window aspect (2x in Versus) so projection and culling widen together. Policy lives in `miniwin/src/d3d/backends/widescreen.cpp`, the Options row in `miniwin/src/game/widescreenoptions.cpp`; game-side hooks are tagged `// [library:3d]` / `// [library:config]` / `// [library:input]`. Design, limits and test commands: `miniwin/src/d3d/backends/WIDESCREEN.md`.

Controls (Options → Player 1/Player 2 Controls): Reset Controls restores the default bindings for the device or keyboard set currently shown in the carousel, leaving joystick steering unchanged. The localized button and reset logic live in `miniwin/src/game/resetcontrols.cpp`; the screen hooks are tagged `// [library:input]`. Both menu adapters share the language-folder lookup in `miniwin/src/game/menulanguage.h`.

Touch (touchscreens/web): always-on additive controls — left-half drag steers (thumb held = throttle), drawn buttons for power-up/slide/brake + pause/look-back, tap-to-click menus, pause-dialog ▲✓▼ cluster, OSK name entry. Single local player only: Versus split-screen sessions stand the whole layer down (no overlay, fingers ignored; menus unaffected). Layer lives in `miniwin/src/dinput/touch.cpp` + `miniwin/src/d3d/backends/overlay.cpp`; game-side hooks are tagged `// [library:input]` (playercontrols.cpp, racesession.cpp, menutextfield.cpp, driverlicensescreen.cpp).

## This fork

This fork's own rules (commit scope, rebasing, attribution, delegation) are in [AGENTS.md](/AGENTS.md).
