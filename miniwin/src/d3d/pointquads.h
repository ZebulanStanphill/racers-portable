#pragma once

#include "renderbackend.h"

#include <cmath>

// Preserve the original pixel-sample lattice only for an affine, axis-aligned 2D
// blit: two triangles covering a rectangle, point filtering, no depth test/write,
// sz=0 and rhw=1 at every corner, and axis-aligned nonconstant UVs. Perspective
// geometry, depth-bearing geometry, rotated sprites and linear filtering bypass it.
inline void MiniwinSetPointQuadSampling(
	MiniwinRasterState& p_state,
	const D3DTLVERTEX* p_vertices,
	Uint32 p_vertexCount,
	const Uint16* p_indices,
	Uint32 p_indexCount,
	int p_width,
	int p_height
)
{
	if (!p_state.textured || p_state.textureLinear || p_state.zEnable || p_state.zWrite || p_vertexCount != 4 ||
		!p_indices || p_indexCount != 6) {
		return;
	}

	float left = p_vertices[0].sx, right = left;
	float top = p_vertices[0].sy, bottom = top;
	for (Uint32 i = 0; i < 4; i++) {
		const D3DTLVERTEX& v = p_vertices[i];
		if (v.sz != 0.0f || v.rhw != 1.0f) {
			return;
		}
		left = SDL_min(left, v.sx);
		right = SDL_max(right, v.sx);
		top = SDL_min(top, v.sy);
		bottom = SDL_max(bottom, v.sy);
	}
	if (!(right > left && bottom > top)) {
		return;
	}

	const D3DTLVERTEX* corners[4] = {};
	Uint32 cornerIds[4];
	for (Uint32 i = 0; i < 4; i++) {
		const D3DTLVERTEX& v = p_vertices[i];
		if ((v.sx != left && v.sx != right) || (v.sy != top && v.sy != bottom)) {
			return;
		}
		Uint32 corner = (v.sx == right ? 1 : 0) | (v.sy == bottom ? 2 : 0);
		if (corners[corner]) {
			return;
		}
		corners[corner] = &v;
		cornerIds[i] = corner;
	}

	// The shared edge must be a diagonal: otherwise six indices can describe an
	// overlapping pair or a partial rectangle despite using all four corners.
	Uint32 triangles[2] = {};
	for (Uint32 i = 0; i < 6; i++) {
		if (p_indices[i] >= 4) {
			return;
		}
		Uint32 bit = 1u << cornerIds[p_indices[i]];
		if (triangles[i / 3] & bit) {
			return;
		}
		triangles[i / 3] |= bit;
	}
	Uint32 shared = triangles[0] & triangles[1];
	if ((triangles[0] | triangles[1]) != 15 || (shared != 9 && shared != 6)) {
		return;
	}

	if (corners[0]->tu != corners[2]->tu || corners[1]->tu != corners[3]->tu || corners[0]->tv != corners[1]->tv ||
		corners[2]->tv != corners[3]->tv) {
		return;
	}
	float du = (corners[1]->tu - corners[0]->tu) / (right - left);
	float dv = (corners[2]->tv - corners[0]->tv) / (bottom - top);
	if (!std::isfinite(du) || !std::isfinite(dv) || du == 0.0f || dv == 0.0f) {
		return;
	}

	// D3D6 covers integer centres inside [left,right) x [top,bottom), clipped
	// to the logical render target. Fractional rectangles and flipped UVs work too.
	float firstX = SDL_max(std::ceil(left), 0.0f);
	float firstY = SDL_max(std::ceil(top), 0.0f);
	float endX = SDL_min(std::ceil(right), (float) p_width);
	float endY = SDL_min(std::ceil(bottom), (float) p_height);
	if (firstX >= endX || firstY >= endY) {
		// A subpixel blit with no original samples must not appear on magnification.
		p_state.pointCount[0] = p_state.pointCount[1] = -1.0f;
		return;
	}
	p_state.pointOriginStep[0] = corners[0]->tu + (firstX - left) * du;
	p_state.pointOriginStep[1] = corners[0]->tv + (firstY - top) * dv;
	p_state.pointOriginStep[2] = du;
	p_state.pointOriginStep[3] = dv;
	p_state.pointCount[0] = endX - firstX;
	p_state.pointCount[1] = endY - firstY;
}
