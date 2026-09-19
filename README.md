# SkyParkour Vibration Addon

Adds modern controller vibration support for all moves in [SkyParkour v3](https://www.nexusmods.com/skyrimspecialedition/mods/132292).
SKSE plugin only: no ESP, no Papyrus, safe to add or remove at any time.

## What vibrates

| Moment | Source in SkyParkour | INI section |
|---|---|---|
| Feet push off | `SoundPlay.SPPF_Jump` annotation | `[PushOff]` |
| Hands catch the ledge / plant on a vault | `SoundPlay.SPPF_Touch`, strength by `SkyParkourLedge` | `[Grab]` |
| Failed climb (no stamina) | `SoundPlay.SPPF_Touch` with ledge type `Failed` | `[Fail]` |
| Landing after a step | `SoundPlay.SPPF_JumpLand` | `[Land]` |
| Landing roll | `SkyParkour_SlideStart` with payload `LandRoll` | `[Roll]` |
| Sliding (continuous) | `SkyParkour_SlideStart` … `SkyParkour_SlideStop` | `[Slide]` |
| Regular jump takeoff | game's `Jump…Start` notification | `[Jump]` |
| Landing after a regular jump or fall, stronger the higher the fall | game's `JumpLand…` notification | `[JumpLand]` |

The annotations sit on the exact contact frames of SkyParkour's animations, so the vibration lands on the same frame as the sound.

## How it works

- **Events.** A `BSAnimationGraphEvent` sink on the player's behavior graphs, attached after a save loads and
  re-attached whenever the graphs are rebuilt (hook on `PostCreateAnimationGraphManager`). SkyParkour itself is not modified.
- **Regular jumps.** A hook on the player's `NotifyAnimationGraph` sees the game start a jump or a fall and land.
  The height at takeoff (or where a fall began) minus the height at landing sets the landing strength, from
  `fMinStrength` for a hop in place up to full strength at `fFullStrengthFallHeight`. Mounted and swimming jumps are ignored.
- **Output.** `SkyrimSE.exe` imports `xinput1_3.dll` by ordinal; the plugin redirects the game's `XInputSetState` and
  `XInputGetState` imports. The game's own vibration and the addon's effects are mixed per motor (maximum), so neither
  overrides the other. Effects advance once per frame from the game's own gamepad update
  (`BSPCGamepadDeviceHandler::Poll`) on the main thread, so plugins that re-point the game's XInput imports can't bypass them.
- Vibration stops while the game is paused and follows the in-game Vibration option (`bGamePadRumble`).

## Requirements

- Skyrim SE/AE with SKSE and Address Library
- SkyParkour v3 with its behavior patch generated (Nemesis or Pandora)
- An XInput gamepad (Xbox controllers; PlayStation controllers through Steam Input)
- Optional: [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) for the in-game settings page

## Configuration

- **In game:** with SKSE Menu Framework installed, open its menu (F1 / LB by default) → *SkyParkour Vibration Addon* →
  *Settings*. Changes apply immediately; every effect has a *Test* button that works while the menu pauses the game.
  *Save* writes the INI, *Revert* reloads it, *Defaults* restores the shipped values.
- **INI:** `SKSE/Plugins/SkyParkourVibrationAddon.ini`. File edits apply on the next save load. `bDebugLog = true` writes every
  SkyParkour animation event and fired effect to `Documents/My Games/Skyrim Special Edition/SKSE/SkyParkourVibrationAddon.log`.

## Building

Visual Studio 2022 with *Desktop development with C++*, and [vcpkg](https://github.com/microsoft/vcpkg).

## License

GPL-3.0, see [`LICENSE`](LICENSE).

## Third-party code

- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG).
- SKSE Menu Framework API header.
- spdlog and fmt (through vcpkg).
