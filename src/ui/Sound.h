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

	// Looks up the cue sound descriptors in AtelierAlchemy.esp (SNDR 0xB00.., files in
	// Data/Sound/FX/AtelierAlchemy). They play through the game's own audio engine in the
	// UI category, which keeps playing while menus pause the game.
	void Initialize();

	// Fire-and-forget; safe to call from any thread. No-op if sound is unavailable.
	void Play(Cue a_cue);
}
