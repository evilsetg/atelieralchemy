#include "ui/Sound.h"

#include "game/Config.h"

#include <fstream>

// CommonLib targets Windows 7 headers. XAudio2 2.8+ is loaded at runtime below, so
// raising the target for this header alone is safe.
#pragma push_macro("_WIN32_WINNT")
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <xaudio2.h>
#pragma pop_macro("_WIN32_WINNT")

namespace ui::Sound
{
	namespace
	{
		constexpr auto        kSoundDir = "Data/SKSE/Plugins/AtelierAlchemy/sounds/";
		constexpr std::size_t kVoicesPerCue = 3;  // lets quick successive hits overlap

		constexpr std::array<std::string_view, static_cast<std::size_t>(Cue::kCount)> kFiles{
			"ring_start.wav",
			"hit.wav",
			"strike.wav",
			"miss.wav",
			"perfect.wav",
		};

		struct Clip
		{
			WAVEFORMATEX                                format{};
			std::vector<std::uint8_t>                   pcm;
			std::array<IXAudio2SourceVoice*, kVoicesPerCue> voices{};
			std::size_t                                 next{ 0 };
		};

		std::mutex                                                g_lock;
		IXAudio2*                                                 g_engine{ nullptr };
		IXAudio2MasteringVoice*                                   g_master{ nullptr };
		std::array<Clip, static_cast<std::size_t>(Cue::kCount)> g_clips;

		std::uint32_t ReadU32(const std::uint8_t* a_data) { return a_data[0] | (a_data[1] << 8) | (a_data[2] << 16) | (static_cast<std::uint32_t>(a_data[3]) << 24); }
		std::uint16_t ReadU16(const std::uint8_t* a_data) { return static_cast<std::uint16_t>(a_data[0] | (a_data[1] << 8)); }

		// Minimal RIFF/WAVE reader: uncompressed 8/16-bit PCM only.
		bool LoadWav(const std::filesystem::path& a_path, Clip& a_clip)
		{
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				logger::warn("Sound: {} not found", a_path.string());
				return false;
			}
			const std::vector<std::uint8_t> bytes{ std::istreambuf_iterator<char>(file), {} };
			if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
				logger::warn("Sound: {} is not a WAVE file", a_path.string());
				return false;
			}

			bool        haveFormat = false;
			std::size_t pos = 12;
			while (pos + 8 <= bytes.size()) {
				const auto size = ReadU32(bytes.data() + pos + 4);
				const auto body = pos + 8;
				if (body + size > bytes.size()) {
					break;
				}
				if (std::memcmp(bytes.data() + pos, "fmt ", 4) == 0 && size >= 16) {
					auto& f = a_clip.format;
					f.wFormatTag = ReadU16(bytes.data() + body);
					f.nChannels = ReadU16(bytes.data() + body + 2);
					f.nSamplesPerSec = ReadU32(bytes.data() + body + 4);
					f.nAvgBytesPerSec = ReadU32(bytes.data() + body + 8);
					f.nBlockAlign = ReadU16(bytes.data() + body + 12);
					f.wBitsPerSample = ReadU16(bytes.data() + body + 14);
					f.cbSize = 0;
					haveFormat = true;
				} else if (std::memcmp(bytes.data() + pos, "data", 4) == 0) {
					a_clip.pcm.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body), bytes.begin() + static_cast<std::ptrdiff_t>(body + size));
				}
				pos = body + size + (size & 1);
			}

			const auto& f = a_clip.format;
			if (!haveFormat || a_clip.pcm.empty() || f.wFormatTag != WAVE_FORMAT_PCM || (f.wBitsPerSample != 8 && f.wBitsPerSample != 16) ||
				f.nChannels < 1 || f.nChannels > 2) {
				logger::warn("Sound: {} must be 8 or 16-bit PCM, mono or stereo", a_path.string());
				a_clip.pcm.clear();
				return false;
			}
			return true;
		}

		float GameMasterVolume()
		{
			// The audio menu's master slider, so the cues follow the player's volume setting.
			if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
				if (auto* setting = prefs->GetSetting("fAudioMasterVolume:AudioMenu")) {
					return std::clamp(setting->GetFloat(), 0.0f, 1.0f);
				}
			}
			return 1.0f;
		}
	}

	void Initialize()
	{
		const auto& config = game::Config::Current();
		if (!config.soundEnabled || config.soundVolume <= 0.0f) {
			logger::info("Sound: disabled in the INI");
			return;
		}

		// Loaded at runtime so a missing XAudio2 2.9 (pre-Windows 10) only costs the sound.
		using Create_t = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
		HMODULE module = LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!module) {
			module = LoadLibraryExW(L"xaudio2_8.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		}
		const auto create = module ? reinterpret_cast<Create_t>(reinterpret_cast<void*>(GetProcAddress(module, "XAudio2Create"))) : nullptr;
		if (!create) {
			logger::warn("Sound: XAudio2 is not available, Judgement Ring cues are disabled");
			return;
		}

		std::scoped_lock lock(g_lock);
		if (FAILED(create(&g_engine, 0, XAUDIO2_DEFAULT_PROCESSOR)) || !g_engine) {
			logger::warn("Sound: XAudio2Create failed");
			g_engine = nullptr;
			return;
		}
		if (FAILED(g_engine->CreateMasteringVoice(&g_master)) || !g_master) {
			logger::warn("Sound: no audio output device");
			g_engine->Release();
			g_engine = nullptr;
			return;
		}

		int loaded = 0;
		for (std::size_t i = 0; i < g_clips.size(); ++i) {
			auto& clip = g_clips[i];
			if (!LoadWav(std::filesystem::path(kSoundDir) / kFiles[i], clip)) {
				continue;
			}
			for (auto& voice : clip.voices) {
				if (FAILED(g_engine->CreateSourceVoice(&voice, &clip.format))) {
					voice = nullptr;
				}
			}
			++loaded;
		}
		logger::info("Sound: XAudio2 ready, {} of {} cues loaded", loaded, g_clips.size());
	}

	void Play(Cue a_cue)
	{
		std::scoped_lock lock(g_lock);
		if (!g_engine || a_cue >= Cue::kCount) {
			return;
		}
		auto& clip = g_clips[static_cast<std::size_t>(a_cue)];
		auto* voice = clip.pcm.empty() ? nullptr : clip.voices[clip.next];
		clip.next = (clip.next + 1) % clip.voices.size();
		if (!voice) {
			return;
		}

		voice->Stop();
		voice->FlushSourceBuffers();
		voice->SetVolume(game::Config::Current().soundVolume * GameMasterVolume());

		XAUDIO2_BUFFER buffer{};
		buffer.AudioBytes = static_cast<UINT32>(clip.pcm.size());
		buffer.pAudioData = clip.pcm.data();
		buffer.Flags = XAUDIO2_END_OF_STREAM;
		if (SUCCEEDED(voice->SubmitSourceBuffer(&buffer))) {
			voice->Start();
		}
	}
}
