# LEGO Racers, portable

This initiative is a portable version of LEGO Racers (1999) based on the [decompilation project](https://github.com/isledecomp/racers). Our primary goal is to transform the codebase to achieve platform independence, thereby enhancing compatibility across various systems while preserving the original game's experience as faithfully as possible.

Please note: this project is primarily dedicated to achieving platform independence without altering the core gameplay or rewriting code for improvement's sake. While those are worthwhile objectives, they are not within the scope of this project.

## Status

| Platform | Status |
| - | - |
| Windows (x64) | [![CI](https://github.com/isledecomp/racers-portable/actions/workflows/ci.yml/badge.svg)](https://github.com/isledecomp/racers-portable/actions/workflows/ci.yml) |
| macOS (arm64) | [![CI](https://github.com/isledecomp/racers-portable/actions/workflows/ci.yml/badge.svg)](https://github.com/isledecomp/racers-portable/actions/workflows/ci.yml) |
| Linux (x64) | [![CI](https://github.com/isledecomp/racers-portable/actions/workflows/ci.yml/badge.svg)](https://github.com/isledecomp/racers-portable/actions/workflows/ci.yml) |
| [Web](https://warp.parts) | [![CI](https://github.com/isledecomp/racers-portable/actions/workflows/ci.yml/badge.svg)](https://github.com/isledecomp/racers-portable/actions/workflows/ci.yml) |

We are actively working to support more platforms. If you have experience with a particular platform, we encourage you to contribute to `racers-portable`.

## Usage

**An existing copy of LEGO Racers is required to use this project.**

Place the built `LEGORacers` executable next to the game's data files (`LEGO.JAM`, the `*.TUN` music files, and the intro `*.AVI` movies), or point it at them with `--path <directory>`. The original game's command-line arguments (`-window`, `-novideo`, `-horzres`, `-vertres`, …) are supported, plus:

- `--renderer sdlgpu|opengl3|opengles3|d3d11` — select the render backend: SDL3 GPU (default; Vulkan/Metal/Direct3D 12), OpenGL 3.3, OpenGL ES 3.0 / WebGL2, or native Direct3D 11 (Windows; feature level 10.0+). It can also be changed in-game under Options → Video (the game relaunches to apply it; on Xbox, restart the app manually).
- `--scale letterbox|stretch` — fullscreen scaling: keep the 4:3 aspect ratio with black bars (default) or fill the screen.
- `--resolution native|original` — render 3D at the display resolution (default) or at the game's original 640x480, upscaled. Point-sampled 2D blits preserve their original sample footprint at native resolution; see the [D3D6 rasterization notes](miniwin/src/d3d/backends/RASTERIZATION.md).
- `--widescreen on|off` — widen races and full-screen rendered cutscenes to fill windows and displays wider than 4:3 (default off). It can also be switched live under Options → Video, and is remembered like the renderer. Cameras gain horizontal field of view at the original vertical framing; menus, loading screens, prerecorded movies, cutscene text, the pause menu and the HUD keep their original proportions. It overrides `--scale stretch`, and looks identical to off in a 4:3 or taller window. See the [widescreen notes](miniwin/src/d3d/backends/WIDESCREEN.md).
- `--language <index>` — seeds the language selection the original stored in the registry.

Alt+Enter toggles fullscreen. The intro movies play from the original Indeo 5 AVIs.

On a gamepad, the D-pad works like the left stick: it moves through menus and steers in races.

Default gamepad race controls follow an Xbox controller: steer with the left stick or D-pad, accelerate with A, brake with X, power-up with LB, camera with Y, powerslide with RB, look back with B, and cycle the HUD with View. Existing profiles keep their saved gamepad controls; use **Reset Controls** on the Player 1 or Player 2 Controls screen to get this layout. Reset Controls restores the defaults for the device or keyboard set currently shown in the carousel; joystick steering stays unchanged. Controls you reassigned on a gamepad before the Xbox button order was introduced now refer to different buttons.

Menu (Start) pauses a race. With the pause menu open it closes it again, choosing Continue (or "No" in a confirmation). Assigning a race action to Menu on the controls screen takes priority over pausing.

B goes back: in menus it presses the screen's Back button (A and the other buttons still select), and with the pause menu open it resumes like Menu does. During a race B is an ordinary button. On the controls screen, B and Menu can be assigned like any other button.

The right stick moves the visible menu cursor, with finer control near the center; A selects the focused item, and race controls keep their existing stick bindings.

## Library substitutions

To achieve our goal of platform independence, we need to replace any Windows-only libraries with platform-independent alternatives. This ensures that our codebase remains versatile and compatible across various systems. The following table serves as an overview of major libraries / subsystems and their chosen replacements.

| Library/subsystem | Substitution | Status |
| - | - | - |
| Window, Events | [SDL3](https://www.libsdl.org/) | ✅ |
| Filesystem | [SDL3](https://www.libsdl.org/), POSIX shims | ✅ |
| Threads, Mutexes (Synchronization) | [SDL3](https://www.libsdl.org/) | ✅ |
| Keyboard/Mouse, DirectInput (Input) | [SDL3](https://www.libsdl.org/) | ✅ |
| Joystick/Gamepad + Force Feedback, DirectInput (Input) | [SDL3](https://www.libsdl.org/) | ✅ |
| WinMM (timers), DirectSound (Audio) | [SDL3](https://www.libsdl.org/), [miniaudio](https://miniaud.io/) | ✅ |
| DirectDraw (2D video) | [SDL3](https://www.libsdl.org/) | ✅ |
| Direct3D immediate mode (3D rendering) | [SDL3 GPU](https://www.libsdl.org/) (default; Vulkan/Metal/D3D12), OpenGL 3.3, OpenGL ES 3.0 / WebGL2, and native Direct3D 11 (Windows) | ✅ |
| DirectShow, Indeo 5 (Video playback) | In-tree AVI demuxer + Indeo 5 decoder (vendored from [FFmpeg](https://ffmpeg.org/) n4.4.4, byte-exact against ffmpeg), [SDL3](https://www.libsdl.org/) | ✅ |
| Windows Registry (Language selection) | Command line (`--language`) | ✅ |

## Building

This project uses the [CMake](https://cmake.org/) build system, which allows for a high degree of versatility regarding compilers and development environments.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

## Xbox (UWP)

This fork builds LEGO Racers as a UWP app for the Xbox One and Xbox Series X|S, the same way [isle-portable](https://github.com/isledecomp/isle-portable) does for LEGO Island: with the [SDL3-uwp](https://github.com/Helloyunho/SDL3-uwp) fork of SDL3, which keeps SDL's UWP support. The app only installs on an Xbox in [Developer Mode](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/devkit-activation).

The build applies our fixes to SDL3-uwp from [patches/](patches/): `sdl3-uwp-controller-duplicates.patch` keeps each controller from showing up twice (and every press counting twice) when the Xbox runs the app as a game.

The app's tiles show the LEGO Racers cover art, and its splash screen a fan-made recreation of the LEGO Racers logo by Eastwood2002, with the LEGO Group's own LEGO logo. See [packaging/UWP/assets/README.md](packaging/UWP/assets/README.md) for the full credits and sources.

### Installing

1. Download the `legoracers-Xbox One` artifact from a run of the [CI workflow](https://github.com/ZebulanStanphill/racers-portable/actions/workflows/ci.yml) and unzip it. Its `legoracers/legoracers_<version>_x64_Test/` folder contains the app package (`legoracers_<version>_x64.msix`), the certificate it is signed with (`.cer`), and `Dependencies/x64/Microsoft.VCLibs.x64.14.00.appx`, the Visual C++ runtime the app needs.
2. On the Xbox, open Dev Home and turn on Xbox Device Portal (under Remote Access), then open its address in a browser on your computer.
3. Under **Home** → **My games & apps**, select **Add**, choose the `.msix` file and select **Next**. Add `Microsoft.VCLibs.x64.14.00.appx` as a dependency (and the `.cer`, if Device Portal asks for a certificate), then select **Start** to install.

Every build is signed with the same certificate (`packaging/UWP/RacersPortable.pfx`), and each CI build has a higher version than the one before (`0.1.<run number>.0`), so installing a newer build updates the app in place and keeps its saved data. Windows won't install an older build over a newer one: uninstall the app first to go back.

**Uninstalling the app deletes your saved games and settings** (see [Game files, saves and the log](#game-files-saves-and-the-log)). To try a newer build, install it over the old one instead.

### Game files, saves and the log

The Xbox app gets no command line, so it looks for the game files in two folders and uses the first one that has `LEGO.JAM`:

1. `E:\racers\`: a USB drive plugged into the Xbox and formatted for *media* (not apps/games) shows up as `E:\`, and apps can read and write it. Create a `racers` folder at the root of the drive.
2. `D:\DevelopmentFiles\racers\`: Dev Mode's development files folder, which a PC can reach over the network (SMB) or through the Device Portal's file explorer.

Copy the files of an installed copy of LEGO Racers into that folder: `LEGO.JAM`, the `*.TUN` music files and the intro `*.AVI` movies (so you have e.g. `E:\racers\LEGO.JAM`). If neither folder has them, the game says so and quits.

The Xbox build tries SDL3 GPU first, then falls back to native Direct3D 11 on Xbox One, whose feature level 10.x device cannot initialize SDL_GPU. Xbox Series X|S keep using SDL3 GPU when available. Both paths render the intro videos and game through the same backend.

Saved games and the renderer and widescreen choices aren't kept with the game files but in the app's own storage, `LocalState\isledecomp\racers\` (in the Device Portal's file explorer: LocalAppData, then the app's package). Updating the app keeps them; uninstalling it deletes them.

Each launch writes a log, `latest-log.txt`, next to the game files (e.g. `E:\racers\latest-log.txt`), replacing the previous one. If the game files weren't found, their folder can't be written to, or the game failed before it got to them, the log goes to `LocalState\isledecomp\racers\` instead, and the message about the missing files says where. Include it when reporting a problem.

### Building

The Xbox build needs Windows and Visual Studio 2026 with the C++ Universal Windows Platform tools (part of the Windows application development workload) and the Windows SDK 10.0.26100.0. Configure with the Visual Studio generator and the `WindowsStore` system name:

```bash
cmake -S . -B build-uwp -G "Visual Studio 18 2026" -A x64 -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0.26100.0
cmake --build build-uwp --config Release
```

The build writes the signed package and its dependencies to `build-uwp/AppPackages/legoracers/legoracers_<version>_x64_Test/`. Local builds have version 0.1.0.0, so they don't install over a CI build without uninstalling it first.

On the Xbox the game renders with SDL_GPU's Direct3D 11 driver, the only one SDL3-uwp has there. Its shaders are compiled during the build with the Windows SDK's `fxc.exe`, which CMake finds on `PATH` or in the SDK's `bin` directory (or set `RACERS_FXC_BIN` to its path).

## Contributing

If you're interested in helping or contributing to this project, check out the [CONTRIBUTING](/CONTRIBUTING.md) page.
