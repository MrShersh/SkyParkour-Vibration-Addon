#include "Haptics.h"
#include "Menu.h"
#include "Settings.h"
#include "Vibration.h"

namespace
{
	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Menu::Register();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Vibration::Reset();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			// Re-reading here lets INI edits apply on the next save load without restarting the game.
			Settings::Load();
			Menu::Refresh();
			Haptics::ResetState();
			Haptics::RegisterSink();
			Vibration::Reset();
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	const auto plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} {} loaded", plugin->GetName(), plugin->GetVersion().string());

	Settings::Load();

	if (!Vibration::Install()) {
		logger::error("XInput hooks not installed, the addon stays inactive");
		return true;
	}
	Haptics::Install();

	SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage);
	return true;
}
