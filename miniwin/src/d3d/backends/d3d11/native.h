#pragma once

// SDK-free boundary: the renderer uses miniwin's D3D6/Win32 types, while native.cpp
// must see only real Windows SDK headers. Enum fields use the D3D6 numeric values.
#include <SDL3/SDL.h>

struct MiniwinD3D11State {
	bool zEnable = true;
	bool zWrite = true;
	int zFunc = 4;
	bool alphaBlend = false;
	int srcBlend = 5;
	int destBlend = 6;
	int cullMode = 1;
	bool colorWrite = true;
	int colorOp = 2;
	int alphaOp = 2;
	int specular = 0;
	int alphaFunc = 0;
	float alphaRef = 0.f;
	int colorKey = 0;
	float pointOriginStep[4] = {};
	float pointCount[2] = {};
	Uint32 textureId = 0;
	bool linear = true;
	bool wrap = false;
};

// D3DTLVERTEX byte layout, checked in renderer.cpp. Draw accepts raw bytes so no
// object is accessed through a different C++ vertex type across this boundary.
struct MiniwinD3D11Vertex {
	float x, y, z, rhw;
	Uint32 color, specular;
	float u, v;
};

class MiniwinD3D11Native {
public:
	MiniwinD3D11Native();
	~MiniwinD3D11Native();
	MiniwinD3D11Native(const MiniwinD3D11Native&) = delete;
	MiniwinD3D11Native& operator=(const MiniwinD3D11Native&) = delete;

	static bool Available();
	bool Init(SDL_Window* p_window);
	bool ResizeSwapchain(int& p_width, int& p_height);
	bool ResizeScene(int p_width, int p_height);
	Uint32 CreateTexture(int p_width, int p_height, const void* p_rgba, bool p_mipmaps);
	void UpdateTexture(Uint32 p_id, int p_width, int p_height, const void* p_rgba, bool p_mipmaps);
	void DestroyTexture(Uint32 p_id);
	void Clear(float p_r, float p_g, float p_b, bool p_color, bool p_depth);
	void Draw(
		const MiniwinD3D11State& p_state,
		const void* p_vertices,
		Uint32 p_vertexCount,
		const Uint16* p_indices,
		Uint32 p_indexCount,
		int p_logicalW,
		int p_logicalH,
		bool p_overlay = false
	);
	bool Composite(const SDL_Rect& p_viewport);
	void Present();
	SDL_Surface* ReadBackbuffer();

private:
	struct Impl;
	Impl* m_impl;
};
