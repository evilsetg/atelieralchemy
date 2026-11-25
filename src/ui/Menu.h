#pragma once

namespace ui::Menu
{
	inline constexpr std::string_view kMenuName = "AtelierAlchemyMenu";

	// kDataLoaded: registers the IMenu (pauses the game, blocks gameplay input) and
	// the menu open/close watcher.
	void Register();

	// Main thread (input handler): open if allowed here, or close.
	void RequestToggle();

	[[nodiscard]] bool IsOpen();
	[[nodiscard]] bool IsRingActive();
	[[nodiscard]] bool ShouldRender();

	// Render thread, inside an ImGui frame.
	void Draw();
}
