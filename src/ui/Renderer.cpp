#include "ui/Renderer.h"

#include "game/Config.h"
#include "ui/Input.h"
#include "ui/Menu.h"
#include "ui/Style.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <d3d11.h>
#include <dxgi.h>

namespace ui::Renderer
{
	namespace
	{
		using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

		Present_t            g_originalPresent{ nullptr };
		bool                 g_initialized{ false };
		bool                 g_failed{ false };
		ID3D11Device*        g_device{ nullptr };
		ID3D11DeviceContext* g_context{ nullptr };
		Clock::time_point    g_lastFrame{};
		ImFont*              g_largeFont{ nullptr };
		float                g_scale{ 1.0f };

		bool Initialize(IDXGISwapChain* a_swapChain)
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			if (FAILED(a_swapChain->GetDesc(&desc)) ||
				FAILED(a_swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device)))) {
				logger::error("Renderer: could not query the D3D11 device from the swap chain");
				return false;
			}
			g_device->GetImmediateContext(&g_context);

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

			const auto& config = game::Config::Current();
			const float height = static_cast<float>(desc.BufferDesc.Height > 0 ? desc.BufferDesc.Height : 1080);
			g_scale = std::clamp(height / 1080.0f, 0.75f, 3.0f) * config.uiScale;
			const float fontSize = config.fontSize > 0.0f ? config.fontSize * config.uiScale : std::round(20.0f * g_scale);

			std::error_code ec;
			if (!config.fontPath.empty() && std::filesystem::exists(config.fontPath, ec)) {
				io.Fonts->AddFontFromFileTTF(config.fontPath.c_str(), fontSize);
				g_largeFont = io.Fonts->AddFontFromFileTTF(config.fontPath.c_str(), std::round(fontSize * 1.6f));
			} else {
				logger::warn("Font '{}' not found, using ImGui's built-in font", config.fontPath);
				ImFontConfig cfg;
				cfg.SizePixels = std::round(13.0f * g_scale);
				io.Fonts->AddFontDefault(&cfg);
				cfg.SizePixels = std::round(13.0f * g_scale * 1.6f);
				g_largeFont = io.Fonts->AddFontDefault(&cfg);
			}

			ApplyStyle(g_scale);

			if (!ImGui_ImplDX11_Init(g_device, g_context)) {
				logger::error("Renderer: ImGui DX11 backend failed to initialize");
				return false;
			}
			g_lastFrame = Clock::now();
			logger::info("Renderer: ImGui initialized ({}x{}, scale {:.2f})", desc.BufferDesc.Width, desc.BufferDesc.Height, g_scale);
			return true;
		}

		void RenderFrame(IDXGISwapChain* a_swapChain)
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			a_swapChain->GetDesc(&desc);
			if (desc.BufferDesc.Width == 0 || desc.BufferDesc.Height == 0) {
				return;
			}

			auto&      io = ImGui::GetIO();
			const auto now = Clock::now();
			io.DeltaTime = std::clamp(std::chrono::duration<float>(now - g_lastFrame).count(), 1.0f / 1000.0f, 0.25f);
			g_lastFrame = now;
			io.DisplaySize = { static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height) };
			io.MouseDrawCursor = false;  // Menu::Draw paints its own cursor on top of everything

			ImGui_ImplDX11_NewFrame();
			ApplyQueuedInput();
			ImGui::NewFrame();
			Menu::Draw();
			ImGui::Render();
			UpdateTextInput(Menu::IsOpen() && io.WantTextInput);

			ID3D11Texture2D* backBuffer = nullptr;
			if (FAILED(a_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
				return;
			}
			ID3D11RenderTargetView* rtv = nullptr;
			const HRESULT           hr = g_device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
			backBuffer->Release();
			if (FAILED(hr)) {
				return;
			}

			ID3D11RenderTargetView* oldTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			ID3D11DepthStencilView* oldDepth = nullptr;
			g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, &oldDepth);

			g_context->OMSetRenderTargets(1, &rtv, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

			g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, oldDepth);
			for (auto* target : oldTargets) {
				if (target) {
					target->Release();
				}
			}
			if (oldDepth) {
				oldDepth->Release();
			}
			rtv->Release();
		}

		HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
		{
			if (!g_failed && Menu::ShouldRender()) {
				if (!g_initialized) {
					g_initialized = Initialize(a_swapChain);
					g_failed = !g_initialized;
				}
				if (g_initialized) {
					RenderFrame(a_swapChain);
				}
			} else if (g_initialized) {
				g_lastFrame = Clock::now();
			}
			return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
		}
	}

	bool Install()
	{
		const auto window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
		if (!window || !window->swapChain) {
			logger::critical("Renderer: no swap chain available - the Atelier menu cannot be shown");
			return false;
		}

		auto*       swapChain = reinterpret_cast<IDXGISwapChain*>(window->swapChain);
		auto**      vtable = *reinterpret_cast<void***>(swapChain);
		constexpr std::size_t kPresentIndex = 8;

		g_originalPresent = reinterpret_cast<Present_t>(vtable[kPresentIndex]);
		const auto hook = reinterpret_cast<void*>(&HookedPresent);
		REL::safe_write(reinterpret_cast<std::uintptr_t>(&vtable[kPresentIndex]), &hook, sizeof(hook));
		logger::info("Renderer: hooked IDXGISwapChain::Present");
		return true;
	}

	ImFont* LargeFont()
	{
		return g_largeFont;
	}

	float Scale()
	{
		return g_scale;
	}
}
