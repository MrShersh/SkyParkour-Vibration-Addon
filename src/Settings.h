#pragma once

namespace Settings
{
	struct Effect
	{
		bool  enabled{ true };
		float heavy{ 0.0f };
		float light{ 0.0f };
		float duration{ 0.0f };
	};

	struct Values
	{
		bool  enabled{ true };
		float masterStrength{ 1.0f };
		bool  respectGameSetting{ true };
		bool  debugLog{ false };

		Effect pushOff{ true, 0.25f, 0.20f, 0.10f };
		Effect grab{ true, 0.65f, 0.60f, 0.16f };
		Effect land{ true, 0.50f, 0.35f, 0.14f };
		Effect fail{ true, 1.00f, 0.40f, 0.35f };
		Effect roll{ true, 0.70f, 0.40f, 0.35f };
		Effect slide{ true, 0.10f, 0.40f, 0.25f };  // duration is the fade-out after the slide ends

		float grabScaleLedgeGrab{ 0.8f };
		float grabScaleVault{ 0.7f };
		float grabScaleLow{ 0.8f };
		float grabScaleMedium{ 1.0f };
		float grabScaleHigh{ 1.15f };
		float grabScaleHighest{ 1.3f };

		// Safety net: a slide vibration never lasts longer than this, even if the stop event is missed.
		float slideMaxDuration{ 3.0f };

		Effect jump{ true, 0.20f, 0.25f, 0.10f };
		Effect jumpLand{ true, 0.95f, 0.50f, 0.24f };  // strength at a fall of jumpLandFullHeight or more

		// A hop in place lands at jumpLandMinStrength, growing linearly to full strength at this fall height (game units).
		float jumpLandFullHeight{ 600.0f };
		float jumpLandMinStrength{ 0.3f };
	};

	// Returns a snapshot, so readers on other threads stay valid while the menu or a save load replaces the settings.
	std::shared_ptr<const Values> Get();

	void Load();
	void Apply(const Values& a_values);
	bool Save(const Values& a_values);
}
