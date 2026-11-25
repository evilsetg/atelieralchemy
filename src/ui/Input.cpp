#include "ui/Input.h"

#include "game/Config.h"
#include "ui/Menu.h"

#include <imgui.h>

namespace ui
{
	namespace
	{
		struct QueuedEvent
		{
			enum class Type : std::uint8_t
			{
				kKey,
				kMouseButton,
				kMouseWheel,
				kMouseMove,
				kChar,
			};
			Type     type{ Type::kKey };
			ImGuiKey key{ ImGuiKey_None };
			int      button{ 0 };
			bool     down{ false };
			float    x{ 0.0f };
			float    y{ 0.0f };
			unsigned ch{ 0 };
		};

		std::mutex                     g_lock;
		std::vector<QueuedEvent>       g_events;
		std::vector<RingPress>         g_ringPresses;

		// Text entry: the render thread says whether an ImGui text field is active, the main
		// thread switches Skyrim's text mode (which makes the game send character events and
		// stop treating keys as controls) to match.
		std::atomic_bool g_wantText{ false };
		// Set when the menu opens or closes: ImGui never sees the keys released while the menu
		// was closed (e.g. the Escape that closed it), so its key state is reset next frame.
		std::atomic_bool g_resetImGui{ false };
		bool             g_textEnabled{ false };  // main thread only

		void ApplyTextInput()
		{
			const bool want = g_wantText && Menu::IsOpen();
			if (want == g_textEnabled) {
				return;
			}
			if (auto* controls = RE::ControlMap::GetSingleton()) {
				controls->AllowTextInput(want);
				g_textEnabled = want;
			}
		}

		// Gamepad button masks as reported by Skyrim's ButtonEvent::idCode.
		namespace Pad
		{
			constexpr std::uint32_t kUp = 0x0001;
			constexpr std::uint32_t kDown = 0x0002;
			constexpr std::uint32_t kLeft = 0x0004;
			constexpr std::uint32_t kRight = 0x0008;
			constexpr std::uint32_t kStart = 0x0010;
			constexpr std::uint32_t kBack = 0x0020;
			constexpr std::uint32_t kLB = 0x0100;
			constexpr std::uint32_t kRB = 0x0200;
			constexpr std::uint32_t kA = 0x1000;
			constexpr std::uint32_t kB = 0x2000;
			constexpr std::uint32_t kX = 0x4000;
			constexpr std::uint32_t kY = 0x8000;
		}

		ImGuiKey MapKeyboard(std::uint32_t a_dik)
		{
			switch (a_dik) {
			case 0x01:
				return ImGuiKey_Escape;
			case 0x0E:
				return ImGuiKey_Backspace;
			case 0x0F:
				return ImGuiKey_Tab;
			case 0x1C:
				return ImGuiKey_Enter;
			case 0x9C:
				return ImGuiKey_KeypadEnter;
			case 0x1D:
				return ImGuiKey_LeftCtrl;
			case 0x9D:
				return ImGuiKey_RightCtrl;
			case 0x2A:
				return ImGuiKey_LeftShift;
			case 0x36:
				return ImGuiKey_RightShift;
			case 0x38:
				return ImGuiKey_LeftAlt;
			case 0x39:
				return ImGuiKey_Space;
			case 0xC8:
				return ImGuiKey_UpArrow;
			case 0xD0:
				return ImGuiKey_DownArrow;
			case 0xCB:
				return ImGuiKey_LeftArrow;
			case 0xCD:
				return ImGuiKey_RightArrow;
			case 0xC9:
				return ImGuiKey_PageUp;
			case 0xD1:
				return ImGuiKey_PageDown;
			case 0xC7:
				return ImGuiKey_Home;
			case 0xCF:
				return ImGuiKey_End;
			case 0xD3:
				return ImGuiKey_Delete;
			default:
				break;
			}
			// Letter rows of the DirectInput layout.
			static constexpr std::string_view row1 = "QWERTYUIOP";  // 0x10..0x19
			static constexpr std::string_view row2 = "ASDFGHJKL";   // 0x1E..0x26
			static constexpr std::string_view row3 = "ZXCVBNM";     // 0x2C..0x32
			auto letter = [](char c) { return static_cast<ImGuiKey>(ImGuiKey_A + (c - 'A')); };
			if (a_dik >= 0x10 && a_dik < 0x10 + row1.size()) {
				return letter(row1[a_dik - 0x10]);
			}
			if (a_dik >= 0x1E && a_dik < 0x1E + row2.size()) {
				return letter(row2[a_dik - 0x1E]);
			}
			if (a_dik >= 0x2C && a_dik < 0x2C + row3.size()) {
				return letter(row3[a_dik - 0x2C]);
			}
			return ImGuiKey_None;
		}

		ImGuiKey MapGamepad(std::uint32_t a_mask)
		{
			switch (a_mask) {
			case Pad::kUp:
				return ImGuiKey_GamepadDpadUp;
			case Pad::kDown:
				return ImGuiKey_GamepadDpadDown;
			case Pad::kLeft:
				return ImGuiKey_GamepadDpadLeft;
			case Pad::kRight:
				return ImGuiKey_GamepadDpadRight;
			case Pad::kStart:
				return ImGuiKey_GamepadStart;
			case Pad::kBack:
				return ImGuiKey_GamepadBack;
			case Pad::kLB:
				return ImGuiKey_GamepadL1;
			case Pad::kRB:
				return ImGuiKey_GamepadR1;
			case Pad::kA:
				return ImGuiKey_GamepadFaceDown;
			case Pad::kB:
				return ImGuiKey_GamepadFaceRight;
			case Pad::kX:
				return ImGuiKey_GamepadFaceLeft;
			case Pad::kY:
				return ImGuiKey_GamepadFaceUp;
			default:
				return ImGuiKey_None;
			}
		}

		bool IsRingButton(RE::INPUT_DEVICE a_device, std::uint32_t a_code)
		{
			switch (a_device) {
			case RE::INPUT_DEVICE::kKeyboard:
				return a_code == 0x39 || a_code == 0x12 || a_code == 0x1C;  // Space, E, Enter
			case RE::INPUT_DEVICE::kMouse:
				return a_code == 0;  // left button
			case RE::INPUT_DEVICE::kGamepad:
				return a_code == Pad::kA;
			default:
				return false;
			}
		}
	}

	InputHandler* InputHandler::Get()
	{
		static InputHandler handler;
		return &handler;
	}

	void InputHandler::Register()
	{
		if (auto* manager = RE::BSInputDeviceManager::GetSingleton()) {
			manager->AddEventSink<RE::InputEvent*>(Get());
			logger::info("Input handler registered");
		}
	}

	RE::BSEventNotifyControl InputHandler::ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const auto& config = game::Config::Current();
		const bool  open = Menu::IsOpen();
		const bool  ringActive = Menu::IsRingActive();
		const auto  now = Clock::now();

		std::scoped_lock lock(g_lock);
		for (auto* event = *a_event; event; event = event->next) {
			if (const auto button = event->AsButtonEvent()) {
				const auto device = button->GetDevice();
				const auto code = button->GetIDCode();
				const bool down = button->IsDown();
				const bool up = button->IsUp();
				if (!down && !up) {
					continue;  // held: ImGui tracks state itself
				}

				const bool typing = g_wantText && device == RE::INPUT_DEVICE::kKeyboard;
				const bool hotkey = (!typing && device == RE::INPUT_DEVICE::kKeyboard && code == config.hotkeyKeyboard) ||
				                    (device == RE::INPUT_DEVICE::kGamepad && config.hotkeyGamepad != 0 && code == config.hotkeyGamepad);
				if (hotkey && down && !ringActive) {
					Menu::RequestToggle();
					continue;
				}
				if (!open) {
					continue;
				}

				if (ringActive && IsRingButton(device, code)) {
					const bool mouse = device == RE::INPUT_DEVICE::kMouse;
					if (down) {
						g_ringPresses.push_back({ now, mouse });
					}
					if (!mouse) {
						continue;  // keys and pad buttons must not activate UI buttons mid-ring
					}
					// Clicks still reach ImGui so the Skip button works; the menu drops ring
					// presses made on it.
				}

				switch (device) {
				case RE::INPUT_DEVICE::kKeyboard:
					if (const auto key = MapKeyboard(code); key != ImGuiKey_None) {
						g_events.push_back({ .type = QueuedEvent::Type::kKey, .key = key, .down = down });
					}
					break;
				case RE::INPUT_DEVICE::kMouse:
					if (code <= 4) {
						g_events.push_back({ .type = QueuedEvent::Type::kMouseButton, .button = static_cast<int>(code), .down = down });
					} else if ((code == 8 || code == 9) && down) {
						g_events.push_back({ .type = QueuedEvent::Type::kMouseWheel, .y = code == 8 ? 1.0f : -1.0f });
					}
					break;
				case RE::INPUT_DEVICE::kGamepad:
					if (const auto key = MapGamepad(code); key != ImGuiKey_None) {
						g_events.push_back({ .type = QueuedEvent::Type::kKey, .key = key, .down = down });
					}
					break;
				default:
					break;
				}
			} else if (open && event->GetEventType() == RE::INPUT_EVENT_TYPE::kChar) {
				const auto ch = static_cast<RE::CharEvent*>(event)->keyCode;
				if (ch >= 0x20 && ch != 0x7F) {  // control keys arrive as button events
					g_events.push_back({ .type = QueuedEvent::Type::kChar, .ch = ch });
				}
			} else if (open && event->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
				const auto move = static_cast<RE::MouseMoveEvent*>(event);
				g_events.push_back({ .type = QueuedEvent::Type::kMouseMove,
					.x = static_cast<float>(move->mouseInputX),
					.y = static_cast<float>(move->mouseInputY) });
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void ApplyQueuedInput()
	{
		auto& io = ImGui::GetIO();

		if (g_resetImGui.exchange(false)) {
			io.ClearEventsQueue();
			io.ClearInputKeys();
			io.ClearInputMouse();
		}

		std::vector<QueuedEvent> events;
		{
			std::scoped_lock lock(g_lock);
			events.swap(g_events);
		}

		// Virtual cursor fed by raw mouse deltas; only used when the game's own menu
		// cursor isn't available.
		static ImVec2 cursor{ -1.0f, -1.0f };
		if (cursor.x < 0.0f) {
			cursor = { io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f };
		}

		for (const auto& e : events) {
			switch (e.type) {
			case QueuedEvent::Type::kKey:
				io.AddKeyEvent(e.key, e.down);
				if (e.key == ImGuiKey_LeftCtrl || e.key == ImGuiKey_RightCtrl) {
					io.AddKeyEvent(ImGuiMod_Ctrl, e.down);
				} else if (e.key == ImGuiKey_LeftShift || e.key == ImGuiKey_RightShift) {
					io.AddKeyEvent(ImGuiMod_Shift, e.down);
				} else if (e.key == ImGuiKey_LeftAlt) {
					io.AddKeyEvent(ImGuiMod_Alt, e.down);
				}
				break;
			case QueuedEvent::Type::kMouseButton:
				io.AddMouseButtonEvent(e.button, e.down);
				break;
			case QueuedEvent::Type::kMouseWheel:
				io.AddMouseWheelEvent(0.0f, e.y);
				break;
			case QueuedEvent::Type::kChar:
				io.AddInputCharacter(e.ch);
				break;
			case QueuedEvent::Type::kMouseMove:
				cursor.x = std::clamp(cursor.x + e.x, 0.0f, io.DisplaySize.x - 1.0f);
				cursor.y = std::clamp(cursor.y + e.y, 0.0f, io.DisplaySize.y - 1.0f);
				break;
			}
		}

		// Prefer the game's menu cursor: it follows the player's mouse settings and keeps
		// moving even when raw mouse-move events aren't dispatched in menu mode.
		if (auto* menuCursor = RE::MenuCursor::GetSingleton()) {
			const auto& data = menuCursor->GetRuntimeData();
			if (data.screenWidthX > 0.0f && data.screenWidthY > 0.0f) {
				cursor.x = std::clamp(data.cursorPosX * io.DisplaySize.x / data.screenWidthX, 0.0f, io.DisplaySize.x - 1.0f);
				cursor.y = std::clamp(data.cursorPosY * io.DisplaySize.y / data.screenWidthY, 0.0f, io.DisplaySize.y - 1.0f);
			}
		}

		// Report the position every frame so ImGui always has a valid cursor to draw,
		// even before the mouse has moved.
		io.AddMousePosEvent(cursor.x, cursor.y);
	}

	std::vector<RingPress> TakeRingPresses()
	{
		std::scoped_lock lock(g_lock);
		std::vector<RingPress> out;
		out.swap(g_ringPresses);
		return out;
	}

	void ClearInput()
	{
		{
			std::scoped_lock lock(g_lock);
			g_events.clear();
			g_ringPresses.clear();
		}
		g_wantText = false;
		g_resetImGui = true;
		ApplyTextInput();
	}

	void UpdateTextInput(bool a_wantText)
	{
		if (g_wantText.exchange(a_wantText) != a_wantText) {
			SKSE::GetTaskInterface()->AddTask(ApplyTextInput);
		}
	}

	bool IsTyping()
	{
		return g_wantText;
	}

	std::string KeyName(std::uint32_t a_dik)
	{
		static const std::unordered_map<std::uint32_t, std::string_view> names = {
			{ 0x01, "Esc" }, { 0x0F, "Tab" }, { 0x1C, "Enter" }, { 0x39, "Space" }, { 0x29, "~" }, { 0x0C, "-" }, { 0x0D, "=" },
			{ 0x3B, "F1" }, { 0x3C, "F2" }, { 0x3D, "F3" }, { 0x3E, "F4" }, { 0x3F, "F5" }, { 0x40, "F6" }, { 0x41, "F7" },
			{ 0x42, "F8" }, { 0x43, "F9" }, { 0x44, "F10" }, { 0x57, "F11" }, { 0x58, "F12" }, { 0xC7, "Home" }, { 0xCF, "End" },
			{ 0xD2, "Insert" }, { 0xD3, "Delete" }, { 0xC9, "PgUp" }, { 0xD1, "PgDn" },
		};
		if (const auto it = names.find(a_dik); it != names.end()) {
			return std::string(it->second);
		}
		if (a_dik >= 0x02 && a_dik <= 0x0B) {
			return std::string(1, a_dik == 0x0B ? '0' : static_cast<char>('1' + (a_dik - 0x02)));
		}
		if (const auto key = MapKeyboard(a_dik); key >= ImGuiKey_A && key <= ImGuiKey_Z) {
			return std::string(1, static_cast<char>('A' + (key - ImGuiKey_A)));
		}
		return std::format("0x{:02X}", a_dik);
	}
}
