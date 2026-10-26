# Widescreen mode

`--widescreen on|off` (persisted in `widescreen` next to the `renderer` preference,
default off) uses the whole window instead of letterboxing it. It is active only
while the drawable is wider than 4:3; in any other window the output is identical to
off, and a change of the setting or of the window size applies on the next frame
without a relaunch. Widescreen overrides `--scale stretch`.

## Coordinates and the two draw phases

The game keeps submitting pre-transformed vertices in a 640x480 logical space. Each
draw is tagged when it is submitted:

- **UI** (the default): menus, loading screens, movies, cutscenes, the pause menu
  and the race HUD. `MiniwinRenderBackend::SubmitTriangles` maps logical x into a
  centred 4:3 region of the wide scene (`UIWidthFraction()`, offset
  `(1 - fraction) * 640 / 2`); y is untouched. Aspect, fonts and the sample lattice
  are exactly those of a 4:3 window of the same height.
- **World**: the race scene and full-screen fades. A `MiniwinWorldScope` in
  `RaceSession::Draw` (and in `DrawFullWidthFades` for the fade quads) leaves
  logical x untouched, so 0..640 spans the whole wide scene. Scopes nest: leaving one
  restores the phase that was active when it began.

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
first draw (`ClearStaleSideBars`). The pause menu keeps the world frozen behind it
and covers the panels explicitly (`MiniwinWidescreen_Pillarbox`).

`--resolution original` renders a 480-high scene whose width is `480 * aspect`,
rounded so that its difference from 640 is even; the centred UI then lands on whole
scene pixels and stays a 1:1 640x480 blit.

## Race cameras

`RaceSession::Draw` sets each race camera's aspect to the window aspect, or twice that
in Versus (each half is 640x240). The vertical field of view (including the Versus
-25 degrees and per-frame FOV animation) is untouched, so framing is unchanged and
`GolCameraBase::ComputeFrustum` widens with the projection: culling matches the view
and nothing pops in at the side edges. Menu and cutscene cameras are not touched.

## Input

Absolute mouse positions, clicks and touches go through
`MiniwinWidescreen_MapUI`, the inverse of the UI mapping; input in a side panel does
not hit UI (menu fingers there are ignored, windowed mouse presses are dropped, the
windowed cursor reports "outside"). Fullscreen drives the game cursor by relative
motion, as before, and is not filtered. The touch overlay works in drawable pixels and
is unaffected.

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
