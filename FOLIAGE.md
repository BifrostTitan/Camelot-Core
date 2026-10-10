# Scenery replacement and ground cover

Enhanced rendering is profile-driven. Classic mode keeps original scenery. Exact source NIF basenames identify replacements; local entries override shared defaults, and a null local mesh retains the original. Unmapped fixtures stay original.

Shared enhancement contains appearance only; it must not change geometry, zone identity, or network coordinates. Species choices, replacement packs, mesh fitting, and scene-specific LOD tuning belong to host assets.

Replacement height fitting uses LOD0 source geometry bounds. Nanite area preservation can enlarge rendering/culling bounds substantially, especially on sparse classic tree meshes; those bounds must never determine a replacement tree's physical size. The original culling bounds remain intact for rendering.

## Ground cover

Cover uses deterministic hierarchical instances, source masks, solid-fixture exclusions, slope alignment, and footprint seating. Profiles configure meshes, size variation, spacing, density thresholds, tint, fade, and culling. Zero-mask areas stay empty.

An empty mesh list can inherit shared settings. With `Override Ground Cover`, the zone controls its own settings and an empty list disables cover. Tint multiplies source color; overrides can expose `DaocGrassTint`. Fade-capable materials use per-instance fade.

Terrain is procedural geometry; Landscape-only automatic-grass nodes do not replace this generator. Keep collision, shadows, LOD, and Nanite explicit and measure them in the host scene.

## Nanite preparation

New object imports enable Nanite for meshes whose entire material set supports it. Translucent sections, non-surface materials and single-layer water retain conventional rendering. Source triangles, explicit tangents and full-quality fallback geometry are preserved. Static replacements with WPO disabled cache virtual shadows; grass retains its existing animation, culling, density and shadow settings.

The toolbox prepares compatible owned replacements and full-quality meadow meshes, including saved material usage flags. UE 5.8 tracks usage on material instances as well as base materials; saving the mesh alone can leave packaged builds using default materials.

[tools/prepare_zone_nanite.py](tools/prepare_zone_nanite.py) prepares existing assets from an explicit JSON plan. Supply `assets` (each with `path` and `foliage`), `references` (zone profile paths), `owned_roots`, and `clone_root`. Assets outside the owned roots are cloned and the listed profiles are rewired. A reviewed previous clone can be named in `existing_clones`; an unlisted existing clone is never overwritten. Existing changed packages and a manifest are backed up under the host project's `Saved` directory.

Run through Unreal's PythonScriptPlugin with `-script=<script> -DaocNanitePlan=<plan> -CamelotNoAutoConnect -DaocOffline`. The default is a compatibility audit; add `-DaocNaniteApply` to prepare and save. Compare warmed gameplay views at the same resolution and lighting settings. Masked foliage can still be limited by overdraw, wind or lighting, so Nanite support alone does not establish a frame-rate improvement.

## Assets and checks

Copy only needed replacement dependencies and retain provenance. A library audit does not prove unrelated host assets are independent of source packs. Prepared masks/layers should preserve land-cover identity.

Check exact mapping, unknown fallback, local opt-out, classic/enhanced switching, tint, and zone isolation. [Data/foliage-inventory.json](Data/foliage-inventory.json) is inventory data, not a required replacement shopping list.
