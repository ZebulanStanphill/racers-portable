# Widescreen mode

`--widescreen on|off` (also Options → Video, persisted in `widescreen` next to the
`renderer` preference, default off) widens the race view to the window instead of
letterboxing it. It is active only while the drawable is wider than 4:3; in any other
window the output is identical to off, and a toggle or window resize applies on the
next frame without a relaunch. Widescreen overrides `--scale stretch`.

## Coordinates and the two draw phases

The game keeps submitting pre-transformed vertices in a 640x480 logical space. Each
draw is tagged when it is submitted:

- **UI** (the default): menus, loading screens, movies, cutscene text, the pause menu
  and the race HUD. `MiniwinRenderBackend::SubmitTriangles` maps logical x into a
  centred 4:3 region of the wide scene (`UIWidthFraction()`, offset
  `(1 - fraction) * 640 / 2`); y is untouched. Aspect, fonts and the sample lattice
  are exactly those of a 4:3 window of the same height.
- **World**: the race scene, full-screen rendered cutscenes and full-screen fades. A `MiniwinWorldScope` in
  `RaceSession::Draw` (and in `DrawFullWidthFades` for the fade quads) leaves
  logical x untouched, so 0..640 spans the whole wide scene.

The tag is read at submission time and copied into the recorded vertices, never at
replay: SDL_GPU records every draw/clear and replays them in `Present`, so no global
state may be consulted later. Draws made outside a scope (video, DirectDraw surface
composites, menus) are UI by default. The D3D6 half-pixel offset and point-quad
lattice (`RASTERIZATION.md`) are applied in logical coordinates first; the lattice
is carried in UV space, so UI draws at 1.5x or 1:1 and world draws at any scale keep
their original sample footprint. The lattice is switched off for UI draws only when
the centred region is exactly 640 wide (`--resolution original`).

`ClearScene` follows the same phases: a full UI clear wipes the whole wide target
(removing a previous race frame from the side panels) and fills only the centred
region with a non-black colour. `PresentScene` ends a frame; if the frame drew the
world, the next frame that draws only UI paints the side panels black before its
first draw (`ClearStaleSideBars`). The pause menu's dimming backdrop
is drawn in a world scope too (`RaceDialog::Draw`), so it dims the whole widened view
while its text stays in the centred UI.

`--resolution original` renders a 480-high scene whose width is `480 * aspect`,
rounded so that its difference from 640 is even; the centred UI then lands on whole
scene pixels and stays a 1:1 640x480 blit.

## Two modules (Windows, Xbox)

There LEGORacers.exe and GolDP.dll each statically link their own copy of miniwin, so
a plain static is two variables (macOS/Linux happen to merge them). The preference,
the world flag and the "backend on screen" pointer therefore live in one block
published through SDL's global properties (`racers.miniwin.widescreen`, see
`widescreen.cpp`). The exe-facing functions (`MiniwinWidescreen_Active`,
`_RaceAspect`, `_MapUI`) use the backend that last presented (the DLL's
renderer), not the exe's own `MiniwinBackend_Get()` (the intro-video backend). The
scale mode and render resolution are not shared this way yet.

## Race cameras

`RaceSession::Draw` sets each race camera's aspect to the window aspect, or twice that
in Versus (each half is 640x240). The vertical field of view (including the Versus
-25 degrees and per-frame FOV animation) is untouched, so framing is unchanged and
`GolCameraBase::ComputeFrustum` widens with the projection: culling matches the view
and nothing pops in at the side edges. Full-screen `SceneWidget` cinematics also
widen their camera aspect during drawing, then restore it. Their 3D content spans
the window while image and text visuals keep the centred UI layout.
`CutscenePlayer::DrawOverlay` widens only its `MenuAnimationList` effects when the
camera viewport covers the whole render target. `CutsceneMenuAnimationEvent`
activates these quads without a camera rectangle, so they cover the full target:
this includes victory camera flashes and fade-to-black transitions. Their alpha,
colour and timing are unchanged. Embedded menu previews and prerecorded AVI movies
retain their original framing. Authored
cutscenes can reveal content beyond the original 4:3 composition.

## Input

Absolute mouse positions, clicks and touches go through
`MiniwinWidescreen_MapUI`, the inverse of the UI mapping; input in a side panel does
not hit UI (menu fingers there are ignored, windowed mouse presses are dropped, the
windowed cursor reports "outside"). Fullscreen drives the game cursor by relative
motion, as before, and is not filtered. The touch overlay works in drawable pixels and
is unaffected.

## Options → Video row

`miniwin/src/game/widescreenoptions.cpp` builds a selector by copying the OPTIONS.MIB
selector binding and style the renderer row uses (the MIB is never edited), below the
renderer selector. Its values are the game's translated YES/NO strings (MENUTEXT
strings 115/116) and its label is looked up by language folder
(`ENGLISH` fallback; language index 10 is Finnish). It lives in the app, not in miniwin's library, because it
depends on the menu classes.

## Known limits

- The HUD is not anchored to the screen edges.
- Fullscreen mouse speed is whatever it is without widescreen.

## Tests

```sh
c++ -std=c++17 -Iminiwin/src/d3d -Iminiwin/src/internal \
  -Iminiwin/include -Iminiwin/include/miniwin -Ibuild/_deps/sdl3-src/include \
  miniwin/src/d3d/backends/widescreen.cpp miniwin/tests/widescreen.cpp \
  -Lbuild/_deps/sdl3-build -lSDL3 -Wl,-rpath,$PWD/build/_deps/sdl3-build \
  -o /tmp/miniwin-widescreen
/tmp/miniwin-widescreen
```

The test links the real policy file against a capture backend and checks the 4:3
identity, UI/world mapping, clear handling, side panels, scene sizing and input
mapping.
