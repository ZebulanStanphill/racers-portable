#include "pointquads.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>

static const Uint16 indices[] = {0, 1, 2, 2, 1, 3};

static MiniwinRasterState Prepare(const D3DTLVERTEX* p_quad, const Uint16* p_indices = indices)
{
	MiniwinRasterState state;
	state.textured = true;
	state.textureLinear = false;
	state.zEnable = false;
	state.zWrite = false;
	MiniwinSetPointQuadSampling(state, p_quad, 4, p_indices, 6, 640, 480);
	return state;
}

static void CheckFootprint(const D3DTLVERTEX* p_quad)
{
	MiniwinRasterState state = Prepare(p_quad);
	assert(state.pointCount[0] > 0.0f);
	// Build an independent reference: texels reached by the original integer-centre
	// rasterizer. Magnified samples must belong to this set (even for minified UVs).
	std::set<std::pair<int, int> > reference;
	float du = (p_quad[2].tu - p_quad[0].tu) / (p_quad[2].sx - p_quad[0].sx);
	float dv = (p_quad[1].tv - p_quad[0].tv) / (p_quad[1].sy - p_quad[0].sy);
	for (int y = (int) std::ceil(p_quad[0].sy); y < p_quad[1].sy; y++) {
		for (int x = (int) std::ceil(p_quad[0].sx); x < p_quad[2].sx; x++) {
			int u = (int) std::floor((p_quad[0].tu + (x - p_quad[0].sx) * du) * 256.0f);
			int v = (int) std::floor((p_quad[0].tv + (y - p_quad[0].sy) * dv) * 256.0f);
			reference.emplace(u, v);
		}
	}
	for (float scale : {1.0f, 1.25f, 2.25f, 3.0f, 4.0f}) {
		for (int py = (int) std::ceil((p_quad[0].sy + 0.5f) * scale - 0.5f); py + 0.5f < (p_quad[1].sy + 0.5f) * scale;
			 py++) {
			for (int px = (int) std::ceil((p_quad[0].sx + 0.5f) * scale - 0.5f);
				 px + 0.5f < (p_quad[2].sx + 0.5f) * scale;
				 px++) {
				float x = (px + 0.5f) / scale - 0.5f;
				float y = (py + 0.5f) / scale - 0.5f;
				float uv[] = {p_quad[0].tu + (x - p_quad[0].sx) * du, p_quad[0].tv + (y - p_quad[0].sy) * dv};
				int texel[2];
				for (int axis = 0; axis < 2; axis++) {
					float origin = state.pointOriginStep[axis], step = state.pointOriginStep[axis + 2];
					float pixel = std::floor((uv[axis] - origin) / step + 0.5f);
					pixel = SDL_max(0.0f, SDL_min(pixel, state.pointCount[axis] - 1.0f));
					float sampled = origin + pixel * step;
					texel[axis] = (int) std::floor(sampled * 256.0f);
					if (scale == 1.0f) {
						assert(texel[axis] == (int) std::floor(uv[axis] * 256.0f));
					}
				}
				assert(reference.count({texel[0], texel[1]}) == 1);
			}
		}
	}
}

int main()
{
	// Actual menu E: atlas x=[8,21), y=[0,32), destination x=[167,180).
	D3DTLVERTEX quad[] = {
		{167, 178, 0, 1, 0xffffffff, 0, 8.25f / 256, 0},
		{167, 210, 0, 1, 0xffffffff, 0, 8.25f / 256, 31.0f / 256},
		{180, 178, 0, 1, 0xffffffff, 0, 21.25f / 256, 0},
		{180, 210, 0, 1, 0xffffffff, 0, 21.25f / 256, 31.0f / 256},
	};
	CheckFootprint(quad);
	D3DTLVERTEX permuted[] = {quad[3], quad[2], quad[1], quad[0]};
	assert(Prepare(permuted).pointCount[0] == 13);
	D3DTLVERTEX clipped[] = {quad[0], quad[1], quad[2], quad[3]};
	for (auto& v : clipped) {
		v.sx -= 170;
		v.sy -= 180;
	}
	MiniwinRasterState clippedState = Prepare(clipped);
	assert(clippedState.pointCount[0] == 10 && clippedState.pointCount[1] == 30);
	assert(clippedState.pointOriginStep[0] == 11.25f / 256);
	for (auto& v : clipped) {
		v.sx -= 100;
	}
	assert(Prepare(clipped).pointCount[0] < 0); // completely outside the logical target
	D3DTLVERTEX tiny[] = {quad[0], quad[1], quad[2], quad[3]};
	for (auto& v : tiny) {
		v.sx = v.sx == 167 ? 10.125f : 10.25f;
	}
	assert(Prepare(tiny).pointCount[0] < 0); // no original sample: suppress at native resolution

	// Fractional geometry, mirrored/minified UVs: skipped original texels must stay skipped.
	for (auto& v : quad) {
		v.sx += 0.25f;
		v.sy += 0.125f;
		v.tu = 1.0f - 2.0f * v.tu;
		v.tv = 1.0f - 2.0f * v.tv;
	}
	CheckFootprint(quad);
	const Uint16 overlapping[] = {0, 1, 2, 0, 1, 2};
	const Uint16 partial[] = {0, 1, 2, 0, 1, 3};
	assert(Prepare(quad, overlapping).pointCount[0] == 0);
	assert(Prepare(quad, partial).pointCount[0] == 0);
	quad[0].sx += 1.0f;
	assert(Prepare(quad).pointCount[0] == 0); // rotated/skewed geometry
	quad[0].sx -= 1.0f;
	quad[0].rhw = 0.5f;
	assert(Prepare(quad).pointCount[0] == 0); // perspective geometry
	quad[0].rhw = 1.0f;
	quad[0].sz = 0.1f;
	assert(Prepare(quad).pointCount[0] == 0); // depth-bearing geometry
	quad[0].sz = 0.0f;
	for (int reject = 0; reject < 4; reject++) {
		MiniwinRasterState state;
		state.textured = reject != 0;
		state.textureLinear = reject == 1;
		state.zEnable = reject == 2;
		state.zWrite = reject == 3;
		MiniwinSetPointQuadSampling(state, quad, 4, indices, 6, 640, 480);
		assert(state.pointCount[0] == 0);
	}
	puts("Point-quad sampling: original samples unchanged; magnified footprints contained; 3D/invalid draws rejected.");
}
