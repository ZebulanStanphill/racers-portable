// [library:3d] Throwaway on-console diagnostic, deliberately independent of SDL_GPU.
// Compiled in its own UWP-only target against the real Windows SDK, not miniwin's shims.
#ifdef WINDOWS_STORE

#include "miniwin.h"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <gamingdeviceinformation.h>
#include <miniwin/uwp.h>
#include <roapi.h>
#include <windows.system.profile.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>

// FXC headers use BYTE, so include the generated arrays after the Windows SDK.
#include "probe_ps_dxbc.h"
#include "probe_ps_dxil.h"
#include "probe_vs_dxbc.h"
#include "probe_vs_dxil.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;

namespace
{

// HRESULTs are always logged, including successful calls. ExecuteCommandLists returns
// void; its immediate device status and the fence/readback are the execution evidence.
bool LogResult(const char* p_variant, const char* p_operation, HRESULT p_result, ID3D12Device* p_device = nullptr)
{
	if (FAILED(p_result)) {
		SDL_LogError(LOG_CATEGORY_MINIWIN, "[d3dprobe] %s %s: hr=0x%08x", p_variant, p_operation, (unsigned) p_result);
		if (p_device) {
			SDL_LogError(
				LOG_CATEGORY_MINIWIN,
				"[d3dprobe] %s GetDeviceRemovedReason: hr=0x%08x",
				p_variant,
				(unsigned) p_device->GetDeviceRemovedReason()
			);
		}
		return false;
	}
	SDL_LogInfo(LOG_CATEGORY_MINIWIN, "[d3dprobe] %s %s: hr=0x%08x", p_variant, p_operation, (unsigned) p_result);
	return true;
}

void LogEnvironment()
{
	// SDL has normally initialized WinRT already. Balance only our successful init;
	// RPC_E_CHANGED_MODE still allows using the main thread's existing apartment.
	HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
	LogResult("environment", "RoInitialize", init);
	{
		ComPtr<ABI::Windows::System::Profile::IAnalyticsInfoStatics> analytics;
		HRESULT hr = RoGetActivationFactory(
			HStringReference(RuntimeClass_Windows_System_Profile_AnalyticsInfo).Get(),
			IID_PPV_ARGS(&analytics)
		);
		if (LogResult("environment", "AnalyticsInfo", hr)) {
			ComPtr<ABI::Windows::System::Profile::IAnalyticsVersionInfo> version;
			hr = analytics->get_VersionInfo(&version);
			if (LogResult("environment", "VersionInfo", hr)) {
				HString family;
				HString encoded;
				HRESULT familyHr = version->get_DeviceFamily(family.GetAddressOf());
				HRESULT versionHr = version->get_DeviceFamilyVersion(encoded.GetAddressOf());
				LogResult("environment", "DeviceFamily", familyHr);
				LogResult("environment", "DeviceFamilyVersion", versionHr);
				if (SUCCEEDED(familyHr) && SUCCEEDED(versionHr)) {
					const wchar_t* digits = encoded.GetRawBuffer(nullptr);
					Uint64 value = 0;
					for (; *digits >= L'0' && *digits <= L'9'; ++digits) {
						value = value * 10 + (*digits - L'0');
					}
					char* utf8 = SDL_iconv_string(
						"UTF-8",
						"UTF-16LE",
						(const char*) family.GetRawBuffer(nullptr),
						(family.Length() + 1) * sizeof(wchar_t)
					);
					SDL_LogInfo(
						LOG_CATEGORY_MINIWIN,
						"[d3dprobe] environment DeviceFamily=%s OS=%u.%u.%u.%u",
						utf8 ? utf8 : "unknown",
						(unsigned) ((value >> 48) & 0xffff),
						(unsigned) ((value >> 32) & 0xffff),
						(unsigned) ((value >> 16) & 0xffff),
						(unsigned) (value & 0xffff)
					);
					SDL_free(utf8);
				}
			}
		}
	}
	if (SUCCEEDED(init)) {
		RoUninitialize();
	}

	GAMING_DEVICE_MODEL_INFORMATION model = {};
	HRESULT hr = GetGamingDeviceModelInformation(&model);
	LogResult("environment", "GetGamingDeviceModelInformation", hr);
	if (SUCCEEDED(hr)) {
		SDL_LogInfo(
			LOG_CATEGORY_MINIWIN,
			"[d3dprobe] gaming device vendor=0x%08x device=0x%08x",
			(unsigned) model.vendorId,
			(unsigned) model.deviceId
		);
	}
}

ComPtr<IDXGIAdapter1> FindHardwareAdapter()
{
	ComPtr<IDXGIFactory1> factory;
	if (!LogResult("DXGI", "CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
		return nullptr;
	}
	ComPtr<IDXGIAdapter1> first;
	for (UINT index = 0;; ++index) {
		ComPtr<IDXGIAdapter1> adapter;
		HRESULT hr = factory->EnumAdapters1(index, &adapter);
		if (hr == DXGI_ERROR_NOT_FOUND) {
			break;
		}
		if (!LogResult("DXGI", "EnumAdapters1", hr)) {
			break;
		}
		DXGI_ADAPTER_DESC1 desc = {};
		if (!LogResult("DXGI", "GetDesc1", adapter->GetDesc1(&desc))) {
			continue;
		}
		char* name = SDL_iconv_string("UTF-8", "UTF-16LE", (const char*) desc.Description, sizeof(desc.Description));
		SDL_LogInfo(
			LOG_CATEGORY_MINIWIN,
			"[d3dprobe] adapter[%u] %s vendor=0x%04x device=0x%04x dedicatedVideoMemory=%llu bytes software=%u",
			index,
			name ? name : "unknown",
			desc.VendorId,
			desc.DeviceId,
			(unsigned long long) desc.DedicatedVideoMemory,
			(unsigned) ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
		);
		SDL_free(name);
		if (!first && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
			first = adapter;
		}
	}
	return first;
}

void LogD3D12Features(ID3D12Device* p_device)
{
	const D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};
	D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels = {};
	featureLevels.NumFeatureLevels = SDL_arraysize(levels);
	featureLevels.pFeatureLevelsRequested = levels;
	HRESULT hr = p_device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels));
	LogResult("D3D12", "FEATURE_LEVELS (12.2..11.0)", hr, p_device);
	if (hr == E_INVALIDARG) {
		featureLevels.NumFeatureLevels--;
		featureLevels.pFeatureLevelsRequested++;
		hr = p_device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels));
		LogResult("D3D12", "FEATURE_LEVELS retry (12.1..11.0)", hr, p_device);
	}
	if (SUCCEEDED(hr)) {
		SDL_LogInfo(
			LOG_CATEGORY_MINIWIN,
			"[d3dprobe] D3D12 max feature level=0x%04x",
			(unsigned) featureLevels.MaxSupportedFeatureLevel
		);
	}

	const D3D_SHADER_MODEL models[] = {
		D3D_SHADER_MODEL_6_7,
		D3D_SHADER_MODEL_6_6,
		D3D_SHADER_MODEL_6_5,
		D3D_SHADER_MODEL_6_4,
		D3D_SHADER_MODEL_6_3,
		D3D_SHADER_MODEL_6_2,
		D3D_SHADER_MODEL_6_1,
		D3D_SHADER_MODEL_6_0,
		D3D_SHADER_MODEL_5_1
	};
	for (D3D_SHADER_MODEL model : models) {
		D3D12_FEATURE_DATA_SHADER_MODEL query = {model};
		hr = p_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &query, sizeof(query));
		SDL_LogInfo(
			LOG_CATEGORY_MINIWIN,
			"[d3dprobe] D3D12 SHADER_MODEL requested=%x.%x hr=0x%08x HighestShaderModel=%x.%x%s",
			(unsigned) model >> 4,
			(unsigned) model & 15,
			(unsigned) hr,
			(unsigned) query.HighestShaderModel >> 4,
			(unsigned) query.HighestShaderModel & 15,
			FAILED(hr) ? " (output invalid on failure; E_INVALIDARG means unknown to runtime)" : ""
		);
	}
	D3D12_FEATURE_DATA_ROOT_SIGNATURE root = {D3D_ROOT_SIGNATURE_VERSION_1_2};
	hr = p_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &root, sizeof(root));
	LogResult("D3D12", "ROOT_SIGNATURE requested=1.2", hr, p_device);
	if (hr == E_INVALIDARG) {
		root.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		hr = p_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &root, sizeof(root));
		LogResult("D3D12", "ROOT_SIGNATURE requested=1.1", hr, p_device);
	}
	if (hr == E_INVALIDARG) {
		root.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		hr = p_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &root, sizeof(root));
		LogResult("D3D12", "ROOT_SIGNATURE requested=1.0", hr, p_device);
	}
	if (SUCCEEDED(hr)) {
		SDL_LogInfo(
			LOG_CATEGORY_MINIWIN,
			"[d3dprobe] D3D12 root signature highest version=%u (1=1.0, 2=1.1, 3=1.2)",
			(unsigned) root.HighestVersion
		);
	}
	D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
	if (LogResult(
			"D3D12",
			"OPTIONS",
			p_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)),
			p_device
		)) {
		SDL_LogInfo(
			LOG_CATEGORY_MINIWIN,
			"[d3dprobe] D3D12 ResourceBindingTier=%u TiledResourcesTier=%u",
			(unsigned) options.ResourceBindingTier,
			(unsigned) options.TiledResourcesTier
		);
	}
}

bool ExecuteShaderTest(ID3D12Device* p_device, bool p_dxil)
{
	const char* variant = p_dxil ? "DXIL 6.0" : "DXBC 5.1";
	const UINT width = 64;
	const UINT height = 64;
	ComPtr<ID3DBlob> rootBlob;
	ComPtr<ID3DBlob> errors;
	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	if (!LogResult(
			variant,
			"SerializeRootSignature 1.0",
			D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootBlob, &errors),
			p_device
		)) {
		return false;
	}
	ComPtr<ID3D12RootSignature> root;
	if (!LogResult(
			variant,
			"CreateRootSignature",
			p_device
				->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&root)),
			p_device
		)) {
		return false;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = {};
	pipelineDesc.pRootSignature = root.Get();
	pipelineDesc.VS = p_dxil ? D3D12_SHADER_BYTECODE{g_probe_vs_dxil, sizeof(g_probe_vs_dxil)}
							 : D3D12_SHADER_BYTECODE{g_probe_vs_dxbc, sizeof(g_probe_vs_dxbc)};
	pipelineDesc.PS = p_dxil ? D3D12_SHADER_BYTECODE{g_probe_ps_dxil, sizeof(g_probe_ps_dxil)}
							 : D3D12_SHADER_BYTECODE{g_probe_ps_dxbc, sizeof(g_probe_ps_dxbc)};
	pipelineDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	pipelineDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
	pipelineDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	pipelineDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	pipelineDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	pipelineDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	pipelineDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
	pipelineDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pipelineDesc.SampleMask = UINT_MAX;
	pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pipelineDesc.RasterizerState.DepthClipEnable = TRUE;
	pipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	pipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	pipelineDesc.DepthStencilState.FrontFace =
		{D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
	pipelineDesc.DepthStencilState.BackFace = pipelineDesc.DepthStencilState.FrontFace;
	pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineDesc.NumRenderTargets = 1;
	pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pipelineDesc.SampleDesc.Count = 1;
	ComPtr<ID3D12PipelineState> pipeline;
	if (!LogResult(
			variant,
			"CreateGraphicsPipelineState",
			p_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)),
			p_device
		)) {
		return false;
	}

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	heap.CreationNodeMask = heap.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.DepthOrArraySize = textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	ComPtr<ID3D12Resource> texture;
	if (!LogResult(
			variant,
			"CreateCommittedResource render target",
			p_device->CreateCommittedResource(
				&heap,
				D3D12_HEAP_FLAG_NONE,
				&textureDesc,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				nullptr,
				IID_PPV_ARGS(&texture)
			),
			p_device
		)) {
		return false;
	}
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT64 totalBytes = 0;
	p_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = totalBytes;
	bufferDesc.Height = bufferDesc.DepthOrArraySize = bufferDesc.MipLevels = 1;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	heap.Type = D3D12_HEAP_TYPE_READBACK;
	ComPtr<ID3D12Resource> readback;
	if (!LogResult(
			variant,
			"CreateCommittedResource readback",
			p_device->CreateCommittedResource(
				&heap,
				D3D12_HEAP_FLAG_NONE,
				&bufferDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&readback)
			),
			p_device
		)) {
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.NumDescriptors = 1;
	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	if (!LogResult(
			variant,
			"CreateDescriptorHeap",
			p_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap)),
			p_device
		)) {
		return false;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	p_device->CreateRenderTargetView(texture.Get(), nullptr, rtv);
	ComPtr<ID3D12CommandAllocator> allocator;
	if (!LogResult(
			variant,
			"CreateCommandAllocator",
			p_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
			p_device
		)) {
		return false;
	}
	ComPtr<ID3D12GraphicsCommandList> commands;
	if (!LogResult(
			variant,
			"CreateCommandList",
			p_device->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				allocator.Get(),
				pipeline.Get(),
				IID_PPV_ARGS(&commands)
			),
			p_device
		)) {
		return false;
	}
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ComPtr<ID3D12CommandQueue> queue;
	if (!LogResult(
			variant,
			"CreateCommandQueue",
			p_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)),
			p_device
		)) {
		return false;
	}
	ComPtr<ID3D12Fence> fence;
	if (!LogResult(
			variant,
			"CreateFence",
			p_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
			p_device
		)) {
		return false;
	}

	D3D12_VIEWPORT viewport = {0, 0, (float) width, (float) height, 0, 1};
	D3D12_RECT scissor = {0, 0, (LONG) width, (LONG) height};
	commands->SetGraphicsRootSignature(root.Get());
	commands->RSSetViewports(1, &viewport);
	commands->RSSetScissorRects(1, &scissor);
	commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const float clear[] = {0, 0, 1, 1};
	commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commands->DrawInstanced(3, 1, 0, 0);
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	commands->ResourceBarrier(1, &barrier);
	D3D12_TEXTURE_COPY_LOCATION source = {};
	source.pResource = texture.Get();
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION destination = {};
	destination.pResource = readback.Get();
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	destination.PlacedFootprint = footprint;
	commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
	if (!LogResult(variant, "Close", commands->Close(), p_device)) {
		return false;
	}
	ID3D12CommandList* lists[] = {commands.Get()};
	queue->ExecuteCommandLists(1, lists);
	SDL_LogInfo(LOG_CATEGORY_MINIWIN, "[d3dprobe] %s ExecuteCommandLists: void (no HRESULT)", variant);
	if (!LogResult(variant, "Execute device status", p_device->GetDeviceRemovedReason(), p_device) ||
		!LogResult(variant, "Signal", queue->Signal(fence.Get(), 1), p_device)) {
		return false;
	}

	// Poll the fence for at most five seconds instead of registering a Win32 event:
	// no event can be signalled after its handle is closed on the timeout path.
	Uint64 start = SDL_GetTicks();
	UINT64 completed = fence->GetCompletedValue();
	while (completed == 0 && SDL_GetTicks() - start < 5000) {
		SDL_Delay(1);
		completed = fence->GetCompletedValue();
	}
	const char* wait = completed == UINT64_MAX ? "DEVICE_REMOVED" : completed >= 1 ? "COMPLETED" : "TIMEOUT";
	SDL_LogInfo(
		LOG_CATEGORY_MINIWIN,
		"[d3dprobe] %s fence wait=%s completed=%llu elapsed=%llu ms timeout=5000 ms",
		variant,
		wait,
		(unsigned long long) completed,
		(unsigned long long) (SDL_GetTicks() - start)
	);
	if (completed == 0 || completed == UINT64_MAX) {
		LogResult(
			variant,
			"fence wait",
			completed == 0 ? HRESULT_FROM_WIN32(WAIT_TIMEOUT) : DXGI_ERROR_DEVICE_REMOVED,
			p_device
		);
		return false;
	}
	if (!LogResult(variant, "post-execution GetDeviceRemovedReason", p_device->GetDeviceRemovedReason(), p_device)) {
		return false;
	}

	const SIZE_T offset = (SIZE_T) footprint.Offset + (height / 2) * footprint.Footprint.RowPitch + (width / 2) * 4;
	D3D12_RANGE range = {offset, offset + 4};
	void* mapped = nullptr;
	if (!LogResult(variant, "Map readback", readback->Map(0, &range, &mapped), p_device)) {
		return false;
	}
	const BYTE* pixel = (const BYTE*) mapped + offset;
	bool pass = pixel[0] == (p_dxil ? 0 : 255) && pixel[1] == (p_dxil ? 255 : 0) && pixel[2] == 0 && pixel[3] == 255;
	SDL_LogInfo(
		LOG_CATEGORY_MINIWIN,
		"[d3dprobe] %s centre RGBA=(%u,%u,%u,%u) expected=(%u,%u,0,255) match=%s",
		variant,
		pixel[0],
		pixel[1],
		pixel[2],
		pixel[3],
		p_dxil ? 0 : 255,
		p_dxil ? 255 : 0,
		pass ? "yes" : "no"
	);
	D3D12_RANGE written = {0, 0};
	readback->Unmap(0, &written);
	if (!pass) {
		SDL_LogError(
			LOG_CATEGORY_MINIWIN,
			"[d3dprobe] %s pixel mismatch; GetDeviceRemovedReason: hr=0x%08x",
			variant,
			(unsigned) p_device->GetDeviceRemovedReason()
		);
	}
	return pass;
}

D3D_FEATURE_LEVEL ProbeD3D11()
{
	const D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_9_1
	};
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL obtained = (D3D_FEATURE_LEVEL) 0;
	HRESULT hr = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		levels,
		SDL_arraysize(levels),
		D3D11_SDK_VERSION,
		&device,
		&obtained,
		&context
	);
	LogResult("D3D11", "D3D11CreateDevice hardware (12.1..9.1)", hr);
	if (hr == E_INVALIDARG) {
		device.Reset();
		context.Reset();
		hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			levels + 3,
			SDL_arraysize(levels) - 3,
			D3D11_SDK_VERSION,
			&device,
			&obtained,
			&context
		);
		LogResult("D3D11", "D3D11CreateDevice hardware retry (11.0..9.1)", hr);
	}
	if (FAILED(hr)) {
		return (D3D_FEATURE_LEVEL) 0;
	}
	SDL_LogInfo(LOG_CATEGORY_MINIWIN, "[d3dprobe] D3D11 obtained feature level=0x%04x", (unsigned) obtained);
	D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
	if (LogResult(
			"D3D11",
			"D3D11_OPTIONS",
			device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))
		)) {
		SDL_LogInfo(
			LOG_CATEGORY_MINIWIN,
			"[d3dprobe] D3D11 ConstantBufferOffsetting=%u ConstantBufferPartialUpdate=%u",
			(unsigned) options.ConstantBufferOffsetting,
			(unsigned) options.ConstantBufferPartialUpdate
		);
	}
	D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS hardware = {};
	if (LogResult(
			"D3D11",
			"D3D10_X_HARDWARE_OPTIONS",
			device->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &hardware, sizeof(hardware))
		)) {
		SDL_LogInfo(
			LOG_CATEGORY_MINIWIN,
			"[d3dprobe] D3D11 ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x=%u",
			(unsigned) hardware.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x
		);
	}
	return obtained;
}

} // namespace

void MiniwinUwp_ProbeDirect3D()
{
	SDL_LogInfo(LOG_CATEGORY_MINIWIN, "[d3dprobe] BEGIN temporary Xbox Direct3D capability probe");
	LogEnvironment();
	bool dxbc = false;
	bool dxil = false;
	bool d3d12 = false;
	{
		ComPtr<IDXGIAdapter1> adapter = FindHardwareAdapter();
		ComPtr<ID3D12Device> device;
		if (adapter) {
			d3d12 = LogResult(
				"D3D12",
				"D3D12CreateDevice first hardware adapter FL 11.0",
				D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))
			);
		}
		else {
			SDL_LogError(LOG_CATEGORY_MINIWIN, "[d3dprobe] D3D12 skipped: no hardware adapter (never use WARP)");
		}
		if (d3d12) {
			LogD3D12Features(device.Get());
			dxbc = ExecuteShaderTest(device.Get(), false);
			dxil = ExecuteShaderTest(device.Get(), true);
		}
	}
	SDL_LogInfo(
		LOG_CATEGORY_MINIWIN,
		"[d3dprobe] DXBC 5.1: %s%s",
		dxbc ? "PASS" : "FAIL",
		d3d12 ? "" : " (D3D12 unavailable)"
	);
	SDL_LogInfo(
		LOG_CATEGORY_MINIWIN,
		"[d3dprobe] DXIL 6.0: %s%s",
		dxil ? "PASS" : "FAIL",
		d3d12 ? "" : " (D3D12 unavailable)"
	);
	D3D_FEATURE_LEVEL d3d11 = ProbeD3D11();
	SDL_LogInfo(
		LOG_CATEGORY_MINIWIN,
		"[d3dprobe] SUMMARY D3D12=%s DXBC_5_1=%s DXIL_6_0=%s D3D11_FL=0x%04x",
		d3d12 ? "available" : "unavailable",
		dxbc ? "PASS" : "FAIL",
		dxil ? "PASS" : "FAIL",
		(unsigned) d3d11
	);
	SDL_LogInfo(LOG_CATEGORY_MINIWIN, "[d3dprobe] END continuing normal app startup");
}

#endif // WINDOWS_STORE
