#pragma once

#include <chrono>

namespace ui
{
	using Clock = std::chrono::steady_clock;

	// Listens to the game's input events (no hooks needed). Toggles the menu on the
	// hotkey and, while the menu is open, forwards keyboard/mouse/gamepad input to ImGui.
	class InputHandler final : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputHandler* Get();
		static void          Register();

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_source) override;
	};

	// Called on the render thread inside an ImGui frame.
	void ApplyQueuedInput();
	// Timestamps of "ring button" presses since the last call.
	std::vector<Clock::time_point> TakeRingPresses();
	// Main thread (menu show/hide). Also leaves Skyrim's text entry mode.
	void                           ClearInput();
	// Render thread, after each frame: whether an ImGui text field wants keyboard text.
	void UpdateTextInput(bool a_wantText);
	bool IsTyping();

	std::string KeyName(std::uint32_t a_dikCode);
}
