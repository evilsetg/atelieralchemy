#include "ui/Style.h"

namespace ui
{
	void ApplyStyle(float a_scale)
	{
		auto& style = ImGui::GetStyle();
		ImGui::StyleColorsDark(&style);

		style.WindowRounding = 10.0f;
		style.ChildRounding = 8.0f;
		style.FrameRounding = 6.0f;
		style.PopupRounding = 8.0f;
		style.GrabRounding = 6.0f;
		style.TabRounding = 6.0f;
		style.ScrollbarRounding = 8.0f;
		style.WindowBorderSize = 2.0f;
		style.ChildBorderSize = 1.0f;
		style.FrameBorderSize = 1.0f;
		style.WindowPadding = { 16.0f, 14.0f };
		style.FramePadding = { 10.0f, 6.0f };
		style.ItemSpacing = { 10.0f, 8.0f };
		style.ScaleAllSizes(a_scale);

		auto* c = style.Colors;
		c[ImGuiCol_Text] = ToVec4(Colors::kParchment);
		c[ImGuiCol_TextDisabled] = ToVec4(Colors::kMuted);
		c[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.10f, 0.08f, 0.96f);
		c[ImGuiCol_ChildBg] = ImVec4(0.17f, 0.14f, 0.11f, 0.85f);
		c[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.11f, 0.09f, 0.98f);
		c[ImGuiCol_Border] = ImVec4(0.60f, 0.46f, 0.28f, 0.70f);
		c[ImGuiCol_FrameBg] = ImVec4(0.24f, 0.19f, 0.15f, 1.00f);
		c[ImGuiCol_FrameBgHovered] = ImVec4(0.34f, 0.26f, 0.18f, 1.00f);
		c[ImGuiCol_FrameBgActive] = ImVec4(0.42f, 0.31f, 0.20f, 1.00f);
		c[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.15f, 0.10f, 1.00f);
		c[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.21f, 0.12f, 1.00f);
		c[ImGuiCol_Button] = ImVec4(0.43f, 0.28f, 0.14f, 1.00f);
		c[ImGuiCol_ButtonHovered] = ImVec4(0.62f, 0.41f, 0.20f, 1.00f);
		c[ImGuiCol_ButtonActive] = ImVec4(0.75f, 0.52f, 0.25f, 1.00f);
		c[ImGuiCol_Header] = ImVec4(0.47f, 0.31f, 0.16f, 0.80f);
		c[ImGuiCol_HeaderHovered] = ImVec4(0.60f, 0.40f, 0.20f, 0.90f);
		c[ImGuiCol_HeaderActive] = ImVec4(0.70f, 0.48f, 0.24f, 1.00f);
		c[ImGuiCol_CheckMark] = ToVec4(Colors::kGold);
		c[ImGuiCol_SliderGrab] = ToVec4(Colors::kGold);
		c[ImGuiCol_Separator] = ImVec4(0.60f, 0.46f, 0.28f, 0.50f);
		c[ImGuiCol_PlotHistogram] = ToVec4(Colors::kGold);
		c[ImGuiCol_NavCursor] = ToVec4(Colors::kGold);
		c[ImGuiCol_ScrollbarGrab] = ImVec4(0.45f, 0.34f, 0.22f, 1.00f);
		c[ImGuiCol_TableHeaderBg] = ImVec4(0.26f, 0.20f, 0.14f, 1.00f);
		c[ImGuiCol_TableBorderLight] = ImVec4(0.40f, 0.31f, 0.20f, 0.60f);
		c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 0.9f, 0.7f, 0.04f);
	}

	ImU32 ElementColor(atelier::Element a_element)
	{
		switch (a_element) {
		case atelier::Element::kFire:
			return IM_COL32(235, 95, 60, 255);
		case atelier::Element::kIce:
			return IM_COL32(95, 175, 245, 255);
		case atelier::Element::kLightning:
			return IM_COL32(245, 210, 70, 255);
		case atelier::Element::kWind:
			return IM_COL32(110, 215, 135, 255);
		}
		return Colors::kParchment;
	}

	ImU32 KindColor(atelier::OutputKind a_kind)
	{
		switch (a_kind) {
		case atelier::OutputKind::kPotion:
			return Colors::kPotion;
		case atelier::OutputKind::kPoison:
			return Colors::kPoison;
		case atelier::OutputKind::kMaterial:
			return Colors::kMaterial;
		}
		return Colors::kParchment;
	}

	const char* KindLabel(atelier::OutputKind a_kind)
	{
		switch (a_kind) {
		case atelier::OutputKind::kPotion:
			return "Potion";
		case atelier::OutputKind::kPoison:
			return "Poison";
		case atelier::OutputKind::kMaterial:
			return "Material";
		}
		return "";
	}

	ImVec4 ToVec4(ImU32 a_color)
	{
		return ImGui::ColorConvertU32ToFloat4(a_color);
	}
}
