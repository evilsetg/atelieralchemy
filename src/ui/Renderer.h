#pragma once

struct ImFont;

namespace ui::Renderer
{
	// Hooks IDXGISwapChain::Present through the swap chain's vtable (no game
	// addresses needed) and renders the ImGui overlay when the menu needs it.
	bool Install();

	ImFont* LargeFont();
	float   Scale();
}
