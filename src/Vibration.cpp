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

		// Controller slots as bit masks: answered the game's polling / received the game's own vibration.
		std::atomic_uint32_t g_connectedUsers{ 0 };
		std::atomic_uint32_t g_gameUsers{ 0 };

		// Diagnostics for bug reports.
		std::atomic_uint32_t g_getStateCalls{ 0 };
		std::atomic_uint32_t g_pollTicks{ 0 };

		std::mutex                            g_lock;
		std::array<ActivePulse, kMaxPulses>   g_pulses{};
		std::size_t                           g_pulseCount{ 0 };
		Sustain                               g_sustain;
		float                                 g_heavy{ 0.0f };
		float                                 g_light{ 0.0f };
		std::chrono::steady_clock::time_point g_lastAdvance{};
		std::chrono::steady_clock::time_point g_lastSettingPoll{};
		int                                   g_loggedGameVibration{ -1 };

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

		bool GameSeesGamepad()
		{
			const auto manager = RE::BSInputDeviceManager::GetSingleton();
			return manager && manager->IsGamepadConnected();
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

		// Runs once per frame from the engine's gamepad poll, on the main thread, menus included.
		void Tick()
		{
			std::array<REX::W32::XINPUT_VIBRATION, kMaxUsers> pending{};
			std::uint32_t                                     pendingUsers = 0;
			{
				std::scoped_lock lock{ g_lock };

				const auto now = std::chrono::steady_clock::now();
				if (now - g_lastSettingPoll >= kGameSettingPollInterval) {
					g_lastSettingPoll = now;
					const bool enabled = ReadGameVibrationSetting();
					g_gameVibrationEnabled = enabled;
					if (g_loggedGameVibration != static_cast<int>(enabled)) {
						g_loggedGameVibration = static_cast<int>(enabled);
						logger::info("In-game Vibration option is {}", enabled ? "on" : "off (effects stay muted while \"Follow the game's Vibration setting\" is enabled)");
					}
				}

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

				// When the game's polling bypasses our import hook, the slot is only known once the game vibrates on its own.
				auto users = g_connectedUsers.load() | g_gameUsers.load();
				if (users == 0 && GameSeesGamepad()) {
					users = 1;
				}

				for (std::uint32_t user = 0; user < kMaxUsers; ++user) {
					if ((users & (1u << user)) == 0) {
						continue;
					}
					const auto output = Mix(user);
					if (output == g_written[user]) {
						continue;
					}
					g_written[user] = output;
					pending[user] = output;
					pendingUsers |= 1u << user;
				}
			}

			for (std::uint32_t user = 0; user < kMaxUsers; ++user) {
				if ((pendingUsers & (1u << user)) != 0) {
					g_setState(user, &pending[user]);
				}
			}
		}

		std::uint32_t GetStateHook(std::uint32_t a_user, REX::W32::XINPUT_STATE* a_state)
		{
			const auto result = g_getState(a_user, a_state);
			g_getStateCalls.fetch_add(1, std::memory_order_relaxed);
			if (result == kErrorSuccess && a_user < kMaxUsers) {
				const auto bit = 1u << a_user;
				if ((g_connectedUsers.fetch_or(bit) & bit) == 0) {
					logger::info("Game polls XInput controller {}: connected", a_user);
				}
			}
			return result;
		}

		std::uint32_t SetStateHook(std::uint32_t a_user, REX::W32::XINPUT_VIBRATION* a_vibration)
		{
			if (!a_vibration || a_user >= kMaxUsers) {
				return g_setState(a_user, a_vibration);
			}

			g_gameUsers.fetch_or(1u << a_user);

			REX::W32::XINPUT_VIBRATION output;
			{
				std::scoped_lock lock{ g_lock };
				g_game[a_user] = *a_vibration;
				output = Mix(a_user);
				g_written[a_user] = output;
			}
			return g_setState(a_user, &output);
		}

		// Ticking from the engine's own gamepad poll instead of XInputGetState: another plugin (KiENBExtender in a
		// Nolvus report) can re-point the game's XInputGetState import so the game's polling never reaches our hook.
		struct GamepadPollHook
		{
			static void Thunk(RE::BSPCGamepadDeviceHandler* a_this, float a_timeDelta)
			{
				original(a_this, a_timeDelta);
				if (g_pollTicks.fetch_add(1, std::memory_order_relaxed) == 0) {
					logger::info("Gamepad update loop running");
				}
				Tick();
			}

			static inline REL::Relocation<decltype(&Thunk)> original;
		};

		// Names the DLL a hook lives in, so a bug report shows which plugin redirected XInput before us.
		std::string ModuleNameOf(std::uintptr_t a_address)
		{
			REX::W32::MEMORY_BASIC_INFORMATION info{};
			if (!REX::W32::VirtualQuery(reinterpret_cast<const void*>(a_address), &info, sizeof(info)) || !info.allocationBase) {
				return "unknown module";
			}

			std::array<wchar_t, 260> path{};
			const auto length = REX::W32::GetModuleFileNameW(reinterpret_cast<REX::W32::HMODULE>(info.allocationBase), path.data(), static_cast<std::uint32_t>(path.size()));
			if (length == 0) {
				return "unknown module (not inside a DLL, e.g. a trampoline)";
			}

			const auto name = std::filesystem::path{ std::wstring_view{ path.data(), length } }.filename().wstring();
			return SKSE::stl::utf16_to_utf8(name).value_or("unknown module");
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
				logger::info("{} is already redirected by {} (0x{:X}), chaining to it", a_name, ModuleNameOf(original), original);
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

		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSPCGamepadDeviceHandler[0] };
		GamepadPollHook::original = vtable.write_vfunc(0x2, GamepadPollHook::Thunk);

		g_installed = true;
		logger::info("XInput and gamepad poll hooks installed");
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

	void LogStatus()
	{
		const auto polls = g_pollTicks.load();
		const auto calls = g_getStateCalls.load();
		const auto connected = g_connectedUsers.load();
		const auto gameUsers = g_gameUsers.load();

		if (polls == 0) {
			logger::warn("Gamepad status: the game's gamepad poll has not run - gamepad input may be turned off in the game");
			return;
		}
		if (calls == 0) {
			logger::info("Gamepad status: {} updates; the game's XInput polling bypasses this plugin (another plugin re-points the import), "
						 "so the controller slot comes from the game's own vibration (mask 0x{:X}) or defaults to 0",
				polls, gameUsers);
			return;
		}
		logger::info("Gamepad status: {} updates, {} XInput polls, connected controllers mask 0x{:X}, game vibration mask 0x{:X}",
			polls, calls, connected, gameUsers);
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
