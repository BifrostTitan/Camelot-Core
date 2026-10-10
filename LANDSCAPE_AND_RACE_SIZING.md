# Coordinate and character sizing

Keep terrain, fixture, server, and skeletal-import units distinct. Apply conversions consistently rather than tuning each for one map.

## World

Height/offset samples remain row-major without transposition. Fixture coordinates convert with `255 / 65536` into legacy rendered units; `WorldUnitsPerDaocUnit` sets Unreal scale. Server positions additionally use zone origin and renderer transform.

Sampling follows the rendered cell diagonal. Movement/scenery agree on scale and feet placement. Visual scale changes must preserve conversion and collision.

## Characters

Skeletal assets import at 2.54 cm per NIF unit. Modular base scales come from source data; `Private/DaocCharacterModelScales.inl` stores sorted, inclusive model-ID ranges. `Private/DaocCharacterSizing.h` validates the table at compile time and provides model, height and packet-size calculations. Unlisted models use scale 1. Non-modular manifests may already bake base scale. Do not apply conversions twice.

Player model identity occupies the lower 11 bits; height selection uses bits 11 and 12. Unflagged models use medium. Relative presentation multipliers are currently 0.95, 1.00, and 1.05, which are tuning values rather than verified original-client ratios. Equipment follows parent scale; movement/gravity remain tied to world scale.

NPC packet size 50 is normal model size; 25 is half size and 100 is double size. Player packet sizing retains its baseline of 100. A zero size preserves normal model scale. Apply this multiplier after world-unit and model sizing; both skeletal fallback and live GPU instances use the resulting transform. Preserve the raw server size value.

Check preview/world agreement, feet alignment, capsules, armor pose compatibility, and coordinate round trips. Host-specific tuning and repair history are not part of this reference.
