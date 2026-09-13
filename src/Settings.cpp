#include "Settings.h"

namespace Settings
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/SkyParkourVibrationAddon.ini"sv;

		using IniValues = std::unordered_map<std::string, std::string>;

		std::atomic<std::shared_ptr<const Values>> g_values{ std::make_shared<const Values>() };

		std::string_view Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return a_text.substr(first, last - first + 1);
		}

		std::string ToLower(std::string_view a_text)
		{
			std::string result{ a_text };
			std::ranges::transform(result, result.begin(), [](unsigned char a_char) { return static_cast<char>(std::tolower(a_char)); });
			return result;
		}

		IniValues Parse(std::istream& a_stream)
		{
			IniValues   values;
			std::string section;
			std::string line;
			while (std::getline(a_stream, line)) {
				auto text = Trim(line);
				if (const auto comment = text.find_first_of(";#"); comment != std::string_view::npos) {
					text = Trim(text.substr(0, comment));
				}
				if (text.empty()) {
					continue;
				}
				if (text.front() == '[' && text.back() == ']') {
					section = ToLower(Trim(text.substr(1, text.size() - 2)));
					continue;
				}
				const auto equals = text.find('=');
				if (equals == std::string_view::npos) {
					continue;
				}
				values[section + "." + ToLower(Trim(text.substr(0, equals)))] = std::string{ Trim(text.substr(equals + 1)) };
			}
			return values;
		}

		void Read(const IniValues& a_ini, const std::string& a_key, bool& a_value)
		{
			const auto it = a_ini.find(a_key);
			if (it == a_ini.end()) {
				return;
			}
			const auto text = ToLower(it->second);
			a_value = text == "true" || text == "1" || text == "yes" || text == "on";
		}

		void Read(const IniValues& a_ini, const std::string& a_key, float& a_value, float a_min, float a_max)
		{
			const auto it = a_ini.find(a_key);
			if (it == a_ini.end()) {
				return;
			}
			const auto& text = it->second;
			float       parsed{};
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
			if (error != std::errc{} || end != text.data() + text.size()) {
				logger::warn("{}: '{}' is not a number, keeping {}", a_key, text, a_value);
				return;
			}
			a_value = std::clamp(parsed, a_min, a_max);
		}

		void Read(const IniValues& a_ini, std::string_view a_section, Effect& a_effect)
		{
			const auto key = [&](std::string_view a_name) { return std::format("{}.{}", a_section, a_name); };
			Read(a_ini, key("benabled"), a_effect.enabled);
			Read(a_ini, key("fheavymotor"), a_effect.heavy, 0.0f, 1.0f);
			Read(a_ini, key("flightmotor"), a_effect.light, 0.0f, 1.0f);
			Read(a_ini, key("fduration"), a_effect.duration, 0.0f, 2.0f);
		}

		std::string_view ToText(bool a_value)
		{
			return a_value ? "true"sv : "false"sv;
		}

		std::string FormatEffect(std::string_view a_section, std::string_view a_comment, const Effect& a_effect)
		{
			return std::format(
				"\n[{}]\n"
				"; {}\n"
				"bEnabled = {}\n"
				"fHeavyMotor = {:.2f}\n"
				"fLightMotor = {:.2f}\n"
				"fDuration = {:.2f}\n",
				a_section, a_comment, ToText(a_effect.enabled), a_effect.heavy, a_effect.light, a_effect.duration);
		}

		// Same layout as the INI shipped in data/, so saving from the menu keeps the file readable.
		std::string Serialize(const Values& a_values)
		{
			std::string text = std::format(
				"; SkyParkour Vibration Addon - gamepad vibration for SkyParkour v3.\n"
				"; Edit in game through SKSE Menu Framework, or here: file changes apply when you load a save.\n"
				";\n"
				"; fHeavyMotor = low-frequency motor (left grip), fLightMotor = high-frequency motor (right grip).\n"
				"; Motor strength goes from 0.0 to 1.0, durations are in seconds.\n"
				"\n"
				"[General]\n"
				"bEnabled = {}\n"
				"; Multiplies every effect below (0.0 - 2.0).\n"
				"fMasterStrength = {:.2f}\n"
				"; Stay silent when vibration is turned off in the game's Controls settings.\n"
				"bRespectGameVibrationSetting = {}\n"
				"; Log every SkyParkour and jump animation event and every fired effect to\n"
				"; Documents\\My Games\\Skyrim Special Edition\\SKSE\\SkyParkourVibrationAddon.log\n"
				"bDebugLog = {}\n",
				ToText(a_values.enabled), a_values.masterStrength, ToText(a_values.respectGameSetting), ToText(a_values.debugLog));

			text += FormatEffect("PushOff", "Feet push off at the start of a climb, step or vault.", a_values.pushOff);
			text += FormatEffect("Grab", "Hands catch the ledge (or plant on the obstacle when vaulting).", a_values.grab);
			text += std::format(
				"; Strength multipliers by ledge type.\n"
				"fScaleLedgeGrab = {:.2f}\n"
				"fScaleVault = {:.2f}\n"
				"fScaleLow = {:.2f}\n"
				"fScaleMedium = {:.2f}\n"
				"fScaleHigh = {:.2f}\n"
				"fScaleHighest = {:.2f}\n",
				a_values.grabScaleLedgeGrab, a_values.grabScaleVault, a_values.grabScaleLow,
				a_values.grabScaleMedium, a_values.grabScaleHigh, a_values.grabScaleHighest);
			text += FormatEffect("Land", "Feet land after stepping up.", a_values.land);
			text += FormatEffect("Fail", "Not enough stamina: the climb fails and you hit the wall.", a_values.fail);
			text += FormatEffect("Roll", "Landing roll.", a_values.roll);
			text += FormatEffect("Slide", "Continuous vibration while sliding. fDuration is the fade-out when the slide ends.", a_values.slide);
			text += std::format(
				"; Upper limit for one slide vibration, in case the slide end is missed.\n"
				"fMaxDuration = {:.2f}\n",
				a_values.slideMaxDuration);
			text += FormatEffect("Jump", "Regular (vanilla) jump: feet push off the ground.", a_values.jump);
			text += FormatEffect("JumpLand", "Landing after a regular jump or a fall. The motor values are the strength of a high fall.", a_values.jumpLand);
			text += std::format(
				"; Fall height in game units that lands at full strength (about 70 units per meter).\n"
				"fFullStrengthFallHeight = {:.0f}\n"
				"; Share of full strength for a hop in place (0.0 - 1.0).\n"
				"fMinStrength = {:.2f}\n",
				a_values.jumpLandFullHeight, a_values.jumpLandMinStrength);
			return text;
		}
	}

	std::shared_ptr<const Values> Get()
	{
		return g_values.load();
	}

	void Load()
	{
		auto values = std::make_shared<Values>();

		std::ifstream file{ std::filesystem::path{ kPath } };
		if (!file) {
			logger::warn("{} not found, using defaults", kPath);
		} else {
			const auto ini = Parse(file);

			Read(ini, "general.benabled", values->enabled);
			Read(ini, "general.fmasterstrength", values->masterStrength, 0.0f, 2.0f);
			Read(ini, "general.brespectgamevibrationsetting", values->respectGameSetting);
			Read(ini, "general.bdebuglog", values->debugLog);

			Read(ini, "pushoff"sv, values->pushOff);
			Read(ini, "grab"sv, values->grab);
			Read(ini, "land"sv, values->land);
			Read(ini, "fail"sv, values->fail);
			Read(ini, "roll"sv, values->roll);
			Read(ini, "slide"sv, values->slide);
			Read(ini, "jump"sv, values->jump);
			Read(ini, "jumpland"sv, values->jumpLand);

			Read(ini, "grab.fscaleledgegrab", values->grabScaleLedgeGrab, 0.0f, 3.0f);
			Read(ini, "grab.fscalevault", values->grabScaleVault, 0.0f, 3.0f);
			Read(ini, "grab.fscalelow", values->grabScaleLow, 0.0f, 3.0f);
			Read(ini, "grab.fscalemedium", values->grabScaleMedium, 0.0f, 3.0f);
			Read(ini, "grab.fscalehigh", values->grabScaleHigh, 0.0f, 3.0f);
			Read(ini, "grab.fscalehighest", values->grabScaleHighest, 0.0f, 3.0f);
			Read(ini, "slide.fmaxduration", values->slideMaxDuration, 0.0f, 10.0f);
			Read(ini, "jumpland.ffullstrengthfallheight", values->jumpLandFullHeight, 50.0f, 3000.0f);
			Read(ini, "jumpland.fminstrength", values->jumpLandMinStrength, 0.0f, 1.0f);
		}

		logger::info("Settings: enabled={} master={:.2f} respectGameSetting={} debugLog={}",
			values->enabled, values->masterStrength, values->respectGameSetting, values->debugLog);
		g_values.store(std::move(values));
	}

	void Apply(const Values& a_values)
	{
		g_values.store(std::make_shared<const Values>(a_values));
	}

	bool Save(const Values& a_values)
	{
		std::ofstream file{ std::filesystem::path{ kPath }, std::ios::trunc };
		if (file) {
			file << Serialize(a_values);
		}
		if (!file) {
			logger::error("Failed to write {}", kPath);
			return false;
		}
		logger::info("Settings saved to {}", kPath);
		return true;
	}
}
