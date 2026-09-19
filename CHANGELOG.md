# Changelog

## 1.0.2 - 2026-09-20

- Fixed: no vibration on setups where another plugin redirects the game's XInput polling (reported on Nolvus with KiENBExtender). Effects now run from the game's own gamepad update, which other plugins can't bypass. Vibration feel is unchanged.
- The log now names plugins that hooked XInput before the addon, shows the in-game Vibration option and prints a gamepad status summary after loading a save (first shipped in test build 1.0.1).

## 1.0.0 - 2026-09-13

- Initial release.
