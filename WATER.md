# Water rendering

Outdoor definitions are read from `sector.dat` water/river sections: paired shorelines, height, flow, type, source textures, and color. Rivers/ponds render as separate procedural sections using the terrain/fixture coordinate conversion.

Use **Import outdoor water and save profile** in the toolbox to embed definitions/materials. A valid zone without water stores an empty prepared list. Cooked consumers need saved data/assets rather than exports or editor-generated graphs.

Profiles select generation and material. Custom materials use section UVs and `WaterFlow`; source names/colors remain metadata. Regeneration clears old sections; submerged ground cover is excluded.

Current support is outdoor paired-bank geometry. It does not establish dungeon water, ocean extensions, waterfalls, original texture animation, underwater presentation, collision, or swimming. Check shorelines, heights, zone cleanup, saved references, and cooked materials.
