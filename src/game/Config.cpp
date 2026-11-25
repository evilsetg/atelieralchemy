#include "game/Config.h"

#include <fstream>

namespace game
{
	namespace
	{
		constexpr auto kIniPath = "Data/SKSE/Plugins/AtelierAlchemy.ini";

		std::string Trim(std::string_view a_str)
		{
			const auto begin = a_str.find_first_not_of(" \t\r\n");
			if (begin == std::string_view::npos) {
				return {};
			}
			const auto end = a_str.find_last_not_of(" \t\r\n");
			return std::string(a_str.substr(begin, end - begin + 1));
		}

		std::string Lower(std::string a_str)
		{
			std::ranges::transform(a_str, a_str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_str;
		}

		bool ParseBool(const std::string& a_value)
		{
			const auto v = Lower(a_value);
			return v == "1" || v == "true" || v == "yes" || v == "on";
		}

		std::uint32_t ParseUInt(const std::string& a_value)
		{
			return static_cast<std::uint32_t>(std::stoul(a_value, nullptr, 0));
		}
	}

	Config& Config::Get()
	{
		static Config config;
		return config;
	}

	void Config::Load()
	{
		std::ifstream file(kIniPath);
		if (!file) {
			logger::warn("{} not found, using defaults", kIniPath);
			return;
		}

		std::string section;
		std::string line;
		while (std::getline(file, line)) {
			auto trimmed = Trim(line);
			if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
				continue;
			}
			if (trimmed.front() == '[' && trimmed.back() == ']') {
				section = Lower(trimmed.substr(1, trimmed.size() - 2));
				continue;
			}
			const auto eq = trimmed.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = Lower(Trim(std::string_view(trimmed).substr(0, eq)));
			auto       value = Trim(std::string_view(trimmed).substr(eq + 1));
			if (const auto comment = value.find(';'); comment != std::string::npos) {
				value = Trim(std::string_view(value).substr(0, comment));
			}

			try {
				if (section == "general") {
					if (key == "hotkeykeyboard") {
						hotkeyKeyboard = ParseUInt(value);
					} else if (key == "hotkeygamepad") {
						hotkeyGamepad = ParseUInt(value);
					} else if (key == "requirealchemylab") {
						requireAlchemyLab = ParseBool(value);
					} else if (key == "labsearchradius") {
						labSearchRadius = std::stof(value);
					} else if (key == "showcraftinghint") {
						showCraftingHint = ParseBool(value);
					}
				} else if (section == "ring") {
					if (key == "mode") {
						const auto v = Lower(value);
						ringMode = v == "auto" ? RingMode::kAuto : v == "easy" ? RingMode::kEasy : RingMode::kNormal;
					} else if (key == "speedmultiplier") {
						ringSpeedMult = std::clamp(std::stof(value), 0.2f, 3.0f);
					} else if (key == "widthmultiplier") {
						ringWidthMult = std::clamp(std::stof(value), 0.2f, 3.0f);
					} else if (key == "missendsring") {
						missEndsRing = ParseBool(value);
					}
				} else if (section == "balance") {
					if (key == "magnitudemultiplier") {
						magnitudeMult = std::max(std::stof(value), 0.0f);
					} else if (key == "valuemultiplier") {
						valueMult = std::max(std::stof(value), 0.0f);
					} else if (key == "xpmultiplier") {
						xpMult = std::max(std::stof(value), 0.0f);
					} else if (key == "basetraitslots") {
						baseTraitSlots = std::clamp(std::stoi(value), 0, 5);
					} else if (key == "useperks") {
						usePerks = ParseBool(value);
					} else if (key == "ingredientqualitymultiplier") {
						ingredientQualityMult = std::max(std::stof(value), 0.0f);
					} else if (key == "traitchance") {
						traitChance = std::clamp(std::stof(value), 0.0f, 1.0f);
					}
				} else if (section == "sound") {
					if (key == "enabled") {
						soundEnabled = ParseBool(value);
					} else if (key == "volume") {
						soundVolume = std::clamp(std::stof(value), 0.0f, 1.0f);
					}
				} else if (section == "ui") {
					if (key == "fontpath") {
						fontPath = value;
					} else if (key == "fontsize") {
						fontSize = std::max(std::stof(value), 0.0f);
					} else if (key == "scale") {
						uiScale = std::clamp(std::stof(value), 0.5f, 3.0f);
					}
				}
			} catch (const std::exception& e) {
				logger::warn("Bad value for [{}] {} = '{}': {}", section, key, value, e.what());
			}
		}
		logger::info("Loaded {}", kIniPath);
	}

	void Config::SaveRingMode() const
	{
		const char* value = ringMode == RingMode::kAuto ? "Auto" : ringMode == RingMode::kEasy ? "Easy" : "Normal";

		std::vector<std::string> lines;
		{
			std::ifstream in(kIniPath);
			for (std::string line; std::getline(in, line);) {
				lines.push_back(line);
			}
		}
		std::string section;
		bool        written = false;
		std::size_t ringEnd = lines.size();  // where to add the key if [Ring] lacks it
		bool        hasRing = false;
		for (std::size_t i = 0; i < lines.size(); ++i) {
			const auto trimmed = Trim(lines[i]);
			if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
				if (section == "ring") {
					ringEnd = i;
				}
				section = Lower(trimmed.substr(1, trimmed.size() - 2));
				hasRing = hasRing || section == "ring";
				continue;
			}
			const auto eq = trimmed.find('=');
			if (section == "ring" && eq != std::string::npos && Lower(Trim(std::string_view(trimmed).substr(0, eq))) == "mode") {
				lines[i] = std::format("Mode = {}", value);
				written = true;
			}
		}
		if (!written) {
			if (!hasRing) {
				lines.emplace_back("");
				lines.emplace_back("[Ring]");
				ringEnd = lines.size();
			}
			lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(ringEnd), std::format("Mode = {}", value));
		}

		std::ofstream out(kIniPath, std::ios::trunc);
		if (!out) {
			logger::warn("Could not write {}", kIniPath);
			return;
		}
		for (const auto& line : lines) {
			out << line << '\n';
		}
		logger::info("Ring mode set to {}", value);
	}
}
