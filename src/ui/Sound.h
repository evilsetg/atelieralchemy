#pragma once

namespace ui::Sound
{
	enum class Cue
	{
		kRingStart,
		kHit,
		kStrike,
		kMiss,
		kPerfect,
		kCount,
	};

	// Loads the cue WAVs from Data/SKSE/Plugins/AtelierAlchemy/sounds and opens an
	// XAudio2 device of our own. The game is paused while the menu is open, so the
	// engine's sound system can't be relied on for instant feedback.
	void Initialize();

	// Fire-and-forget; safe to call from any thread. No-op if sound is unavailable.
	void Play(Cue a_cue);
}
