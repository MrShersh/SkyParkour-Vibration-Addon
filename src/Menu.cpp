#include "Menu.h"

#include "Settings.h"
#include "Vibration.h"

#pragma warning(push, 0)
#include <SKSEMenuFramework.h>
#pragma warning(pop)

namespace Menu
{
	namespace
	{
		constexpr auto  kSection = "SkyParkour Vibration Addon";
		constexpr auto  kTranslationName = "SkyParkourVibrationAddon";
		constexpr auto  kFrameworkIni = "Data/SKSE/Plugins/SKSEMenuFramework.ini";
		constexpr float kSlidePreviewDuration = 1.0f;

		const ImGuiMCP::ImVec4 kWarningColor{ 1.0f, 0.75f, 0.25f, 1.0f };

		struct EffectText
		{
			std::string title;
			std::string description;
		};

		struct Text
		{
			std::string settings;
			std::string save;
			std::string revert;
			std::string defaults;
			std::string statusSaved;
			std::string statusSaveFailed;
			std::string statusReverted;
			std::string statusDefaults;
			std::string unsaved;
			std::string warnHooks;
			std::string warnGameVibration;
			std::string general;
			std::string enabled;
			std::string masterStrength;
			std::string followGame;
			std::string debugLog;
			std::string heavyMotor;
			std::string lightMotor;
			std::string duration;
			std::string fadeOut;
			std::string test;
			EffectText  pushOff;
			EffectText  grab;
			EffectText  land;
			EffectText  fail;
			EffectText  roll;
			EffectText  slide;
			EffectText  jump;
			EffectText  jumpLand;
			std::string grabScaleInfo;
			std::string scaleLedgeGrab;
			std::string scaleVault;
			std::string scaleLow;
			std::string scaleMedium;
			std::string scaleHigh;
			std::string scaleHighest;
			std::string slideLimit;
			std::string jumpFullHeight;
			std::string jumpMinStrength;
		};

		Text             g_text;
		Settings::Values g_edit;
		std::atomic_bool g_needsSync{ true };
		std::atomic_bool g_unsaved{ false };
		std::string      g_status;

		std::string GameLanguage()
		{
			const auto ini = RE::INISettingCollection::GetSingleton();
			const auto setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
			if (setting && setting->GetType() == RE::Setting::Type::kString && setting->data.s) {
				return setting->data.s;
			}
			return "ENGLISH";
		}

		// SKSE Menu Framework draws Cyrillic only with EnableCyrillic = true; otherwise every Russian letter becomes "?".
		bool FrameworkDrawsCyrillic()
		{
			std::ifstream file{ std::filesystem::path{ kFrameworkIni } };
			std::string   line;
			while (std::getline(file, line)) {
				const auto equals = line.find('=');
				if (line.starts_with(';') || equals == std::string::npos || line.substr(0, equals).find("EnableCyrillic") == std::string::npos) {
					continue;
				}
				auto value = line.substr(equals + 1);
				std::ranges::transform(value, value.begin(), [](unsigned char a_char) { return static_cast<char>(std::tolower(a_char)); });
				return value.find("true") != std::string::npos || value.find('1') != std::string::npos;
			}
			return false;
		}

		void LoadText()
		{
			const auto language = GameLanguage();
			bool       translate = true;
			if (_stricmp(language.c_str(), "RUSSIAN") == 0 && !FrameworkDrawsCyrillic()) {
				translate = false;
				logger::info("SKSE Menu Framework has EnableCyrillic = false, the settings page stays in English");
			}
			if (translate) {
				SKSE::Translation::ParseTranslation(kTranslationName);
			}

			const auto tr = [translate](const char* a_key, const char* a_english) {
				std::string result{ a_english };
				if (translate) {
					SKSE::Translation::Translate(a_key, result);
				}
				return result;
			};

			g_text.settings = tr("$SPV_Settings", "Settings");
			g_text.save = tr("$SPV_Save", "Save");
			g_text.revert = tr("$SPV_Revert", "Revert");
			g_text.defaults = tr("$SPV_Defaults", "Defaults");
			g_text.statusSaved = tr("$SPV_StatusSaved", "Saved to SkyParkourVibrationAddon.ini");
			g_text.statusSaveFailed = tr("$SPV_StatusSaveFailed", "Could not write SkyParkourVibrationAddon.ini");
			g_text.statusReverted = tr("$SPV_StatusReverted", "Reloaded from the INI");
			g_text.statusDefaults = tr("$SPV_StatusDefaults", "Defaults applied, not saved yet");
			g_text.unsaved = tr("$SPV_Unsaved", "Changes already work in game. Save them to keep them after loading a save or restarting.");
			g_text.warnHooks = tr("$SPV_WarnHooks", "XInput hooks are not installed, see SkyParkourVibrationAddon.log.");
			g_text.warnGameVibration = tr("$SPV_WarnGameVibration", "Vibration is turned off in the game's Controls settings, so every effect is muted.");
			g_text.general = tr("$SPV_General", "General");
			g_text.enabled = tr("$SPV_Enabled", "Enabled");
			g_text.masterStrength = tr("$SPV_MasterStrength", "Master strength");
			g_text.followGame = tr("$SPV_FollowGame", "Follow the game's Vibration setting");
			g_text.debugLog = tr("$SPV_DebugLog", "Debug log");
			g_text.heavyMotor = tr("$SPV_HeavyMotor", "Heavy motor");
			g_text.lightMotor = tr("$SPV_LightMotor", "Light motor");
			g_text.duration = tr("$SPV_Duration", "Duration (s)");
			g_text.fadeOut = tr("$SPV_FadeOut", "Fade-out (s)");
			g_text.test = tr("$SPV_Test", "Test");
			g_text.pushOff = { tr("$SPV_PushOff", "Push-off"), tr("$SPV_PushOffInfo", "Feet push off at the start of a climb, step or vault.") };
			g_text.grab = { tr("$SPV_Grab", "Grab"), tr("$SPV_GrabInfo", "Hands catch the ledge, or plant on the obstacle when vaulting.") };
			g_text.land = { tr("$SPV_Land", "Step landing"), tr("$SPV_LandInfo", "Feet land after stepping up.") };
			g_text.fail = { tr("$SPV_Fail", "Failed climb"), tr("$SPV_FailInfo", "Not enough stamina: the climb fails and you hit the wall.") };
			g_text.roll = { tr("$SPV_Roll", "Landing roll"), tr("$SPV_RollInfo", "Rolling out of a fall.") };
			g_text.slide = { tr("$SPV_Slide", "Slide"), tr("$SPV_SlideInfo", "Continuous vibration while sliding. Test plays one second of it.") };
			g_text.jump = { tr("$SPV_Jump", "Jump"), tr("$SPV_JumpInfo", "Regular jump: feet push off the ground.") };
			g_text.jumpLand = { tr("$SPV_JumpLand", "Jump landing"), tr("$SPV_JumpLandInfo", "Landing after a regular jump or a fall. The higher the fall, the stronger it is; Test plays full strength.") };
			g_text.grabScaleInfo = tr("$SPV_GrabScaleInfo", "Grab strength by ledge type:");
			g_text.scaleLedgeGrab = tr("$SPV_ScaleLedgeGrab", "Mid-air ledge grab");
			g_text.scaleVault = tr("$SPV_ScaleVault", "Vault");
			g_text.scaleLow = tr("$SPV_ScaleLow", "Low ledge");
			g_text.scaleMedium = tr("$SPV_ScaleMedium", "Medium ledge");
			g_text.scaleHigh = tr("$SPV_ScaleHigh", "High ledge");
			g_text.scaleHighest = tr("$SPV_ScaleHighest", "Highest ledge");
			g_text.slideLimit = tr("$SPV_SlideLimit", "Longest slide (s)");
			g_text.jumpFullHeight = tr("$SPV_JumpFullHeight", "Fall height for full strength (units, ~70 per meter)");
			g_text.jumpMinStrength = tr("$SPV_JumpMinStrength", "Strength of a hop in place");
		}

		bool Slider(const std::string& a_label, float& a_value, float a_min, float a_max, const char* a_format = "%.2f")
		{
			return ImGuiMCP::SliderFloat(a_label.c_str(), &a_value, a_min, a_max, a_format);
		}

		void Preview(const Settings::Effect& a_effect, float a_duration)
		{
			const auto strength = g_edit.masterStrength;
			Vibration::Pulse(a_effect.heavy * strength, a_effect.light * strength, a_duration, true);
		}

		// Widget IDs come from a fixed ASCII id, not the translated title, so they stay unique whatever the language.
		bool EffectSection(const char* a_id, const EffectText& a_text, Settings::Effect& a_effect,
			const std::string& a_durationLabel, float a_previewDuration = 0.0f)
		{
			ImGuiMCP::PushID(a_id);
			ImGuiMCP::SeparatorText(a_text.title.c_str());
			ImGuiMCP::TextDisabled("%s", a_text.description.c_str());

			bool changed = ImGuiMCP::Checkbox(g_text.enabled.c_str(), &a_effect.enabled);
			ImGuiMCP::BeginDisabled(!a_effect.enabled);
			changed |= Slider(g_text.heavyMotor, a_effect.heavy, 0.0f, 1.0f);
			changed |= Slider(g_text.lightMotor, a_effect.light, 0.0f, 1.0f);
			changed |= Slider(a_durationLabel, a_effect.duration, 0.0f, 2.0f);
			if (ImGuiMCP::Button(g_text.test.c_str())) {
				Preview(a_effect, a_previewDuration > 0.0f ? a_previewDuration : a_effect.duration);
			}
			ImGuiMCP::EndDisabled();

			ImGuiMCP::PopID();
			return changed;
		}

		void __stdcall Render()
		{
			if (g_needsSync.exchange(false)) {
				g_edit = *Settings::Get();
			}

			if (!Vibration::IsInstalled()) {
				ImGuiMCP::TextColored(kWarningColor, "%s", g_text.warnHooks.c_str());
			}
			if (g_edit.respectGameSetting && !Vibration::GameVibrationEnabled()) {
				ImGuiMCP::TextColored(kWarningColor, "%s", g_text.warnGameVibration.c_str());
			}

			bool changed = false;

			if (ImGuiMCP::Button(g_text.save.c_str())) {
				if (Settings::Save(g_edit)) {
					g_unsaved = false;
					g_status = g_text.statusSaved;
				} else {
					g_status = g_text.statusSaveFailed;
				}
			}
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(g_text.revert.c_str())) {
				Settings::Load();
				g_edit = *Settings::Get();
				g_unsaved = false;
				g_status = g_text.statusReverted;
			}
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(g_text.defaults.c_str())) {
				g_edit = {};
				changed = true;
				g_status = g_text.statusDefaults;
			}
			if (!g_status.empty()) {
				ImGuiMCP::SameLine();
				ImGuiMCP::TextDisabled("%s", g_status.c_str());
			}
			if (g_unsaved) {
				ImGuiMCP::TextDisabled("%s", g_text.unsaved.c_str());
			}

			ImGuiMCP::PushID("General");
			ImGuiMCP::SeparatorText(g_text.general.c_str());
			changed |= ImGuiMCP::Checkbox(g_text.enabled.c_str(), &g_edit.enabled);
			changed |= Slider(g_text.masterStrength, g_edit.masterStrength, 0.0f, 2.0f);
			changed |= ImGuiMCP::Checkbox(g_text.followGame.c_str(), &g_edit.respectGameSetting);
			changed |= ImGuiMCP::Checkbox(g_text.debugLog.c_str(), &g_edit.debugLog);
			ImGuiMCP::PopID();

			changed |= EffectSection("PushOff", g_text.pushOff, g_edit.pushOff, g_text.duration);

			changed |= EffectSection("Grab", g_text.grab, g_edit.grab, g_text.duration);
			ImGuiMCP::PushID("GrabScale");
			ImGuiMCP::TextDisabled("%s", g_text.grabScaleInfo.c_str());
			changed |= Slider(g_text.scaleLedgeGrab, g_edit.grabScaleLedgeGrab, 0.0f, 3.0f);
			changed |= Slider(g_text.scaleVault, g_edit.grabScaleVault, 0.0f, 3.0f);
			changed |= Slider(g_text.scaleLow, g_edit.grabScaleLow, 0.0f, 3.0f);
			changed |= Slider(g_text.scaleMedium, g_edit.grabScaleMedium, 0.0f, 3.0f);
			changed |= Slider(g_text.scaleHigh, g_edit.grabScaleHigh, 0.0f, 3.0f);
			changed |= Slider(g_text.scaleHighest, g_edit.grabScaleHighest, 0.0f, 3.0f);
			ImGuiMCP::PopID();

			changed |= EffectSection("Land", g_text.land, g_edit.land, g_text.duration);
			changed |= EffectSection("Fail", g_text.fail, g_edit.fail, g_text.duration);
			changed |= EffectSection("Roll", g_text.roll, g_edit.roll, g_text.duration);

			changed |= EffectSection("Slide", g_text.slide, g_edit.slide, g_text.fadeOut, kSlidePreviewDuration);
			ImGuiMCP::PushID("SlideLimit");
			changed |= Slider(g_text.slideLimit, g_edit.slideMaxDuration, 0.0f, 10.0f);
			ImGuiMCP::PopID();

			changed |= EffectSection("Jump", g_text.jump, g_edit.jump, g_text.duration);

			changed |= EffectSection("JumpLand", g_text.jumpLand, g_edit.jumpLand, g_text.duration);
			ImGuiMCP::PushID("JumpLandScale");
			changed |= Slider(g_text.jumpFullHeight, g_edit.jumpLandFullHeight, 50.0f, 3000.0f, "%.0f");
			changed |= Slider(g_text.jumpMinStrength, g_edit.jumpLandMinStrength, 0.0f, 1.0f);
			ImGuiMCP::PopID();

			if (changed) {
				Settings::Apply(g_edit);
				g_unsaved = true;
			}
		}

		void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_event)
		{
			if (a_event == SKSEMenuFramework::Model::EventType::kOpenMenu) {
				g_needsSync = true;
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework is not installed, settings are INI-only");
			return;
		}
		LoadText();
		SKSEMenuFramework::SetSection(kSection);
		SKSEMenuFramework::AddSectionItem(g_text.settings, Render);
		SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0f);
		logger::info("Settings page registered in SKSE Menu Framework");
	}

	void Refresh()
	{
		g_unsaved = false;
		g_needsSync = true;
	}
}
