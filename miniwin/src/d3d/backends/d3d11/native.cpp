// [library:3d] Windows SDK half of the native D3D11 backend. This target deliberately
// has no miniwin compatibility include paths or MINIWIN definition.
#include "native.h"

#include "miniwin.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <limits>
#include <unordered_map>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// FXC headers use the SDK's BYTE type: include them after the native SDK headers.
#include "scene_ps.h"
#include "scene_vs.h"

namespace
{

bool Checked(HRESULT p_result, const char* p_operation)
{
	if (FAILED(p_result)) {
		SDL_LogError(LOG_CATEGORY_MINIWIN, "D3D11 %s failed: HRESULT 0x%08lx", p_operation, (unsigned long) p_result);
		return false;
	}
	return true;
}

bool CreateDevice(ComPtr<ID3D11Device>& p_device, ComPtr<ID3D11DeviceContext>& p_context)
{
	const D3D_FEATURE_LEVEL levels[] =
		{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
	D3D_FEATURE_LEVEL chosen = D3D_FEATURE_LEVEL_9_1;
	HRESULT result = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		levels,
		SDL_arraysize(levels),
		D3D11_SDK_VERSION,
		p_device.GetAddressOf(),
		&chosen,
		p_context.GetAddressOf()
	);
	if (result == E_INVALIDARG) {
		Checked(result, "D3D11CreateDevice (retrying without 11.1)");
		p_device.Reset();
		p_context.Reset();
		result = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			levels + 1,
			SDL_arraysize(levels) - 1,
			D3D11_SDK_VERSION,
			p_device.GetAddressOf(),
			&chosen,
			p_context.GetAddressOf()
		);
	}
	if (!Checked(result, "D3D11CreateDevice")) {
		return false;
	}
	if (chosen < D3D_FEATURE_LEVEL_10_0) {
		SDL_LogError(LOG_CATEGORY_MINIWIN, "D3D11 rejects feature level 0x%04x (requires 10.0)", (unsigned) chosen);
		return false;
	}
	SDL_LogInfo(
		LOG_CATEGORY_MINIWIN,
		"D3D11 feature level: %u.%u (0x%04x)",
		((unsigned) chosen >> 12),
		((unsigned) chosen >> 8) & 15,
		(unsigned) chosen
	);
	return true;
}

D3D11_BLEND BlendFactor(int p_factor, bool p_alpha)
{
	// D3D11 forbids COLOR factors in the alpha equation: use their alpha
	// equivalents (and ONE for SRC_ALPHA_SAT's alpha component).
	switch (p_factor) {
	case 1:
		return D3D11_BLEND_ZERO;
	case 2:
		return D3D11_BLEND_ONE;
	case 3:
		return p_alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
	case 4:
		return p_alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
	case 5:
		return D3D11_BLEND_SRC_ALPHA;
	case 6:
		return D3D11_BLEND_INV_SRC_ALPHA;
	case 7:
		return D3D11_BLEND_DEST_ALPHA;
	case 8:
		return D3D11_BLEND_INV_DEST_ALPHA;
	case 9:
		return p_alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
	case 10:
		return p_alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
	case 11:
		return p_alpha ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA_SAT;
	default:
		return D3D11_BLEND_ONE;
	}
}

struct VertexUniforms {
	float viewport[2];
	float pad[2];
};
struct FragmentUniforms {
	Sint32 colorOp;
	Sint32 alphaOp;
	Sint32 specular;
	Sint32 alphaFunc;
	float alphaRef;
	Sint32 colorKey;
	float pad[2];
	float pointOriginStep[4];
	float pointCount[2];
	float pointPad[2];
};
static_assert(sizeof(VertexUniforms) == 16 && sizeof(FragmentUniforms) == 64, "HLSL constant buffer packing");

struct Texture {
	ComPtr<ID3D11Texture2D> resource;
	ComPtr<ID3D11ShaderResourceView> view;
	int width = 0;
	int height = 0;
	bool mipmaps = false;
};
struct Pipeline {
	ComPtr<ID3D11BlendState> blend;
	ComPtr<ID3D11DepthStencilState> depth;
	ComPtr<ID3D11RasterizerState> raster;
};

Uint32 PipelineKey(const MiniwinD3D11State& p_state)
{
	return (Uint32) p_state.alphaBlend | ((Uint32) p_state.srcBlend & 15) << 1 |
		   ((Uint32) p_state.destBlend & 15) << 5 | (Uint32) p_state.zEnable << 9 | (Uint32) p_state.zWrite << 10 |
		   ((Uint32) p_state.zFunc & 15) << 11 | ((Uint32) p_state.cullMode & 3) << 15 |
		   (Uint32) p_state.colorWrite << 17;
}

} // namespace

struct MiniwinD3D11Native::Impl {
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<IDXGISwapChain1> swapchain;
	ComPtr<ID3D11RenderTargetView> swapTarget;
	ComPtr<ID3D11Texture2D> scene;
	ComPtr<ID3D11RenderTargetView> sceneTarget;
	ComPtr<ID3D11ShaderResourceView> sceneView;
	ComPtr<ID3D11DepthStencilView> sceneDepth;
	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3D11InputLayout> layout;
	ComPtr<ID3D11Buffer> vertexUniforms;
	ComPtr<ID3D11Buffer> fragmentUniforms;
	ComPtr<ID3D11Buffer> vertices;
	ComPtr<ID3D11Buffer> indices;
	ComPtr<ID3D11SamplerState> samplers[2][2];
	std::unordered_map<Uint32, Texture> textures;
	std::unordered_map<Uint32, Pipeline> pipelines;
	Texture dummy;
	Uint32 nextId = 1;
	int sceneW = 0, sceneH = 0;
	int drawableW = 0, drawableH = 0;

	bool CreateSwapchain(SDL_Window* p_window);
	bool AcquireSwapTarget();
	bool MakeTexture(Texture& p_texture, int p_width, int p_height, const void* p_rgba, bool p_mipmaps);
	bool DynamicBuffer(ComPtr<ID3D11Buffer>& p_buffer, UINT p_bytes, UINT p_bind);
	bool Upload(ComPtr<ID3D11Buffer>& p_buffer, const void* p_data, UINT p_bytes, UINT p_bind);
	Pipeline* PipelineFor(const MiniwinD3D11State& p_state);
	bool Draw(
		const MiniwinD3D11State& p_state,
		const void* p_vertices,
		Uint32 p_vertexCount,
		const Uint16* p_indices,
		Uint32 p_indexCount,
		int p_logicalW,
		int p_logicalH,
		bool p_overlay,
		ID3D11ShaderResourceView* p_texture = nullptr,
		const SDL_Rect* p_viewport = nullptr
	);
};

MiniwinD3D11Native::MiniwinD3D11Native() : m_impl(new Impl())
{
}

MiniwinD3D11Native::~MiniwinD3D11Native()
{
	if (m_impl->context) {
		m_impl->context->ClearState();
		m_impl->context->Flush();
	}
	delete m_impl;
}

bool MiniwinD3D11Native::Available()
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	return CreateDevice(device, context);
}

bool MiniwinD3D11Native::Impl::CreateSwapchain(SDL_Window* p_window)
{
	// Called synchronously on SDL's main/UI thread. CoreWindow access and DXGI's
	// association with it stay there; the immediate context remains game-thread-only.
	ComPtr<IDXGIDevice> dxgiDevice;
	ComPtr<IDXGIAdapter> adapter;
	ComPtr<IDXGIFactory2> factory;
	if (!Checked(device.As(&dxgiDevice), "QueryInterface IDXGIDevice") ||
		!Checked(dxgiDevice->GetAdapter(adapter.GetAddressOf()), "GetAdapter") ||
		!Checked(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())), "GetParent IDXGIFactory2")) {
		return false;
	}
	DXGI_ADAPTER_DESC adapterDesc = {};
	if (!Checked(adapter->GetDesc(&adapterDesc), "GetDesc adapter")) {
		return false;
	}
	char adapterName[512] = {};
	WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
	SDL_LogInfo(LOG_CATEGORY_MINIWIN, "D3D11 adapter: %s", adapterName);

	int width = 0, height = 0;
	SDL_GetWindowSizeInPixels(p_window, &width, &height);
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = (UINT) SDL_max(width, 1);
	desc.Height = (UINT) SDL_max(height, 1);
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 2;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	SDL_PropertiesID props = SDL_GetWindowProperties(p_window);
#ifdef WINDOWS_STORE
	// SDL3-uwp's SDL_video.h defines this as "SDL.window.winrt.window" and exposes
	// the CoreWindow IInspectable (an IUnknown), rather than an HWND.
	auto window = (IUnknown*) SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WINRT_WINDOW_POINTER, nullptr);
	if (!window) {
		SDL_LogError(LOG_CATEGORY_MINIWIN, "D3D11: SDL window has no CoreWindow property");
		return false;
	}
	return Checked(
		factory->CreateSwapChainForCoreWindow(device.Get(), window, &desc, nullptr, swapchain.GetAddressOf()),
		"CreateSwapChainForCoreWindow"
	);
#else
	auto window = (HWND) SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
	if (!window) {
		SDL_LogError(LOG_CATEGORY_MINIWIN, "D3D11: SDL window has no HWND property");
		return false;
	}
	if (!Checked(
			factory->CreateSwapChainForHwnd(device.Get(), window, &desc, nullptr, nullptr, swapchain.GetAddressOf()),
			"CreateSwapChainForHwnd"
		)) {
		return false;
	}
	return Checked(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");
#endif
}

bool MiniwinD3D11Native::Impl::AcquireSwapTarget()
{
	ComPtr<ID3D11Texture2D> buffer;
	if (!Checked(swapchain->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf())), "GetBuffer") ||
		!Checked(
			device->CreateRenderTargetView(buffer.Get(), nullptr, swapTarget.GetAddressOf()),
			"CreateRenderTargetView swapchain"
		)) {
		return false;
	}
	D3D11_TEXTURE2D_DESC desc = {};
	buffer->GetDesc(&desc);
	drawableW = (int) desc.Width;
	drawableH = (int) desc.Height;
	SDL_LogInfo(LOG_CATEGORY_MINIWIN, "D3D11 swapchain: %dx%d", drawableW, drawableH);
	return true;
}

bool MiniwinD3D11Native::Init(SDL_Window* p_window)
{
	Impl& d = *m_impl;
	if (!CreateDevice(d.device, d.context)) {
		return false;
	}
	struct Request {
		Impl* impl;
		SDL_Window* window;
		bool ok;
	} request = {&d, p_window, false};
	if (!SDL_RunOnMainThread(
			[](void* p_data) {
				auto& r = *(Request*) p_data;
				r.ok = r.impl->CreateSwapchain(r.window);
			},
			&request,
			true
		) ||
		!request.ok || !d.AcquireSwapTarget()) {
		return false;
	}
	if (!Checked(
			d.device->CreateVertexShader(g_d3d11_vs, sizeof(g_d3d11_vs), nullptr, d.vs.GetAddressOf()),
			"CreateVertexShader"
		) ||
		!Checked(
			d.device->CreatePixelShader(g_d3d11_ps, sizeof(g_d3d11_ps), nullptr, d.ps.GetAddressOf()),
			"CreatePixelShader"
		)) {
		return false;
	}
	const D3D11_INPUT_ELEMENT_DESC elements[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	if (!Checked(
			d.device->CreateInputLayout(
				elements,
				SDL_arraysize(elements),
				g_d3d11_vs,
				sizeof(g_d3d11_vs),
				d.layout.GetAddressOf()
			),
			"CreateInputLayout"
		) ||
		!d.DynamicBuffer(d.vertexUniforms, sizeof(VertexUniforms), D3D11_BIND_CONSTANT_BUFFER) ||
		!d.DynamicBuffer(d.fragmentUniforms, sizeof(FragmentUniforms), D3D11_BIND_CONSTANT_BUFFER)) {
		return false;
	}
	for (int linear = 0; linear < 2; linear++) {
		for (int wrap = 0; wrap < 2; wrap++) {
			D3D11_SAMPLER_DESC desc = {};
			desc.Filter = linear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = desc.AddressV = desc.AddressW =
				wrap ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
			desc.MaxAnisotropy = 1;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			if (!Checked(
					d.device->CreateSamplerState(&desc, d.samplers[linear][wrap].GetAddressOf()),
					"CreateSamplerState"
				)) {
				return false;
			}
		}
	}
	const Uint32 white = 0xffffffff;
	return d.MakeTexture(d.dummy, 1, 1, &white, false);
}

bool MiniwinD3D11Native::ResizeSwapchain(int& p_width, int& p_height)
{
	Impl& d = *m_impl;
	if (p_width <= 0 || p_height <= 0) {
		return false;
	}
	if (p_width != d.drawableW || p_height != d.drawableH) {
		d.context->OMSetRenderTargets(0, nullptr, nullptr);
		d.swapTarget.Reset();
		if (!Checked(
				d.swapchain->ResizeBuffers(2, p_width, p_height, DXGI_FORMAT_B8G8R8A8_UNORM, 0),
				"ResizeBuffers"
			)) {
			return false;
		}
	}
	if (!d.swapTarget && !d.AcquireSwapTarget()) {
		return false;
	}
	p_width = d.drawableW;
	p_height = d.drawableH;
	return true;
}

bool MiniwinD3D11Native::ResizeScene(int p_width, int p_height)
{
	Impl& d = *m_impl;
	if (p_width <= 0 || p_height <= 0) {
		return false;
	}
	if (d.scene && d.sceneW == p_width && d.sceneH == p_height) {
		return true;
	}
	// Build into temporaries so a failed allocation never leaves a half-valid target.
	ComPtr<ID3D11Texture2D> color, depth;
	ComPtr<ID3D11RenderTargetView> target;
	ComPtr<ID3D11ShaderResourceView> view;
	ComPtr<ID3D11DepthStencilView> depthView;
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = p_width;
	desc.Height = p_height;
	desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (!Checked(d.device->CreateTexture2D(&desc, nullptr, color.GetAddressOf()), "CreateTexture2D scene") ||
		!Checked(
			d.device->CreateRenderTargetView(color.Get(), nullptr, target.GetAddressOf()),
			"CreateRenderTargetView scene"
		) ||
		!Checked(
			d.device->CreateShaderResourceView(color.Get(), nullptr, view.GetAddressOf()),
			"CreateShaderResourceView scene"
		)) {
		return false;
	}
	desc.Format = DXGI_FORMAT_D32_FLOAT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (!Checked(d.device->CreateTexture2D(&desc, nullptr, depth.GetAddressOf()), "CreateTexture2D depth") ||
		!Checked(
			d.device->CreateDepthStencilView(depth.Get(), nullptr, depthView.GetAddressOf()),
			"CreateDepthStencilView"
		)) {
		return false;
	}
	d.scene = color;
	d.sceneTarget = target;
	d.sceneView = view;
	d.sceneDepth = depthView;
	d.sceneW = p_width;
	d.sceneH = p_height;
	// Persistent targets retain their contents across frames, like SDL_GPU.
	Clear(0.f, 0.f, 0.f, true, true);
	return true;
}

bool MiniwinD3D11Native::Impl::MakeTexture(
	Texture& p_texture,
	int p_width,
	int p_height,
	const void* p_rgba,
	bool p_mipmaps
)
{
	if (p_width <= 0 || p_height <= 0) {
		return false;
	}
	Texture texture;
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = p_width;
	desc.Height = p_height;
	desc.MipLevels = p_mipmaps ? 0 : 1;
	desc.ArraySize = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (p_mipmaps ? D3D11_BIND_RENDER_TARGET : 0);
	desc.MiscFlags = p_mipmaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
	if (!Checked(device->CreateTexture2D(&desc, nullptr, texture.resource.GetAddressOf()), "CreateTexture2D texture") ||
		!Checked(
			device->CreateShaderResourceView(texture.resource.Get(), nullptr, texture.view.GetAddressOf()),
			"CreateShaderResourceView texture"
		)) {
		return false;
	}
	texture.width = p_width;
	texture.height = p_height;
	texture.mipmaps = p_mipmaps;
	if (p_rgba) {
		context->UpdateSubresource(texture.resource.Get(), 0, nullptr, p_rgba, p_width * 4, 0);
		if (p_mipmaps) {
			context->GenerateMips(texture.view.Get());
		}
	}
	p_texture = texture;
	return true;
}

Uint32 MiniwinD3D11Native::CreateTexture(int p_width, int p_height, const void* p_rgba, bool p_mipmaps)
{
	Texture texture;
	if (!m_impl->MakeTexture(texture, p_width, p_height, p_rgba, p_mipmaps)) {
		return 0;
	}
	Uint32 id = m_impl->nextId++;
	m_impl->textures.emplace(id, texture);
	return id;
}

void MiniwinD3D11Native::UpdateTexture(Uint32 p_id, int p_width, int p_height, const void* p_rgba, bool p_mipmaps)
{
	auto found = m_impl->textures.find(p_id);
	if (found == m_impl->textures.end() || !p_rgba) {
		return;
	}
	Texture& texture = found->second;
	if (texture.width != p_width || texture.height != p_height || texture.mipmaps != p_mipmaps) {
		m_impl->MakeTexture(texture, p_width, p_height, p_rgba, p_mipmaps);
		return;
	}
	ID3D11ShaderResourceView* nullView = nullptr;
	m_impl->context->PSSetShaderResources(0, 1, &nullView);
	m_impl->context->UpdateSubresource(texture.resource.Get(), 0, nullptr, p_rgba, p_width * 4, 0);
	if (p_mipmaps) {
		m_impl->context->GenerateMips(texture.view.Get());
	}
}

void MiniwinD3D11Native::DestroyTexture(Uint32 p_id)
{
	ID3D11ShaderResourceView* nullView = nullptr;
	m_impl->context->PSSetShaderResources(0, 1, &nullView);
	m_impl->textures.erase(p_id);
}

bool MiniwinD3D11Native::Impl::DynamicBuffer(ComPtr<ID3D11Buffer>& p_buffer, UINT p_bytes, UINT p_bind)
{
	D3D11_BUFFER_DESC desc = {};
	if (p_buffer) {
		p_buffer->GetDesc(&desc);
		if (desc.ByteWidth >= p_bytes) {
			return true;
		}
	}
	p_buffer.Reset();
	desc = {};
	desc.ByteWidth = p_bytes;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = p_bind;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	return Checked(device->CreateBuffer(&desc, nullptr, p_buffer.GetAddressOf()), "CreateBuffer dynamic");
}

bool MiniwinD3D11Native::Impl::Upload(ComPtr<ID3D11Buffer>& p_buffer, const void* p_data, UINT p_bytes, UINT p_bind)
{
	if (!DynamicBuffer(p_buffer, p_bytes, p_bind)) {
		return false;
	}
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (!Checked(context->Map(p_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map dynamic buffer")) {
		return false;
	}
	SDL_memcpy(mapped.pData, p_data, p_bytes);
	context->Unmap(p_buffer.Get(), 0);
	return true;
}

Pipeline* MiniwinD3D11Native::Impl::PipelineFor(const MiniwinD3D11State& p_state)
{
	Uint32 key = PipelineKey(p_state);
	auto found = pipelines.find(key);
	if (found != pipelines.end()) {
		return &found->second;
	}
	Pipeline pipeline;
	D3D11_BLEND_DESC blend = {};
	auto& rt = blend.RenderTarget[0];
	rt.BlendEnable = p_state.alphaBlend;
	rt.SrcBlend = BlendFactor(p_state.srcBlend, false);
	rt.DestBlend = BlendFactor(p_state.destBlend, false);
	rt.SrcBlendAlpha = BlendFactor(p_state.srcBlend, true);
	rt.DestBlendAlpha = BlendFactor(p_state.destBlend, true);
	rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = p_state.colorWrite ? D3D11_COLOR_WRITE_ENABLE_ALL : 0;
	D3D11_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = p_state.zEnable;
	depth.DepthWriteMask = p_state.zWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	// D3DCMPFUNC and D3D11_COMPARISON_FUNC share values 1..8.
	depth.DepthFunc =
		p_state.zFunc >= 1 && p_state.zFunc <= 8 ? (D3D11_COMPARISON_FUNC) p_state.zFunc : D3D11_COMPARISON_ALWAYS;
	depth.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
	depth.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
	depth.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depth.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	depth.BackFace = depth.FrontFace;
	D3D11_RASTERIZER_DESC raster = {};
	raster.FillMode = D3D11_FILL_SOLID;
	raster.CullMode = p_state.cullMode == 1 ? D3D11_CULL_NONE : D3D11_CULL_BACK;
	// Identical to SDL_GPU's D3D11 driver: CW front for D3DCULL_CW (2),
	// CCW otherwise. The TL shader's Y inversion is already accounted for.
	raster.FrontCounterClockwise = p_state.cullMode != 2;
	raster.DepthClipEnable = FALSE; // SDL_GPU's default: clamp depth rather than clip.
	if (!Checked(device->CreateBlendState(&blend, pipeline.blend.GetAddressOf()), "CreateBlendState") ||
		!Checked(device->CreateDepthStencilState(&depth, pipeline.depth.GetAddressOf()), "CreateDepthStencilState") ||
		!Checked(device->CreateRasterizerState(&raster, pipeline.raster.GetAddressOf()), "CreateRasterizerState")) {
		return nullptr;
	}
	return &pipelines.emplace(key, pipeline).first->second;
}

void MiniwinD3D11Native::Clear(float p_r, float p_g, float p_b, bool p_color, bool p_depth)
{
	Impl& d = *m_impl;
	const float color[] = {p_r, p_g, p_b, 1.f};
	if (p_color && d.sceneTarget) {
		d.context->ClearRenderTargetView(d.sceneTarget.Get(), color);
	}
	if (p_depth && d.sceneDepth) {
		d.context->ClearDepthStencilView(d.sceneDepth.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
	}
}

bool MiniwinD3D11Native::Impl::Draw(
	const MiniwinD3D11State& p_state,
	const void* p_vertices,
	Uint32 p_vertexCount,
	const Uint16* p_indices,
	Uint32 p_indexCount,
	int p_logicalW,
	int p_logicalH,
	bool p_overlay,
	ID3D11ShaderResourceView* p_texture,
	const SDL_Rect* p_viewport
)
{
	if (!p_vertices || !p_vertexCount || (p_indices && !p_indexCount) ||
		p_vertexCount > std::numeric_limits<UINT>::max() / sizeof(MiniwinD3D11Vertex) ||
		p_indexCount > std::numeric_limits<UINT>::max() / sizeof(Uint16) || p_logicalW <= 0 || p_logicalH <= 0) {
		return false;
	}
	ID3D11RenderTargetView* target = p_overlay ? swapTarget.Get() : sceneTarget.Get();
	if (!target) {
		return false;
	}
	Pipeline* pipeline = PipelineFor(p_state);
	VertexUniforms vu = {{(float) p_logicalW, (float) p_logicalH}, {0.f, 0.f}};
	FragmentUniforms fu = {
		p_state.colorOp,
		p_state.alphaOp,
		p_state.specular,
		p_state.alphaFunc,
		p_state.alphaRef,
		p_state.colorKey,
		{0.f, 0.f}
	};
	SDL_memcpy(fu.pointOriginStep, p_state.pointOriginStep, sizeof(fu.pointOriginStep));
	if (sceneW > p_logicalW || sceneH > p_logicalH) {
		SDL_memcpy(fu.pointCount, p_state.pointCount, sizeof(fu.pointCount));
	}
	if (!pipeline ||
		!Upload(vertices, p_vertices, p_vertexCount * sizeof(MiniwinD3D11Vertex), D3D11_BIND_VERTEX_BUFFER) ||
		(p_indices && !Upload(indices, p_indices, p_indexCount * sizeof(Uint16), D3D11_BIND_INDEX_BUFFER)) ||
		!Upload(vertexUniforms, &vu, sizeof(vu), D3D11_BIND_CONSTANT_BUFFER) ||
		!Upload(fragmentUniforms, &fu, sizeof(fu), D3D11_BIND_CONSTANT_BUFFER)) {
		return false;
	}
	// Explicitly unbind scene sampling before making it a render target again.
	ID3D11ShaderResourceView* nullView = nullptr;
	context->PSSetShaderResources(0, 1, &nullView);
	context->OMSetRenderTargets(1, &target, p_overlay ? nullptr : sceneDepth.Get());
	D3D11_VIEWPORT viewport = {};
	viewport.Width = (float) (p_overlay ? drawableW : sceneW);
	viewport.Height = (float) (p_overlay ? drawableH : sceneH);
	viewport.MaxDepth = 1.f;
	if (p_viewport) {
		viewport.TopLeftX = (float) p_viewport->x;
		viewport.TopLeftY = (float) p_viewport->y;
		viewport.Width = (float) p_viewport->w;
		viewport.Height = (float) p_viewport->h;
	}
	context->RSSetViewports(1, &viewport);
	context->RSSetState(pipeline->raster.Get());
	context->OMSetBlendState(pipeline->blend.Get(), nullptr, 0xffffffff);
	context->OMSetDepthStencilState(pipeline->depth.Get(), 0);
	context->VSSetShader(vs.Get(), nullptr, 0);
	context->PSSetShader(ps.Get(), nullptr, 0);
	context->VSSetConstantBuffers(0, 1, vertexUniforms.GetAddressOf());
	context->PSSetConstantBuffers(1, 1, fragmentUniforms.GetAddressOf());
	if (!p_texture) {
		auto found = textures.find(p_state.textureId);
		p_texture = found == textures.end() ? dummy.view.Get() : found->second.view.Get();
	}
	context->PSSetShaderResources(0, 1, &p_texture);
	context->PSSetSamplers(0, 1, samplers[p_state.linear][p_state.wrap].GetAddressOf());
	context->IASetInputLayout(layout.Get());
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const UINT stride = sizeof(MiniwinD3D11Vertex), offset = 0;
	context->IASetVertexBuffers(0, 1, vertices.GetAddressOf(), &stride, &offset);
	if (p_indices) {
		context->IASetIndexBuffer(indices.Get(), DXGI_FORMAT_R16_UINT, 0);
		context->DrawIndexed(p_indexCount, 0, 0);
	}
	else {
		context->Draw(p_vertexCount, 0);
	}
	return true;
}

void MiniwinD3D11Native::Draw(
	const MiniwinD3D11State& p_state,
	const void* p_vertices,
	Uint32 p_vertexCount,
	const Uint16* p_indices,
	Uint32 p_indexCount,
	int p_logicalW,
	int p_logicalH,
	bool p_overlay
)
{
	m_impl->Draw(p_state, p_vertices, p_vertexCount, p_indices, p_indexCount, p_logicalW, p_logicalH, p_overlay);
}

bool MiniwinD3D11Native::Composite(const SDL_Rect& p_viewport)
{
	Impl& d = *m_impl;
	if (!d.swapTarget || !d.sceneView || p_viewport.w <= 0 || p_viewport.h <= 0) {
		return false;
	}
	const float black[] = {0.f, 0.f, 0.f, 1.f};
	d.context->ClearRenderTargetView(d.swapTarget.Get(), black);
	const MiniwinD3D11Vertex vertices[] = {
		{0.f, 0.f, 0.f, 1.f, 0xffffffff, 0, 0.f, 0.f},
		{1.f, 0.f, 0.f, 1.f, 0xffffffff, 0, 1.f, 0.f},
		{0.f, 1.f, 0.f, 1.f, 0xffffffff, 0, 0.f, 1.f},
		{1.f, 1.f, 0.f, 1.f, 0xffffffff, 0, 1.f, 1.f}
	};
	const Uint16 indices[] = {0, 1, 2, 2, 1, 3};
	MiniwinD3D11State state;
	state.zEnable = state.zWrite = false;
	state.colorOp = state.alphaOp = 1;
	// A unit logical quad fills the letterbox viewport; bilinear/clamp matches blit.
	bool ok = d.Draw(state, vertices, 4, indices, 6, 1, 1, true, d.sceneView.Get(), &p_viewport);
	ID3D11ShaderResourceView* nullView = nullptr;
	d.context->PSSetShaderResources(0, 1, &nullView);
	return ok;
}

void MiniwinD3D11Native::Present()
{
	if (!Checked(m_impl->swapchain->Present(1, 0), "Present")) {
		Checked(m_impl->device->GetDeviceRemovedReason(), "GetDeviceRemovedReason");
	}
}

SDL_Surface* MiniwinD3D11Native::ReadBackbuffer()
{
	Impl& d = *m_impl;
	if (!d.scene) {
		return nullptr;
	}
	D3D11_TEXTURE2D_DESC desc = {};
	d.scene->GetDesc(&desc);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;
	if (!Checked(d.device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()), "CreateTexture2D readback")) {
		return nullptr;
	}
	d.context->OMSetRenderTargets(0, nullptr, nullptr);
	d.context->CopyResource(staging.Get(), d.scene.Get());
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	// Blocking Map waits for the scene copy; rows may have GPU padding.
	if (!Checked(d.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map readback")) {
		return nullptr;
	}
	SDL_Surface* raw = SDL_CreateSurface(d.sceneW, d.sceneH, SDL_PIXELFORMAT_BGRA32);
	if (raw) {
		for (int y = 0; y < d.sceneH; y++) {
			SDL_memcpy(
				(Uint8*) raw->pixels + y * raw->pitch,
				(const Uint8*) mapped.pData + y * mapped.RowPitch,
				d.sceneW * 4
			);
		}
	}
	d.context->Unmap(staging.Get(), 0);
	SDL_Surface* surface = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ABGR8888) : nullptr;
	SDL_DestroySurface(raw);
	return surface;
}
