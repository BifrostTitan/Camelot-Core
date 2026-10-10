# Terrain rendering

The core loads height/offset data and saved profiles, generates procedural geometry/collision, and binds original or authored materials. Hosts own material assets and visual tuning.

## Geometry and materials

Data is row-major. PCX decoding supports indexed palette and planar RGB, raw/RLE encoding, origins, and padding. Sampling follows the rendered cell diagonal and handles boundaries consistently.

Classic presentation uses saved atlases or prepared source layers/patch weights. Incomplete or unsupported layers retain a valid atlas fallback. Enhanced materials use land-cover masks without replacing geometry or coordinates. Unknown biomes need explicit host treatment.

Profiles override materials. Imports save dependencies before referencing profiles; runtime does not generate editor material graphs. Refresh failure must not bind stale textures to another zone.

## Preparation and checks

`tools/prepare_native_terrain.py --migration <cache>` prepares textures/weights. Host-authoring helpers apply materials in Unreal; inspect targets and invoke them only in the intended project. They are not automatic setup.

Check orientation, heights, normals, sampling, collision, profile identity, saved-reference reloads, and fallback. Verify shaders/viewport appearance separately from null-RHI asset audits. Packs, tint recipes, density, and scene budgets belong to hosts.
