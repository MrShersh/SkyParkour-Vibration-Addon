#include "Vibration.h"

#include "Settings.h"

namespace Vibration
{
	namespace
	{
		using XInputGetState_t = std::uint32_t(std::uint32_t a_user, REX::W32::XINPUT_STATE* a_state);
		using XInputSetState_t = std::uint32_t(std::uint32_t a_user, REX::W32::XINPUT_VIBRATION* a_vibration);

		constexpr auto          kXInputModule = "xinput1_3.dll"sv;
		constexpr std::uint16_t kOrdinalGetState = 2;
		constexpr std::uint16_t kOrdinalSetState = 3;
		constexpr std::uint32_t kErrorSuccess = 0;

		// PE constants spelled out: windows.h defines macros with the same names as REX's.
		constexpr std::size_t   kImportDirectory = 1;
		constexpr std::uint64_t kImportByOrdinalFlag = 1ull << 63;

		constexpr std::uint32_t kMaxUsers = 4;
		constexpr std::size_t   kMaxPulses = 8;
		constexpr float         kPulseHold = 0.4f;
		constexpr float         kMaxFrameTime = 0.1f;
		constexpr float         kSustainTimeoutFade = 0.25f;
		constexpr auto          kGameSettingPollInterval = 1s;

		struct ActivePulse
		{
			float heavy;
			float light;
			float duration;
			float elapsed;
			bool  preview;
		};

		struct Sustain
		{
			float heavy{ 0.0f };
			float light{ 0.0f };
			float level{ 0.0f };
			float fadeRate{ 0.0f };
			float timeLeft{ 0.0f };
		};

		XInputGetState_t* g_getState{ nullptr };
		XInputSetState_t* g_setState{ nullptr };
		std::atomic_bool  g_installed{ false };
		std::atomic_bool  g_gameVibrationEnabled{ true };

		std::mutex                            g_lock;
		std::array<ActivePulse, kMaxPulses>   g_pulses{};
		std::size_t                           g_pulseCount{ 0 };
		Sustain                               g_sustain;
		float                                 g_heavy{ 0.0f };
		float                                 g_light{ 0.0f };
		std::chrono::steady_clock::time_point g_lastAdvance{};
		std::chrono::steady_clock::time_point g_lastSettingPoll{};

		std::array<REX::W32::XINPUT_VIBRATION, kMaxUsers> g_game{};
		std::array<REX::W32::XINPUT_VIBRATION, kMaxUsers> g_written{};

		bool ReadGameVibrationSetting()
		{
			constexpr auto kSetting = "bGamePadRumble:Controls"sv;

			RE::Setting* setting = nullptr;
			if (const auto prefs = RE::INIPrefSettingCollection::GetSingleton()) {
				setting = prefs->GetSetting(kSetting);
			}
			if (!setting) {
				if (const auto ini = RE::INISettingCollection::GetSingleton()) {
					setting = ini->GetSetting(kSetting);
				}
			}
			return !setting || setting->GetBool();
		}

		void ClearEffects()
		{
			g_pulseCount = 0;
			g_sustain = {};
			g_heavy = 0.0f;
			g_light = 0.0f;
		}

		void DropGameplayEffects()
		{
			const auto end = std::remove_if(g_pulses.begin(), g_pulses.begin() + g_pulseCount, [](const ActivePulse& a_pulse) { return !a_pulse.preview; });
			g_pulseCount = static_cast<std::size_t>(end - g_pulses.begin());
			g_sustain = {};
		}

		void Advance(float a_dt)
		{
			float heavy = 0.0f;
			float light = 0.0f;

			std::size_t kept = 0;
			for (std::size_t i = 0; i < g_pulseCount; ++i) {
				auto       pulse = g_pulses[i];
				const auto progress = pulse.elapsed / pulse.duration;
				const auto envelope = progress < kPulseHold ? 1.0f : 1.0f - (progress - kPulseHold) / (1.0f - kPulseHold);
				heavy = std::max(heavy, pulse.heavy * envelope);
				light = std::max(light, pulse.light * envelope);

				pulse.elapsed += a_dt;
				if (pulse.elapsed < pulse.duration) {
					g_pulses[kept++] = pulse;
				}
			}
			g_pulseCount = kept;

			if (g_sustain.level > 0.0f) {
				heavy = std::max(heavy, g_sustain.heavy * g_sustain.level);
				light = std::max(light, g_sustain.light * g_sustain.level);

				if (g_sustain.fadeRate == 0.0f) {
					g_sustain.timeLeft -= a_dt;
					if (g_sustain.timeLeft <= 0.0f) {
						g_sustain.fadeRate = 1.0f / kSustainTimeoutFade;
					}
				} else {
					g_sustain.level = std::max(0.0f, g_sustain.level - g_sustain.fadeRate * a_dt);
				}
			}

			g_heavy = heavy;
			g_light = light;
		}

		std::uint16_t ToMotorSpeed(float a_level)
		{
			return static_cast<std::uint16_t>(std::clamp(a_level, 0.0f, 1.0f) * 65535.0f + 0.5f);
		}

		REX::W32::XINPUT_VIBRATION Mix(std::uint32_t a_user)
		{
			const auto& game = g_game[a_user];
			return {
				std::max(game.leftMotorSpeed, ToMotorSpeed(g_heavy)),
				std::max(game.rightMotorSpeed, ToMotorSpeed(g_light))
			};
		}

		bool operator==(const REX::W32::XINPUT_VIBRATION& a_lhs, const REX::W32::XINPUT_VIBRATION& a_rhs)
		{
			return a_lhs.leftMotorSpeed == a_rhs.leftMotorSpeed && a_lhs.rightMotorSpeed == a_rhs.rightMotorSpeed;
		}

		// Driven by the game's own controller polling: runs every frame on the main thread, menus included.
		void Tick(std::uint32_t a_user)
		{
			REX::W32::XINPUT_VIBRATION output;
			{
				std::scoped_lock lock{ g_lock };

				const auto now = std::chrono::steady_clock::now();
				if (now - g_lastSettingPoll >= kGameSettingPollInterval) {
					g_lastSettingPoll = now;
					g_gameVibrationEnabled = ReadGameVibrationSetting();
				}

				// The game may poll several controllers per frame: advance effects once, mix per controller.
				const auto dt = std::chrono::duration<float>(now - g_lastAdvance).count();
				if (dt > 0.001f) {
					g_lastAdvance = now;

					const auto settings = Settings::Get();
					if (settings->respectGameSetting && !g_gameVibrationEnabled) {
						ClearEffects();
					} else {
						const auto ui = RE::UI::GetSingleton();
						if (!settings->enabled || (ui && ui->GameIsPaused())) {
							DropGameplayEffects();
						}
						Advance(std::min(dt, kMaxFrameTime));
					}
				}

				output = Mix(a_user);
				if (output == g_written[a_user]) {
					return;
				}
				g_written[a_user] = output;
			}
			g_setState(a_user, &output);
		}

		std::uint32_t GetStateHook(std::uint32_t a_user, REX::W32::XINPUT_STATE* a_state)
		{
			const auto result = g_getState(a_user, a_state);
			if (result == kErrorSuccess && a_user < kMaxUsers) {
				Tick(a_user);
			}
			return result;
		}

		std::uint32_t SetStateHook(std::uint32_t a_user, REX::W32::XINPUT_VIBRATION* a_vibration)
		{
			if (!a_vibration || a_user >= kMaxUsers) {
				return g_setState(a_user, a_vibration);
			}

			REX::W32::XINPUT_VIBRATION output;
			{
				std::scoped_lock lock{ g_lock };
				g_game[a_user] = *a_vibration;
				output = Mix(a_user);
				g_written[a_user] = output;
			}
			return g_setState(a_user, &output);
		}

		// SKSE::PatchIAT only matches imports by name, but SkyrimSE.exe imports XInput by ordinal.
		// Prefer the slot holding the real export (covers both), fall back to the ordinal.
		std::uintptr_t FindImportSlot(std::uintptr_t a_export, std::uint16_t a_ordinal)
		{
			const auto  base = REL::Module::get().base();
			const auto  dos = reinterpret_cast<const REX::W32::IMAGE_DOS_HEADER*>(base);
			const auto  nt = reinterpret_cast<const REX::W32::IMAGE_NT_HEADERS64*>(base + dos->lfanew);
			const auto& directory = nt->optionalHeader.dataDirectory[kImportDirectory];

			for (auto descriptor = reinterpret_cast<const REX::W32::IMAGE_IMPORT_DESCRIPTOR*>(base + directory.virtualAddress);
				 descriptor->characteristics != 0;
				 ++descriptor) {
				if (_stricmp(reinterpret_cast<const char*>(base + descriptor->name), kXInputModule.data()) != 0) {
					continue;
				}

				const auto iat = reinterpret_cast<REX::W32::IMAGE_THUNK_DATA64*>(base + descriptor->firstThunk);
				const auto lookup = reinterpret_cast<const REX::W32::IMAGE_THUNK_DATA64*>(base + descriptor->firstThunkOriginal);

				for (std::size_t i = 0; iat[i].function != 0; ++i) {
					if (iat[i].function == a_export) {
						return reinterpret_cast<std::uintptr_t>(&iat[i].function);
					}
				}
				for (std::size_t i = 0; lookup[i].ordinal != 0; ++i) {
					if ((lookup[i].ordinal & kImportByOrdinalFlag) != 0 && (lookup[i].ordinal & 0xFFFF) == a_ordinal) {
						return reinterpret_cast<std::uintptr_t>(&iat[i].function);
					}
				}
			}
			return 0;
		}

		template <class F>
		bool PatchImport(const char* a_name, std::uint16_t a_ordinal, F* a_hook, F*& a_original)
		{
			const auto module = REX::W32::GetModuleHandleW(L"xinput1_3.dll");
			const auto exported = module ? reinterpret_cast<std::uintptr_t>(REX::W32::GetProcAddress(module, a_name)) : 0;

			const auto slot = FindImportSlot(exported, a_ordinal);
			if (!slot) {
				logger::error("{} not found in the game's imports", a_name);
				return false;
			}

			const auto original = *reinterpret_cast<std::uintptr_t*>(slot);
			if (original != exported) {
				logger::info("{} is already redirected by another plugin, chaining to it", a_name);
			}

			// Publish the original before the slot points at the hook, so the hook never sees it unset.
			a_original = reinterpret_cast<F*>(original);
			const auto hook = reinterpret_cast<std::uintptr_t>(a_hook);
			if (!REL::safe_write(slot, &hook, sizeof(hook), &original, sizeof(original))) {
				logger::error("Failed to patch the {} import", a_name);
				return false;
			}
			return true;
		}
	}

	bool Install()
	{
		if (!PatchImport("XInputSetState", kOrdinalSetState, &SetStateHook, g_setState) ||
			!PatchImport("XInputGetState", kOrdinalGetState, &GetStateHook, g_getState)) {
			return false;
		}
		g_installed = true;
		logger::info("XInput hooks installed");
		return true;
	}

	bool IsInstalled()
	{
		return g_installed;
	}

	bool GameVibrationEnabled()
	{
		return g_gameVibrationEnabled;
	}

	void Pulse(float a_heavy, float a_light, float a_duration, bool a_preview)
	{
		if (a_duration <= 0.0f || (a_heavy <= 0.0f && a_light <= 0.0f)) {
			return;
		}

		std::scoped_lock lock{ g_lock };
		const ActivePulse pulse{ a_heavy, a_light, a_duration, 0.0f, a_preview };
		if (g_pulseCount < kMaxPulses) {
			g_pulses[g_pulseCount++] = pulse;
		} else {
			*std::ranges::max_element(g_pulses, {}, [](const ActivePulse& a_pulse) { return a_pulse.elapsed / a_pulse.duration; }) = pulse;
		}
	}

	void StartSustain(float a_heavy, float a_light, float a_maxDuration)
	{
		std::scoped_lock lock{ g_lock };
		g_sustain = { a_heavy, a_light, 1.0f, 0.0f, a_maxDuration };
	}

	void StopSustain(float a_fadeOut)
	{
		std::scoped_lock lock{ g_lock };
		if (g_sustain.level <= 0.0f) {
			return;
		}
		if (a_fadeOut > 0.0f) {
			g_sustain.fadeRate = 1.0f / a_fadeOut;
		} else {
			g_sustain.level = 0.0f;
		}
	}

	void Reset()
	{
		std::scoped_lock lock{ g_lock };
		ClearEffects();
	}
}
