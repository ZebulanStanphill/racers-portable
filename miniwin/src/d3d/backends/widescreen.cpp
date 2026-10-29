// [library:3d] Shared presentation policy and submission-time UI transform.
#include "renderbackend.h"

#include <miniwin/miniwinapp.h>
#include <miniwin/widescreen.h>
#include <vector>

// On Windows and Xbox the game is two modules (LEGORacers.exe and GolDP.dll) that each
// statically link their own copy of miniwin, so a plain static here would be two
// variables: the exe sets the preference and the world scope while the DLL's renderer
// reads its own, unset copy. State both modules need lives in one block published
// through SDL's global properties (SDL itself is a single shared library). It is
// allocated once and intentionally never freed.
struct SharedState {
	SDL_AtomicInt widescreen;
	// Draw phase, captured per submitted draw: UI (default) or race world.
	SDL_AtomicInt world;
	// The backend that last presented, i.e. the one the player sees. The exe's own
	// MiniwinBackend_Get() may be a different (intro video) backend.
	void* backend;
};

static SharedState* g_shared;

static SharedState* Shared()
{
	if (!g_shared) {
		SDL_PropertiesID props = SDL_GetGlobalProperties();
		SDL_LockProperties(props);
		auto* shared = (SharedState*) SDL_GetPointerProperty(props, "racers.miniwin.widescreen", nullptr);
		if (!shared) {
			shared = (SharedState*) SDL_calloc(1, sizeof(SharedState));
			SDL_SetPointerProperty(props, "racers.miniwin.widescreen", shared);
		}
		SDL_UnlockProperties(props);
		g_shared = shared;
	}
	return g_shared;
}

// The backend the player sees, else this module's own.
static MiniwinRenderBackend* ScreenBackend()
{
	auto* backend = (MiniwinRenderBackend*) SDL_GetAtomicPointer(&Shared()->backend);
	return backend ? backend : MiniwinBackend_Get();
}

MiniwinRenderBackend::~MiniwinRenderBackend()
{
	SDL_CompareAndSwapAtomicPointer(&Shared()->backend, this, nullptr);
}

// A world draw or clear happened since the last present, and the side panels may
// still hold wide-world pixels that a UI-only frame must not leave behind.
static bool g_worldFrame;
static bool g_barsDirty;
static constexpr float c_uiAspect = 4.f / 3.f;

static bool PrefPath(char* p_path, size_t p_size)
{
	char base[1024];
	MiniwinGetUserDataPath(base, sizeof(base));
	if (!base[0]) {
		return false;
	}
	SDL_snprintf(p_path, p_size, "%swidescreen", base);
	return true;
}

void MiniwinWidescreen_Load()
{
	char path[1024];
	if (PrefPath(path, sizeof(path))) {
		void* data = SDL_LoadFile(path, nullptr);
		if (data) {
			SDL_SetAtomicInt(&Shared()->widescreen, SDL_strcmp((const char*) data, "on") == 0);
			SDL_free(data);
		}
	}
}

void MiniwinWidescreen_Set(bool p_enabled)
{
	SDL_SetAtomicInt(&Shared()->widescreen, p_enabled);
	char path[1024];
	if (PrefPath(path, sizeof(path))) {
		const char* value = p_enabled ? "on" : "off";
		SDL_SaveFile(path, value, SDL_strlen(value));
	}
}

bool MiniwinWidescreen_Get()
{
	return SDL_GetAtomicInt(&Shared()->widescreen) != 0;
}

bool MiniwinRenderBackend::WidescreenActive() const
{
	return SDL_GetAtomicInt(&Shared()->widescreen) && m_drawableH > 0 &&
		   (float) m_drawableW / (float) m_drawableH > c_uiAspect;
}

bool MiniwinWidescreen_Active()
{
	auto* backend = ScreenBackend();
	return backend && backend->WidescreenActive();
}

float MiniwinWidescreen_RaceAspect(bool p_splitScreen)
{
	auto* backend = ScreenBackend();
	float aspect =
		MiniwinWidescreen_Active() ? (float) backend->m_drawableW / (float) backend->m_drawableH : c_uiAspect;
	return p_splitScreen ? 2.f * aspect : aspect;
}

MiniwinWorldScope::MiniwinWorldScope()
{
	m_previous = SDL_SetAtomicInt(&Shared()->world, 1) != 0;
}

MiniwinWorldScope::~MiniwinWorldScope()
{
	End();
}

void MiniwinWorldScope::End()
{
	if (!m_ended) {
		SDL_SetAtomicInt(&Shared()->world, m_previous);
		m_ended = true;
	}
}

bool MiniwinRenderBackend::UpdatePresentation(int p_dw, int p_dh)
{
	m_drawableW = p_dw;
	m_drawableH = p_dh;
	int w = p_dw;
	int h = p_dh;
	if (!m_videoPresentation && !WidescreenActive() && MiniwinGetScaleMode() == MINIWIN_SCALE_LETTERBOX) {
		h = (int) ((float) p_dw * (float) m_height / (float) m_width);
		if (h > p_dh) {
			h = p_dh;
			w = (int) ((float) p_dh * (float) m_width / (float) m_height);
		}
	}
	int x = (p_dw - w) / 2;
	int y = (p_dh - h) / 2;
	bool changed = x != m_vpX || y != m_vpY || w != m_vpW || h != m_vpH;
	m_vpX = x;
	m_vpY = y;
	m_vpW = w;
	m_vpH = h;
	return changed;
}

void MiniwinRenderBackend::SceneSize(int& p_width, int& p_height) const
{
	bool original = MiniwinGetRenderResolution() == MINIWIN_RESOLUTION_ORIGINAL;
	p_width = original ? m_width : m_drawableW;
	p_height = original ? m_height : m_drawableH;
	if (original && WidescreenActive()) {
		// An even difference from 640 keeps the 1:1 centred UI on whole scene pixels.
		float wide = (float) m_height * (float) m_drawableW / (float) m_drawableH;
		p_width = m_width + 2 * (int) SDL_lroundf((wide - (float) m_width) * .5f);
	}
}

float MiniwinRenderBackend::UIWidthFraction() const
{
	if (!WidescreenActive()) {
		return 1.f;
	}
	if (MiniwinGetRenderResolution() == MINIWIN_RESOLUTION_ORIGINAL) {
		int width;
		int height;
		SceneSize(width, height);
		return (float) m_width / (float) width;
	}
	return (float) m_width / (float) m_height * (float) m_drawableH / (float) m_drawableW;
}

void MiniwinRenderBackend::PresentScene()
{
	SDL_SetAtomicPointer(&Shared()->backend, this);
	Present();
	g_barsDirty = g_barsDirty || (g_worldFrame && WidescreenActive());
	g_worldFrame = false;
}

// Black panels over the side regions, in the wide logical space.
void MiniwinRenderBackend::DrawSideBars()
{
	float width = (float) m_width;
	float inset = (1.f - UIWidthFraction()) * width * .5f;
	D3DTLVERTEX vertices[8] = {};
	for (int i = 0; i < 8; i++) {
		vertices[i].sx = i < 4 ? (i & 1 ? inset : 0.f) : (i & 1 ? width : width - inset);
		vertices[i].sy = i & 2 ? (float) m_height : 0.f;
		vertices[i].rhw = 1.f;
		vertices[i].color = 0xff000000;
	}
	MiniwinRasterState state;
	state.zEnable = state.zWrite = false;
	state.colorOp = state.alphaOp = MiniwinTextureOp::Diffuse;
	const Uint16 indices[] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
	DrawTriangles(state, vertices, 8, indices, 12);
	g_barsDirty = false;
}

// A UI draw in a frame that has not drawn the world yet: wipe the previous wide frame.
void MiniwinRenderBackend::ClearStaleSideBars()
{
	if (g_barsDirty && !g_worldFrame) {
		DrawSideBars();
	}
}

void MiniwinRenderBackend::SubmitTriangles(
	const MiniwinRasterState& p_state,
	const D3DTLVERTEX* p_vertices,
	Uint32 p_vertexCount,
	const Uint16* p_indices,
	Uint32 p_indexCount
)
{
	bool world = SDL_GetAtomicInt(&Shared()->world) != 0;
	if (world || !WidescreenActive()) {
		g_worldFrame = g_worldFrame || world;
		DrawTriangles(p_state, p_vertices, p_vertexCount, p_indices, p_indexCount);
		return;
	}
	ClearStaleSideBars();
	// SDL_GPU copies these into its staging list now. Replay never consults the world flag
	// or the preference, and caller-owned vertex buffers remain untouched.
	static std::vector<D3DTLVERTEX> vertices;
	vertices.assign(p_vertices, p_vertices + p_vertexCount);
	float scale = UIWidthFraction();
	float offset = (1.f - scale) * (float) m_width * .5f;
	for (auto& vertex : vertices) {
		vertex.sx = offset + vertex.sx * scale;
	}
	MiniwinRasterState state = p_state;
	int width;
	int height;
	SceneSize(width, height);
	// Point-quad UV steps/counts stay in the original logical lattice. Only its
	// magnification gate changes: a wide original-resolution target still gives
	// the centred UI 640x480 samples (allow one pixel for rounded scene width).
	if ((float) width * scale <= (float) m_width + 1.f && height <= m_height) {
		state.pointCount[0] = state.pointCount[1] = 0.f;
	}
	DrawTriangles(state, vertices.data(), p_vertexCount, p_indices, p_indexCount);
}

void MiniwinRenderBackend::ClearScene(
	const SDL_Rect* p_rect,
	float p_r,
	float p_g,
	float p_b,
	bool p_color,
	bool p_depth
)
{
	bool world = SDL_GetAtomicInt(&Shared()->world) != 0;
	if (world || !WidescreenActive()) {
		g_worldFrame = g_worldFrame || world;
		Clear(p_rect, p_r, p_g, p_b, p_color, p_depth);
		return;
	}
	SDL_Rect rect = p_rect ? *p_rect : SDL_Rect{0, 0, m_width, m_height};
	bool full = rect.x == 0 && rect.y == 0 && rect.w == m_width && rect.h == m_height;
	if (full) {
		// Erase the previous race world's sides when returning to menus/loading/video.
		Clear(nullptr, 0.f, 0.f, 0.f, p_color, p_depth);
		g_barsDirty = g_barsDirty && !p_color;
		if (!p_color || (p_r == 0.f && p_g == 0.f && p_b == 0.f)) {
			return;
		}
	}
	else {
		ClearStaleSideBars();
	}
	float scale = UIWidthFraction();
	float offset = (1.f - scale) * (float) m_width * .5f;
	int right = (int) SDL_lroundf(offset + (float) (rect.x + rect.w) * scale);
	rect.x = (int) SDL_lroundf(offset + (float) rect.x * scale);
	rect.w = right - rect.x;
	Clear(&rect, p_r, p_g, p_b, p_color, p_depth);
}

bool MiniwinWidescreen_MapUI(float p_nx, float p_ny, int p_width, int p_height, long* p_x, long* p_y)
{
	auto* backend = ScreenBackend();
	float x = p_nx * (float) p_width;
	float y = p_ny * (float) p_height;
	if (backend && backend->m_vpW > 0 && backend->m_vpH > 0) {
		float scale = backend->UIWidthFraction();
		float width = (float) backend->m_vpW * scale;
		float left = (float) backend->m_vpX + ((float) backend->m_vpW - width) * .5f;
		x = (p_nx * (float) backend->m_drawableW - left) * (float) p_width / width;
		y = (p_ny * (float) backend->m_drawableH - (float) backend->m_vpY) * (float) p_height / (float) backend->m_vpH;
	}
	*p_x = (long) SDL_floorf(x);
	*p_y = (long) SDL_floorf(y);
	return x >= 0.f && x < (float) p_width && y >= 0.f && y < (float) p_height;
}
