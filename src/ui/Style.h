#pragma once

#include "core/Types.h"

#include <imgui.h>

namespace ui
{
	// Warm "alchemist's workshop" palette.
	namespace Colors
	{
		inline constexpr ImU32 kGold = IM_COL32(232, 190, 110, 255);
		inline constexpr ImU32 kParchment = IM_COL32(240, 228, 205, 255);
		inline constexpr ImU32 kMuted = IM_COL32(170, 150, 125, 255);
		inline constexpr ImU32 kGood = IM_COL32(140, 215, 120, 255);
		inline constexpr ImU32 kBad = IM_COL32(230, 100, 85, 255);
		inline constexpr ImU32 kPotion = IM_COL32(220, 120, 140, 255);
		inline constexpr ImU32 kPoison = IM_COL32(150, 200, 90, 255);
		inline constexpr ImU32 kMaterial = IM_COL32(180, 160, 230, 255);
		inline constexpr ImU32 kRingBase = IM_COL32(35, 28, 24, 235);
		inline constexpr ImU32 kRingTrack = IM_COL32(95, 78, 60, 255);
		inline constexpr ImU32 kHitArea = IM_COL32(235, 160, 60, 255);
		inline constexpr ImU32 kStrikeArea = IM_COL32(220, 50, 45, 255);
		inline constexpr ImU32 kNeedle = IM_COL32(255, 245, 225, 255);
	}

	void        ApplyStyle(float a_scale);
	ImU32       ElementColor(atelier::Element a_element);
	ImU32       KindColor(atelier::OutputKind a_kind);
	ImVec4      ToVec4(ImU32 a_color);
	const char* KindLabel(atelier::OutputKind a_kind);
}
