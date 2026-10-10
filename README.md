# Camelot Core

Camelot Core is an Unreal Engine plugin for reusable DAoC-compatible game mechanics, networking, and rendering. It supports upgrades to the existing Camelot Enhanced Client and distinct non-client projects, such as cutscenes, movies, mods, and item catalog inspectors or analyzers.

The core provides shared systems and interfaces. Host projects own maps, content, branding, visual tuning, and deployment. The downstream examples above are permitted uses, not features promised by this repository. See [CONTRIBUTING.md](CONTRIBUTING.md) for scope, usage restrictions, and attribution.

## Systems

| Area | Responsibility |
| --- | --- |
| Networking | DOL-compatible session, authentication, character state, world travel, and packet handling |
| Mechanics | Movement, targeting, server-confirmed combat, equipment, and appearance |
| Rendering | Terrain, scenery, skeletal characters, shared GPU crowds, animation, water, and weather |
| Data and tools | Catalogs, zone profiles, offline preparation, and editor imports |
| UI | Login, character selection, reusable panels, status box, three hotbars, character/skills/effects/equipment pages, chat, and bounded nameplates |
| Inventory | Confirmed equipment/backpack state, classic move/use/details requests, drag/drop, stack splitting, and a player preview |

DOL remains authoritative for connected gameplay. Rendering does not own login or sockets. Offline consumers can use prepared assets and rendering without a connection.

Live mobs and remote players automatically use prepared GPU crowds, with full presentation for local/selected characters and automatic fallback for unsupported appearances or capacity limits. Host assets must be prepared and saved; see [crowd rendering](CROWD_RENDERING.md).

## Install and build

Engine target: **UE 5.8.3**, Windows. Build both Development Editor and Game targets. Imported content, cooked assets and battlefield performance require host validation. See [engine integration](UE_MIGRATION.md) and [crowd rendering](CROWD_RENDERING.md).

1. Clone this repository with `git clone --recurse-submodules <Camelot-Core-repository-url>`. For an existing checkout, run `git submodule update --init --recursive`.
2. Create `Plugins/CamelotCore` in the host project and copy `CamelotCore.uplugin`, `Source`, `Config`, `Data`, and `ThirdParty` into it. The descriptor declares ProceduralMeshComponent and Niagara dependencies.
3. Enable Camelot Core and build the host editor target with the engine-supported C++ toolchain.
4. Prepare and save required host assets before runtime use. Enable PythonScriptPlugin when using Unreal Python import tools.

Keep `tests/build` out of installed plugin copies; it contains a separate verification project. Close the editor before rebuilding reflected types or importing assets. Live Coding does not replace a full rebuild for reflected changes.

The module is `CamelotCore` and its export macro is `CAMELOTCORE_API`. Existing `Daoc*` class names remain. [Config/DefaultCamelotCore.ini](Config/DefaultCamelotCore.ini) redirects the previous script package for saved assets. Do not load both plugin versions in the same host.

## Required source dependencies

| Dependency | Source | Purpose |
| --- | --- | --- |
| DAoC Client Library | [BifrostTitan/DAoC-Client-Lib](https://github.com/BifrostTitan/DAoC-Client-Lib) | Native client-data readers, archive decoding, zone geometry and asset preparation |
| DAoC Niflib | [BifrostTitan/daoc-niflib](https://github.com/BifrostTitan/daoc-niflib) | NIF parsing, geometry, skinning and animation data used by asset preparation |

Both repositories are required, pinned Git submodules under `ThirdParty`. Unreal build setup rejects missing source dependencies with the initialization command. GitHub source ZIP downloads do not include submodule contents; use a recursive clone or supply the pinned dependency checkouts separately. Preserve upstream attribution and licenses.

The current Unreal runtime consumes prepared assets and implements its gameplay protocol internally; it does not link these native libraries into its runtime module. Build the native data libraries and zone exporter with the combined CMake entry point:

```powershell
cmake -S ThirdParty -B tests/build/dependencies
cmake --build tests/build/dependencies --config Release
ctest --test-dir tests/build/dependencies -C Release --output-on-failure
```

Requires CMake 3.20+, a C++17 toolchain and Zlib. The client library uses an installed Zlib when available; otherwise its pinned build fetches Zlib 1.3.1, requiring network access during the first configure. `DAoC-Client-Lib` links `Niflib::niflib` through this entry point. Do not build inside the submodule directories or change their pinned revisions as part of an ordinary build.

Known upstream test issue at client-library revision `81a8034`: the indexed-PCX regression reports `indexed PCX palette mismatch`. Its fixture encodes palette index 1 but initializes palette entry 0. Both native libraries and the zone exporter compile; this upstream test issue does not affect Camelot Core's independent runtime PCX decoder. Keep the failure visible when reporting dependency verification.

## Verification

Run from this repository with CMake and the engine toolchain available:

```powershell
cmake -S tests -B tests/build/native
cmake --build tests/build/native --config Debug
ctest --test-dir tests/build/native -C Debug --output-on-failure
python tests/migration_tests.py
python tests/terrain_detail_tests.py
python tests/engine_configuration_tests.py
./tests/engine_configuration_tests.ps1
./tools/verify_camelot.ps1 -EngineRoot $EngineRoot -SkipContentTests
```

Set `$EngineRoot` to your completed 5.8.3 installation, or set `CAMELOT_ENGINE_ROOT` for shared build/import/launcher helpers. Engine version and required files are checked before launch. `verify_camelot.ps1 -PrepareOnly` stages an isolated host without running Unreal; normal verification builds both Game and Editor targets. Python terrain tests require NumPy/Pillow. The standalone Unreal check verifies plugin identity, catalog availability, and the old-module redirect. Omitting `-SkipContentTests` adds socket/rendering integration requiring imported game assets in the verification host. Compilation and protocol checks do not establish cooked-content coverage or visual quality.

## References

- [Capabilities and limits](CLIENT_STATUS.md)
- [Networking](NETWORK.md), [combat](COMBAT.md), and [animation](COMBAT_ANIMATIONS.md)
- [Gameplay UI](UI.md), [inventory and equipment](INVENTORY.md), [frontend](FRONTEND.md), and [destination selection](STARTING_LOCATIONS.md)
- [Server mob navigation and terrain following](NAVIGATION.md)
- [Asset preparation](MIGRATION.md) and [developer tools](DEVELOPER_TOOLBOX.md)
- [Terrain](TERRAIN.md), [foliage](FOLIAGE.md), and [sizing](LANDSCAPE_AND_RACE_SIZING.md)
- [Water](WATER.md), [weather](WEATHER.md), and [administration](ADMIN_COMMANDS.md)

Original artwork, licensed packs, server databases, credentials, and generated host content are not core source. Some legacy scripts retain local defaults: supply explicit paths and inspect targets before use. Asset-changing setup/import scripts are not runtime initialization.
