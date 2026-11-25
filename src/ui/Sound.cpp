#include "ui/Sound.h"

#include "game/Config.h"
#include "game/Forms.h"

namespace ui::Sound
{
	namespace
	{
		// Local form ids of the cue descriptors in AtelierAlchemy.esp, in Cue order
		// (see tools/build_assets.py).
		constexpr RE::FormID kSoundBase = 0xB00;

		std::array<RE::BGSSoundDescriptorForm*, static_cast<std::size_t>(Cue::kCount)> g_descriptors{};
	}

	void Initialize()
	{
		const auto& config = game::Config::Current();
		if (!config.soundEnabled || config.soundVolume <= 0.0f) {
			logger::info("Sound: disabled in the INI");
			return;
		}

		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		int   found = 0;
		for (std::size_t i = 0; i < g_descriptors.size(); ++i) {
			g_descriptors[i] = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(kSoundBase + static_cast<RE::FormID>(i), game::kPluginName);
			found += g_descriptors[i] != nullptr;
		}
		logger::info("Sound: {} of {} cues found in {}", found, g_descriptors.size(), game::kPluginName);
	}

	void Play(Cue a_cue)
	{
		if (a_cue >= Cue::kCount) {
			return;
		}
		auto* descriptor = g_descriptors[static_cast<std::size_t>(a_cue)];
		if (!descriptor) {
			return;
		}
		// The audio manager belongs to the main thread; the menu draws on the render thread.
		SKSE::GetTaskInterface()->AddTask([descriptor] {
			auto* audio = RE::BSAudioManager::GetSingleton();
			if (!audio) {
				return;
			}
			RE::BSSoundHandle handle;
			if (audio->GetSoundHandle(handle, descriptor)) {
				// The UI category already follows the game's master volume.
				handle.SetVolume(game::Config::Current().soundVolume);
				handle.Play();
			}
		});
	}
}
