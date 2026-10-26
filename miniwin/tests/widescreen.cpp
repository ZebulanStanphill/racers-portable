// Standalone shared-policy test. See backends/WIDESCREEN.md for the build command.
#include "renderbackend.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <miniwin/miniwinapp.h>
#include <miniwin/widescreen.h>
#include <vector>

static MiniwinScaleMode g_scale = MINIWIN_SCALE_LETTERBOX;
static MiniwinRenderResolution g_resolution = MINIWIN_RESOLUTION_NATIVE;
MiniwinScaleMode MiniwinGetScaleMode()
{
	return g_scale;
}
MiniwinRenderResolution MiniwinGetRenderResolution()
{
	return g_resolution;
}
void MiniwinGetUserDataPath(char* p_path, size_t)
{
	p_path[0] = '\0';
}

class CaptureBackend : public MiniwinRenderBackend {
public:
	struct ClearCall {
		bool full;
		SDL_Rect rect;
		float r;
		bool color;
		bool depth;
	};
	std::vector<D3DTLVERTEX> vertices;
	std::vector<Uint32> draws; // vertex count of every submitted draw
	std::vector<ClearCall> clears;
	MiniwinRasterState state;
	bool Init(SDL_Window*) override { return true; }
	void Resize(int, int) override {}
	Uint32 CreateTexture(int, int, const void*, bool) override { return 0; }
	void UpdateTexture(Uint32, int, int, const void*, bool) override {}
	void DestroyTexture(Uint32) override {}
	void BeginScene() override {}
	void EndScene() override {}
	void Present() override {}
	SDL_Surface* ReadBackbuffer() override { return nullptr; }
	void DrawOverlay(const D3DTLVERTEX*, Uint32, const Uint16*, Uint32) override {}
	void Clear(const SDL_Rect* p_rect, float p_r, float, float, bool p_color, bool p_depth) override
	{
		clears.push_back({!p_rect, p_rect ? *p_rect : SDL_Rect{}, p_r, p_color, p_depth});
	}
	void DrawTriangles(
		const MiniwinRasterState& p_state,
		const D3DTLVERTEX* p_vertices,
		Uint32 p_count,
		const Uint16*,
		Uint32
	) override
	{
		state = p_state;
		vertices.assign(p_vertices, p_vertices + p_count);
		draws.push_back(p_count);
	}
};

static CaptureBackend g_backend;
MiniwinRenderBackend* MiniwinBackend_Get()
{
	return &g_backend;
}

static bool Near(float p_a, float p_b)
{
	return std::fabs(p_a - p_b) < .001f;
}

int main()
{
	auto& b = g_backend;
	D3DTLVERTEX vertices[3] = {};
	vertices[0].sx = .5f; // Already converted from D3D6 integer centres.
	vertices[1].sx = 640.5f;
	vertices[2].sx = 320.5f;
	vertices[0].sy = vertices[1].sy = vertices[2].sy = 100.5f;
	MiniwinRasterState state;
	state.pointOriginStep[0] = .123f;
	state.pointCount[0] = 7.f;
	state.pointCount[1] = 9.f;

	// On/off are bit-identical at 4:3; tall windows retain the old presentation.
	for (bool enabled : {false, true}) {
		MiniwinWidescreen_Set(enabled);
		b.UpdatePresentation(640, 480);
		b.SubmitTriangles(state, vertices, 3, nullptr, 0);
		assert(std::memcmp(vertices, b.vertices.data(), sizeof(vertices)) == 0);
		assert(!b.WidescreenActive());
	}
	b.UpdatePresentation(600, 800);
	assert(!b.WidescreenActive() && b.m_vpW == 600 && b.m_vpH == 450);

	b.UpdatePresentation(1280, 720);
	assert(b.WidescreenActive() && b.m_vpX == 0 && b.m_vpW == 1280);
	assert(Near(MiniwinWidescreen_RaceAspect(false), 16.f / 9.f));
	assert(Near(MiniwinWidescreen_RaceAspect(true), 32.f / 9.f));
	b.SubmitTriangles(state, vertices, 3, nullptr, 0);
	assert(Near(b.vertices[0].sx, 80.375f));
	assert(Near(b.vertices[1].sx, 560.375f));
	assert(b.vertices[0].sy == 100.5f && vertices[0].sx == .5f);
	assert(b.state.pointCount[0] == 7.f && b.state.pointOriginStep[0] == .123f);

	// The recorded UI draw keeps its coordinates after the phase changes.
	auto uiVertices = b.vertices;
	{
		MiniwinWorldScope world;
		b.SubmitTriangles(state, vertices, 3, nullptr, 0);
		assert(std::memcmp(vertices, b.vertices.data(), sizeof(vertices)) == 0);
		SDL_Rect half = {0, 0, 640, 240};
		b.ClearScene(&half, 1.f, 0.f, 0.f, true, true);
		assert(b.clears.back().rect.w == 640 && b.clears.back().rect.h == 240);
		world.End();
		world.End();
	}
	b.SubmitTriangles(state, vertices, 3, nullptr, 0);
	assert(std::memcmp(uiVertices.data(), b.vertices.data(), sizeof(vertices)) == 0);

	b.clears.clear();
	b.ClearScene(nullptr, 1.f, 0.f, 0.f, true, true);
	assert(b.clears.size() == 2 && b.clears[0].full && b.clears[0].r == 0.f);
	assert(b.clears[1].rect.x == 80 && b.clears[1].rect.w == 480);
	SDL_Rect partial = {0, 240, 320, 240};
	b.ClearScene(&partial, 1.f, 0.f, 0.f, true, false);
	assert(b.clears.back().rect.x == 80 && b.clears.back().rect.w == 240 && b.clears.back().rect.y == 240);

	// Scopes nest: leaving an inner scope restores the enclosing phase.
	{
		MiniwinWorldScope outer;
		{
			MiniwinWorldScope inner;
		}
		b.SubmitTriangles(state, vertices, 3, nullptr, 0);
		assert(std::memcmp(vertices, b.vertices.data(), sizeof(vertices)) == 0);
	}

	// A race frame's own UI (HUD) never covers the world; the next frame that has no
	// world draws wipes the side panels once before its first UI draw.
	b.PresentScene();
	b.draws.clear();
	{
		MiniwinWorldScope world;
		b.SubmitTriangles(state, vertices, 3, nullptr, 0);
		world.End();
		b.SubmitTriangles(state, vertices, 3, nullptr, 0);
	}
	assert(b.draws.size() == 2 && b.draws[0] == 3 && b.draws[1] == 3);
	b.PresentScene();
	b.draws.clear();
	b.SubmitTriangles(state, vertices, 3, nullptr, 0);
	b.SubmitTriangles(state, vertices, 3, nullptr, 0);
	assert(b.draws.size() == 3 && b.draws[0] == 8 && b.draws[1] == 3 && b.draws[2] == 3);
	b.PresentScene();
	b.draws.clear();
	b.SubmitTriangles(state, vertices, 3, nullptr, 0);
	assert(b.draws.size() == 1);
	// A full colour clear already erased the panels, so no extra bars follow it.
	{
		MiniwinWorldScope world;
		b.SubmitTriangles(state, vertices, 3, nullptr, 0);
	}
	b.PresentScene();
	b.draws.clear();
	b.ClearScene(nullptr, 0.f, 0.f, 0.f, true, true);
	b.SubmitTriangles(state, vertices, 3, nullptr, 0);
	assert(b.draws.size() == 1 && b.draws[0] == 3);

	long x;
	long y;
	assert(MiniwinWidescreen_MapUI(.5f, .5f, 640, 480, &x, &y) && x == 320 && y == 240);
	assert(!MiniwinWidescreen_MapUI(.1f, .5f, 640, 480, &x, &y) && x < 0);
	assert(!MiniwinWidescreen_MapUI(.9f, .5f, 640, 480, &x, &y) && x >= 640);

	g_resolution = MINIWIN_RESOLUTION_ORIGINAL;
	int w;
	int h;
	b.SceneSize(w, h);
	assert(w == 854 && h == 480); // 853.33 rounded so the 640-wide UI lands on a whole pixel.
	assert(Near(b.UIWidthFraction(), 640.f / 854.f));
	b.SubmitTriangles(state, vertices, 3, nullptr, 0);
	assert(b.state.pointCount[0] == 0.f); // Centred UI is still 1:1, not 854/640.
	{
		MiniwinWorldScope world;
		b.SubmitTriangles(state, vertices, 3, nullptr, 0);
		assert(b.state.pointCount[0] == 7.f); // The world uses the whole 854-wide target.
	}
	g_scale = MINIWIN_SCALE_STRETCH;
	b.UpdatePresentation(1920, 1080);
	assert(b.m_vpW == 1920 && b.m_vpX == 0 && Near(b.UIWidthFraction(), .75f));
	MiniwinWidescreen_Set(false);
	b.UpdatePresentation(1920, 1080);
	assert(b.UIWidthFraction() == 1.f);
	MiniwinWidescreen_Set(true);
	b.UpdatePresentation(640, 480);
	assert(!b.WidescreenActive());
}
