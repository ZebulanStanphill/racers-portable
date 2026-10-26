// [library:3d] Native Direct3D 11 backend. Keep SDK types in the isolated native
// target; this translation unit sees miniwin's compatibility headers instead.
#include "backends.h"
#include "miniwin.h"
#include "native.h"
#include "overlay.h"
#include "renderbackend.h"

#include <stddef.h>

namespace
{

static_assert(sizeof(D3DTLVERTEX) == sizeof(MiniwinD3D11Vertex), "D3D11 vertex stride");
static_assert(offsetof(D3DTLVERTEX, sx) == 0, "D3D11 position offset");
static_assert(offsetof(D3DTLVERTEX, color) == 16, "D3D11 diffuse offset");
static_assert(offsetof(D3DTLVERTEX, specular) == 20, "D3D11 specular offset");
static_assert(offsetof(D3DTLVERTEX, tu) == 24, "D3D11 UV offset");

class MiniwinD3D11Backend : public MiniwinRenderBackend {
public:
	bool Init(SDL_Window* p_window) override
	{
		m_window = p_window;
		return m_native.Init(p_window) && PrepareFrame();
	}

	void Resize(int p_width, int p_height) override
	{
		m_width = p_width;
		m_height = p_height;
		m_frameReady = false;
		PrepareFrame();
	}

	Uint32 CreateTexture(int p_width, int p_height, const void* p_rgba, bool p_mipmaps) override
	{
		return m_native.CreateTexture(p_width, p_height, p_rgba, p_mipmaps);
	}

	void UpdateTexture(Uint32 p_id, int p_width, int p_height, const void* p_rgba, bool p_mipmaps) override
	{
		m_native.UpdateTexture(p_id, p_width, p_height, p_rgba, p_mipmaps);
	}

	void DestroyTexture(Uint32 p_id) override { m_native.DestroyTexture(p_id); }
	void BeginScene() override { PrepareFrame(); }
	void EndScene() override {}

	void Clear(const SDL_Rect* p_rect, float p_r, float p_g, float p_b, bool p_color, bool p_depth) override
	{
		if ((!p_color && !p_depth) || !PrepareFrame()) {
			return;
		}
		if (!p_rect) {
			m_native.Clear(p_r, p_g, p_b, p_color, p_depth);
			return;
		}
		if (p_rect->w <= 0 || p_rect->h <= 0) {
			return;
		}
		// D3D11 Clear*View ignores scissor rectangles. Draw a depth=1 quad for a
		// rectangular clear, scaling logical coordinates through the scene viewport.
		Uint32 color = 0xff000000u | ((Uint32) SDL_lroundf(SDL_clamp(p_r, 0.f, 1.f) * 255.f) << 16) |
					   ((Uint32) SDL_lroundf(SDL_clamp(p_g, 0.f, 1.f) * 255.f) << 8) |
					   (Uint32) SDL_lroundf(SDL_clamp(p_b, 0.f, 1.f) * 255.f);
		D3DTLVERTEX vertices[4] = {};
		for (int i = 0; i < 4; i++) {
			vertices[i].sx = (float) p_rect->x + (i & 1 ? (float) p_rect->w : 0.f);
			vertices[i].sy = (float) p_rect->y + (i & 2 ? (float) p_rect->h : 0.f);
			vertices[i].sz = 1.f;
			vertices[i].rhw = 1.f;
			vertices[i].color = color;
		}
		const Uint16 indices[] = {0, 1, 2, 2, 1, 3};
		MiniwinD3D11State state;
		state.zEnable = p_depth;
		state.zWrite = p_depth;
		state.zFunc = D3DCMP_ALWAYS;
		state.colorWrite = p_color;
		m_native.Draw(state, vertices, 4, indices, 6, m_width, m_height);
	}

	void DrawTriangles(
		const MiniwinRasterState& p_state,
		const D3DTLVERTEX* p_vertices,
		Uint32 p_vertexCount,
		const Uint16* p_indices,
		Uint32 p_indexCount
	) override
	{
		if (!PrepareFrame()) {
			return;
		}
		MiniwinD3D11State state;
		state.zEnable = p_state.zEnable;
		state.zWrite = p_state.zWrite;
		state.zFunc = p_state.zFunc;
		state.alphaBlend = p_state.alphaBlend;
		state.srcBlend = p_state.srcBlend;
		state.destBlend = p_state.destBlend;
		state.cullMode = p_state.cullMode;
		state.colorOp = (int) p_state.colorOp;
		state.alphaOp = (int) p_state.alphaOp;
		state.specular = p_state.specular;
		state.alphaFunc = p_state.alphaTest ? (int) p_state.alphaFunc : 0;
		state.alphaRef = p_state.alphaRef;
		state.colorKey = p_state.colorKeyTest;
		state.textureId = p_state.textured ? p_state.textureId : 0;
		state.linear = p_state.textureLinear;
		state.wrap = p_state.textureWrap;
		m_native.Draw(state, p_vertices, p_vertexCount, p_indices, p_indexCount, m_width, m_height);
	}

	void Present() override
	{
		MiniwinPhaseScope phase(MINIWIN_PHASE_PRESENT);
		m_frameCounter++;
		if (PrepareFrame() && m_native.Composite({m_vpX, m_vpY, m_vpW, m_vpH})) {
			m_overlayActive = true;
			MiniwinOverlay_Emit(this);
			m_overlayActive = false;
			m_native.Present();
		}
		MiniwinBackend_HandleFrameDump(this, m_frameCounter);
		m_frameReady = false;
	}

	void DrawOverlay(
		const D3DTLVERTEX* p_vertices,
		Uint32 p_vertexCount,
		const Uint16* p_indices,
		Uint32 p_indexCount
	) override
	{
		if (!m_overlayActive) {
			return;
		}
		MiniwinD3D11State state;
		state.zEnable = false;
		state.zWrite = false;
		state.alphaBlend = true;
		m_native.Draw(state, p_vertices, p_vertexCount, p_indices, p_indexCount, m_drawableW, m_drawableH, true);
	}

	SDL_Surface* ReadBackbuffer() override { return m_native.ReadBackbuffer(); }

private:
	bool PrepareFrame()
	{
		if (m_frameReady) {
			return true;
		}
		int dw = 0;
		int dh = 0;
		MiniwinApp_RunOnMainThread([&]() { SDL_GetWindowSizeInPixels(m_window, &dw, &dh); });
		if (dw <= 0 || dh <= 0 || m_width <= 0 || m_height <= 0 || !m_native.ResizeSwapchain(dw, dh)) {
			return false;
		}
		m_drawableW = dw;
		m_drawableH = dh;
		m_vpW = dw;
		m_vpH = dh;
		if (MiniwinGetScaleMode() == MINIWIN_SCALE_LETTERBOX) {
			m_vpH = (int) ((float) dw * (float) m_height / (float) m_width);
			if (m_vpH > dh) {
				m_vpH = dh;
				m_vpW = (int) ((float) dh * (float) m_width / (float) m_height);
			}
		}
		m_vpX = (dw - m_vpW) / 2;
		m_vpY = (dh - m_vpH) / 2;
		bool original = MiniwinGetRenderResolution() == MINIWIN_RESOLUTION_ORIGINAL;
		m_frameReady = m_native.ResizeScene(original ? m_width : dw, original ? m_height : dh);
		return m_frameReady;
	}

	MiniwinD3D11Native m_native;
	SDL_Window* m_window = nullptr;
	bool m_frameReady = false;
	bool m_overlayActive = false;
	Uint64 m_frameCounter = 0;
};

} // namespace

bool MiniwinD3D11_Available()
{
	return MiniwinD3D11Native::Available();
}

Uint32 MiniwinD3D11_PrepareWindowFlags()
{
	return 0;
}

MiniwinRenderBackend* MiniwinD3D11_Create(SDL_Window* p_window, int p_width, int p_height)
{
	MiniwinD3D11Backend* backend = new MiniwinD3D11Backend();
	backend->m_width = p_width;
	backend->m_height = p_height;
	if (!backend->Init(p_window)) {
		delete backend;
		return nullptr;
	}
	return backend;
}
