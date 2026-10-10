# UE 5.8.3 integration

[Config/EngineTarget.json](Config/EngineTarget.json) defines the supported engine target. Build and launch helpers require UE 5.8.3. The descriptor uses Unreal's `5.8.0` compatibility baseline so unattended startup recognizes the plugin.

## Build and verification

Set `CAMELOT_ENGINE_ROOT` to a complete engine installation, then run:

```powershell
./tools/verify_camelot.ps1 -PrepareOnly
./tools/verify_camelot.ps1 -SkipContentTests
```

The helper prepares an isolated verification host, checks engine files/version, builds Game and Editor targets, and runs core identity automation. Targets use `Unreal5_8` include order and `BuildSettingsVersion.V7`. Imported-content checks require prepared host assets; see [README.md](README.md).

## Upgrade a host

1. Back up the project and upgrade a disposable copy first; converted assets may not load in older engines.
2. Set engine association to `5.8`, install Camelot Core, and regenerate project files. Custom source-engine GUID associations require adapting the helpers.
3. Rebuild derived data and validate redirects, catalogs, terrain/water materials, Niagara assets, skeletal imports, equipment poses, animation, UI and world travel.
4. Cook and run Development on the intended RHI before Shipping. Null-RHI checks cannot validate shaders, Nanite, shadows or frame time.

Crowds require saved instancing and animation assets. Skeletal Nanite is an explicit asset choice, not a capacity guarantee or global conversion. No MetaHuman or Mass dependency is required. See [CROWD_RENDERING.md](CROWD_RENDERING.md) and [Epic's release notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes).
