`SKSEMenuFramework.h` is the API header of SKSE Menu Framework 3, copied unmodified from
[QTR-Modding/SKSE-Menu-Framework-3-Example](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-Example)
(`include/SKSEMenuFramework.h`, commit `f21b8e034c5d7a9fa9479ea128324eae40f99653`, 2026-09-03), MIT License — see `LICENSE`.

SKSE Menu Framework itself is a separate mod ([Nexus 120352](https://www.nexusmods.com/skyrimspecialedition/mods/120352)).
The plugin does not link it: the header looks up the framework's exported functions at runtime, and the settings page
is simply not registered when the framework is not installed.
