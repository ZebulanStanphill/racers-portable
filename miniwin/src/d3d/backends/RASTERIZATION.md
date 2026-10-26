# D3D6 screen coordinates and point-sampled 2D quads

The device boundary in `d3ddevice.cpp` copies game-submitted TL vertices and adds
`(0.5, 0.5)` to screen positions before any backend scales logical coordinates to
its scene target. D3D6 integer screen coordinates name pixel centres; the modern
backends use half-integers. This conversion applies to both 2D and 3D geometry.
It does not change depth, reciprocal W, UVs, or the resolution of 3D rendering.
At 640x480 a hardware pixel centre now interpolates the original integer D3D6
screen position. Point-sampled sprites remain point-sampled; linearly filtered
sprites receive the interpolation that D3D6 would have used.

Miniwin's own video, DirectDraw surface-composite and drawable-space overlay draws
call the backends directly, bypassing this conversion. Scene-to-window letterbox
composites also bypass it. Their existing modern pixel-boundary geometry and UVs
remain unchanged. The shaders contain no unconditional position offset.

## Why the coordinate conversion alone is insufficient

`GolFont::DrawGlyph` uses integer destination rectangles, horizontal UV endpoints
`(sourceLeft + 0.25) / textureWidth` and `(sourceRight + 0.25) / textureWidth`, and
vertical endpoints `sourceTop / textureHeight` and
`(sourceBottom - 1) / textureHeight`. `GolFontBase::PackGlyphTextures` packs glyphs
without padding. The font uses point filtering.

For example, a menu E occupies logical x=[167,180), with atlas x endpoints
8.25 and 21.25 in a 256-wide texture. D3D6's last covered integer x=179 samples
20.25, inside the E's texels [8,21). Before coordinate conversion, at 2.25x scale
the last native pixel centre x=404.5 maps to logical 179.7778 and atlas 21.0278:
point sampling selects texel 21, the next glyph. Bilinear filtering is unnecessary
for this artefact. Moving geometry by half a logical pixel fixes this particular
edge, but other scale phases still reach the neighbouring glyph. In 59 logged
menu glyph quads, 24 sampled outside their horizontal atlas cell before conversion,
and 15 still did so with the half-pixel conversion alone.

These extra subpixel samples did not exist in the 640x480 D3D6 framebuffer.
Magnified point-sampled 2D blits therefore retain the original discrete sampling
footprint, rather than introducing new atlas samples between or beyond it.

## Which draws qualify

`pointquads.h` recognizes a draw only when all of these conditions hold:

- It has exactly four distinct vertices and six indices forming two triangles
  whose shared edge is a diagonal of the complete rectangle. Four-vertex triangle
  strips are triangulated by the device before this check. Batched meshes and
  six-vertex nonindexed triangle lists are not recognized.
- Its screen positions form an axis-aligned, nondegenerate rectangle.
- All four vertices have `sz == 0` and `rhw == 1`, and both depth testing and depth
  writing are disabled.
- A texture participates, filtering is point, and UVs form an axis-aligned,
  nonconstant rectangle. Mirrored UVs and fractional/clipped positions are allowed.

This is a screen-space blit contract, independent of texture IDs, filenames,
glyphs, game states, or a fixed 640x480 constant. Projected 3D geometry with depth
or perspective, depth-tested geometry, rotated sprites and linear filtering all
bypass the sampling restriction. Geometry deliberately submitted with the exact
same depth-free affine blit contract is treated as 2D; TL vertices carry no other
semantic distinction between a sprite and a polygon.

For a qualifying quad, the device supplies its UV at the first covered integer
logical pixel centre, UV step per logical pixel, and covered sample count, clipped
to the logical render target. A qualifying quad with no covered original samples
is discarded when magnified. Each fragment chooses the nearest original sample
index and clamps it to that range. Thus even a downscaled source with skipped
texels cannot acquire previously unsampled texels when enlarged. UV gradients for
LOD use the original logical-pixel steps, using `textureGrad`/`SampleGrad`, so
magnification cannot select a different mip level or derive LOD from quantization
jumps. Other draws retain their normal native-resolution derivatives.

The restriction is enabled only when either scene dimension exceeds the logical
size. At 1:1 it is bypassed completely, preserving the coordinate-correct hardware
interpolation, including its floating-point behaviour at exact texel boundaries.
GL, GLES, SDL_GPU and D3D11 use the same sample calculation and 1:1 bypass. Overlay
uniforms explicitly disable it; private draws have a zero sample count by default.

The final sample calculation reduced the logged menu's out-of-cell count to 0/59.
OpenGL captures at 640x480 matched a coordinate-conversion-only reference in all
44,800 pixels of the static menu region x=[30,190), y=[170,450). The old output
changed because it interpolated at half-integer logical positions; it was not the
D3D6 reference.

## Validation

A focused standalone test checks the original sample footprint at 1x, 1.25x,
2.25x, 3x and 4x, including mirrored/minified UVs, fractional rectangles, malformed
topology and 3D/depth/filter exclusions. After configuring the default build:

```sh
c++ -std=c++17 -Iminiwin/src/d3d -Iminiwin/src/internal \
  -Iminiwin/include -Iminiwin/include/miniwin -Ibuild/_deps/sdl3-src/include \
  miniwin/tests/pointquads.cpp -o /tmp/miniwin-pointquads
/tmp/miniwin-pointquads
```

The SDL_GPU fragment source changed, so its generated header and derived shader
sources must be regenerated by the **SDL_GPU shaders** workflow. Extending the
uniform buffer after its existing first 32 bytes keeps an old generated shader
loadable, but the sampling restriction is absent until regeneration. Runtime GLSL
provides local visual validation without shadercross. D3D11's HLSL compiles with FXC
at build time.

References: [NVIDIA's contemporary Direct3D texel/pixel mapping note](https://developer.download.nvidia.com/assets/gamedev/docs/D3D_Texel_to_Pixel_Mapping.pdf),
[Microsoft's D3D9/D3D10 coordinate comparison](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-coordinates).
