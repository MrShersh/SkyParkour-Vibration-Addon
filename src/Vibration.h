#pragma once

// The game's own XInput calls are intercepted so its vibration (hits, shouts, bows)
// and ours are mixed per motor instead of overwriting each other.
namespace Vibration
{
	bool Install();
	bool IsInstalled();
	bool GameVibrationEnabled();

	// A preview (Test button in the settings menu) keeps playing while the menu pauses the game or the addon is disabled.
	void Pulse(float a_heavy, float a_light, float a_duration, bool a_preview = false);

	void StartSustain(float a_heavy, float a_light, float a_maxDuration);
	void StopSustain(float a_fadeOut);
	void Reset();
}
