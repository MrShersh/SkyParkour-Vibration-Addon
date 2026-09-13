#include "Haptics.h"

#include "Vibration.h"
#include "Settings.h"

#include <cmath>

namespace Haptics
{
	namespace
	{
		// SkyParkour v3 behavior graph names (include/_References/BehaviorGraph.h in its source).
		constexpr auto kParkourStart = "SkyParkour_Start"sv;
		constexpr auto kSlideStart = "SkyParkour_SlideStart"sv;
		constexpr auto kSlideStop = "SkyParkour_SlideStop"sv;
		constexpr auto kSlidePayload = "Slide"sv;
		constexpr auto kRollPayload = "LandRoll"sv;
		constexpr auto kLedgeVariable = "SkyParkourLedge";
		constexpr auto kIsRollVariable = "SkyParkourIsLandingRoll";

		// Sound annotations inside SkyParkour's animations, placed on the physical contact frames.
		constexpr auto kSoundPushOff = "SPPF_Jump"sv;
		constexpr auto kSoundHandContact = "SPPF_Touch"sv;
		constexpr auto kSoundLanding = "SPPF_JumpLand"sv;

		// Vanilla jump notifications the game sends into the player's graph (JumpStandingStart, JumpFall, JumpLand, ...).
		constexpr auto kJumpPrefix = "Jump"sv;
		constexpr auto kJumpStartMarker = "Start"sv;
		constexpr auto kJumpFallPrefix = "JumpFall"sv;
		constexpr auto kJumpLandPrefix = "JumpLand"sv;
		constexpr auto kJumpLandEnd = "JumpLandEnd"sv;

		// A jump turns into a fall right after takeoff; a fall notification later than this means the player
		// stepped off a ledge, so the fall height is measured from there instead.
		constexpr auto kJumpToFallWindow = 1500ms;

		// ParkourType (include/_References/ParkourType.h in SkyParkour's source).
		enum class Ledge : std::int32_t
		{
			NoLedge = -1,
			Failed,
			Grab,
			Vault,
			StepLow,
			StepHigh,
			Low,
			Medium,
			High,
			Highest
		};

		enum class Effect : std::size_t
		{
			PushOff,
			Grab,
			Land,
			Fail,
			Roll,
			Jump,
			JumpLand,
			Total
		};

		constexpr std::array<std::string_view, std::to_underlying(Effect::Total)> kEffectNames{
			"PushOff", "Grab", "Land", "Fail", "Roll", "Jump", "JumpLand"
		};

		// Ignores the same effect arriving twice in a row (e.g. from both first- and third-person graphs).
		constexpr auto kRetriggerGuard = 60ms;

		std::mutex                                                                            g_lock;
		std::array<std::chrono::steady_clock::time_point, std::to_underlying(Effect::Total)> g_lastFired{};
		std::atomic_bool                                                                      g_sliding{ false };
		bool                                                                                  g_airborne{ false };
		float                                                                                 g_airStartZ{ 0.0f };
		std::chrono::steady_clock::time_point                                                 g_airStartTime{};

		bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
		{
			return a_lhs.size() == a_rhs.size() && _strnicmp(a_lhs.data(), a_rhs.data(), a_lhs.size()) == 0;
		}

		bool IStartsWith(std::string_view a_text, std::string_view a_prefix)
		{
			return a_text.size() >= a_prefix.size() && IEquals(a_text.substr(0, a_prefix.size()), a_prefix);
		}

		float GrabScale(const Settings::Values& a_settings, Ledge a_ledge)
		{
			switch (a_ledge) {
			case Ledge::Grab:
				return a_settings.grabScaleLedgeGrab;
			case Ledge::Vault:
				return a_settings.grabScaleVault;
			case Ledge::Low:
				return a_settings.grabScaleLow;
			case Ledge::Medium:
				return a_settings.grabScaleMedium;
			case Ledge::High:
				return a_settings.grabScaleHigh;
			case Ledge::Highest:
				return a_settings.grabScaleHighest;
			default:
				return 1.0f;
			}
		}

		void Fire(const Settings::Values& a_settings, Effect a_kind, const Settings::Effect& a_effect, float a_scale = 1.0f)
		{
			if (!a_effect.enabled) {
				return;
			}
			{
				std::scoped_lock lock{ g_lock };
				const auto       now = std::chrono::steady_clock::now();
				auto&            last = g_lastFired[std::to_underlying(a_kind)];
				if (now - last < kRetriggerGuard) {
					return;
				}
				last = now;
			}

			const auto strength = a_settings.masterStrength * a_scale;
			Vibration::Pulse(a_effect.heavy * strength, a_effect.light * strength, a_effect.duration);
			if (a_settings.debugLog) {
				logger::info("  -> {} x{:.2f}", kEffectNames[std::to_underlying(a_kind)], strength);
			}
		}

		void MarkAirborne(float a_z, std::chrono::steady_clock::time_point a_now)
		{
			g_airborne = true;
			g_airStartZ = a_z;
			g_airStartTime = a_now;
		}

		void OnAnimationEvent(const RE::BSAnimationGraphEvent& a_event)
		{
			const std::string_view tag{ a_event.tag.c_str() };
			const std::string_view payload{ a_event.payload.c_str() };

			// "SoundPlay.SPPF_Touch" arrives as tag + payload; accept the joined spelling as well.
			std::string_view sound;
			if (IEquals(tag, "SoundPlay"sv)) {
				sound = payload;
			} else if (IStartsWith(tag, "SoundPlay."sv)) {
				sound = tag.substr("SoundPlay."sv.size());
			}

			if (!IStartsWith(tag, "SkyParkour"sv) && !IStartsWith(sound, "SPPF_"sv)) {
				return;
			}

			const auto settings = Settings::Get();
			if (settings->debugLog) {
				logger::info("event '{}' payload '{}'", tag, payload);
			}

			// Parkour can start mid-air and ends on solid ground without a vanilla landing, so the pending fall is void.
			if (IEquals(tag, kParkourStart)) {
				std::scoped_lock lock{ g_lock };
				g_airborne = false;
			}

			if (!settings->enabled) {
				return;
			}

			const auto holder = a_event.holder;

			if (IEquals(tag, kSlideStart)) {
				bool isRoll = IEquals(payload, kRollPayload);
				if (!isRoll && !IEquals(payload, kSlidePayload) && holder) {
					holder->GetGraphVariableBool(kIsRollVariable, isRoll);
				}
				g_sliding = true;

				if (isRoll) {
					Fire(*settings, Effect::Roll, settings->roll);
				} else if (settings->slide.enabled) {
					Vibration::StartSustain(
						settings->slide.heavy * settings->masterStrength,
						settings->slide.light * settings->masterStrength,
						settings->slideMaxDuration);
				}
			} else if (IEquals(tag, kSlideStop)) {
				g_sliding = false;
				Vibration::StopSustain(settings->slide.duration);
			} else if (IEquals(sound, kSoundPushOff)) {
				// The landing roll animation reuses this sound; the roll already has its own effect.
				if (!g_sliding) {
					Fire(*settings, Effect::PushOff, settings->pushOff);
				}
			} else if (IEquals(sound, kSoundHandContact)) {
				auto ledge = std::to_underlying(Ledge::NoLedge);
				if (holder) {
					holder->GetGraphVariableInt(kLedgeVariable, ledge);
				}
				if (static_cast<Ledge>(ledge) == Ledge::Failed) {
					Fire(*settings, Effect::Fail, settings->fail);
				} else {
					Fire(*settings, Effect::Grab, settings->grab, GrabScale(*settings, static_cast<Ledge>(ledge)));
				}
			} else if (IEquals(sound, kSoundLanding)) {
				Fire(*settings, Effect::Land, settings->land);
			}
		}

		void OnJumpNotify(std::string_view a_event, bool a_accepted)
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}

			const auto settings = Settings::Get();
			const auto z = player->GetPositionZ();
			if (settings->debugLog) {
				const auto controller = player->GetCharController();
				logger::info("notify '{}' accepted={} z={:.0f} fallStartHeight={:.0f} fallTime={:.2f}",
					a_event, a_accepted, z, controller ? controller->fallStartHeight : 0.0f, controller ? controller->fallTime : 0.0f);
			}

			if (!a_accepted || !settings->enabled || player->IsOnMount() || player->AsActorState()->IsSwimming()) {
				return;
			}

			const auto now = std::chrono::steady_clock::now();

			if (IStartsWith(a_event, kJumpLandPrefix)) {
				if (IEquals(a_event, kJumpLandEnd)) {
					return;
				}

				float fall = 0.0f;
				{
					std::scoped_lock lock{ g_lock };
					if (g_airborne) {
						fall = std::max(0.0f, g_airStartZ - z);
					}
					g_airborne = false;
				}
				// The landing roll replaces the landing and has its own effect.
				if (g_sliding) {
					return;
				}

				const auto progress = std::clamp(fall / std::max(settings->jumpLandFullHeight, 1.0f), 0.0f, 1.0f);
				const auto strength = std::lerp(settings->jumpLandMinStrength, 1.0f, progress);
				if (settings->debugLog) {
					logger::info("  fall {:.0f} units", fall);
				}
				Fire(*settings, Effect::JumpLand, settings->jumpLand, strength);
			} else if (IStartsWith(a_event, kJumpFallPrefix)) {
				std::scoped_lock lock{ g_lock };
				if (!g_airborne || now - g_airStartTime > kJumpToFallWindow) {
					MarkAirborne(z, now);
				}
			} else if (a_event.find(kJumpStartMarker) != std::string_view::npos) {
				{
					std::scoped_lock lock{ g_lock };
					MarkAirborne(z, now);
				}
				Fire(*settings, Effect::Jump, settings->jump);
			}
		}

		class PlayerAnimationSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::BSAnimationGraphEvent* a_event,
				RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (a_event) {
					OnAnimationEvent(*a_event);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		PlayerAnimationSink g_sink;

		// Subscribes to every graph of the manager (first and third person). Re-adding is harmless.
		void AttachSink(RE::BSAnimationGraphManager& a_manager)
		{
			for (const auto& graph : a_manager.graphs) {
				if (!graph) {
					continue;
				}
				const auto source = graph->GetEventSource<RE::BSAnimationGraphEvent>();
				source->RemoveEventSink(&g_sink);
				source->AddEventSink(&g_sink);
			}
		}

		// IAnimationGraphManagerHolder::PostCreateAnimationGraphManager runs whenever the player's behavior graphs
		// are rebuilt (e.g. race change), which drops existing sinks. SkyParkour hooks the same slot; the hooks chain.
		struct PostCreateGraphHook
		{
			static void Thunk(RE::IAnimationGraphManagerHolder* a_this, RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_manager)
			{
				original(a_this, a_manager);
				if (a_manager) {
					AttachSink(*a_manager);
				}
			}

			static inline REL::Relocation<decltype(&Thunk)> original;
		};

		// The game starts jumps and landings by notifying the player's graph at the physical moment, which the
		// animation events only follow later. SkyParkour hooks the same slot too; the hooks chain.
		struct PlayerNotifyHook
		{
			static bool Thunk(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_event)
			{
				const bool accepted = original(a_this, a_event);
				const std::string_view name{ a_event.c_str() };
				if (IStartsWith(name, kJumpPrefix)) {
					OnJumpNotify(name, accepted);
				}
				return accepted;
			}

			static inline REL::Relocation<decltype(&Thunk)> original;
		};
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[3] };
		PostCreateGraphHook::original = vtable.write_vfunc(0xB, PostCreateGraphHook::Thunk);
		PlayerNotifyHook::original = vtable.write_vfunc(0x1, PlayerNotifyHook::Thunk);
		logger::info("Player graph hooks installed");
	}

	void RegisterSink()
	{
		const auto                     player = RE::PlayerCharacter::GetSingleton();
		RE::BSAnimationGraphManagerPtr manager;
		if (!player || !player->GetAnimationGraphManager(manager) || !manager) {
			logger::warn("Player has no behavior graph yet, animation events not subscribed");
			return;
		}
		AttachSink(*manager);
	}

	void ResetState()
	{
		g_sliding = false;
		std::scoped_lock lock{ g_lock };
		g_airborne = false;
	}
}
