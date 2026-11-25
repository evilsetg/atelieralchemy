#pragma once

namespace game
{
	enum class RingMode
	{
		kNormal,
		kEasy,  // slower needle, wider areas
		kAuto,  // never fails, never strikes
	};

	struct Config
	{
		// [General]
		std::uint32_t hotkeyKeyboard{ 0x25 };  // DIK_K
		std::uint32_t hotkeyGamepad{ 0 };      // XInput mask, 0 = disabled
		bool          requireAlchemyLab{ true };
		float         labSearchRadius{ 450.0f };
		bool          showCraftingHint{ true };

		// [Ring]
		RingMode ringMode{ RingMode::kNormal };
		float    ringSpeedMult{ 1.0f };
		float    ringWidthMult{ 1.0f };
		bool     missEndsRing{ true };

		// [Balance]
		float magnitudeMult{ 1.0f };
		float xpMult{ 1.0f };
		int   baseTraitSlots{ 3 };
		bool  usePerks{ true };
		float ingredientQualityMult{ 1.0f };
		float traitChance{ 0.35f };

		// [Sound]
		bool  soundEnabled{ true };
		float soundVolume{ 0.8f };

		// [UI]
		std::string fontPath{ "Data/SKSE/Plugins/AtelierAlchemy/fonts/AlegreyaSans-Regular.ttf" };
		float       fontSize{ 0.0f };  // 0 = scale with resolution
		float       uiScale{ 1.0f };

		static Config&       Get();
		static const Config& Current() { return Get(); }
		void                 Load();
	};
}
