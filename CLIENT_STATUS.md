# Core capabilities and limits

| Area | Existing support |
| --- | --- |
| Session | Login, realm/character selection, world initialization, connection across map travel |
| Mechanics | Movement, targeting, server-confirmed combat, learned abilities, equipment and appearance |
| World | Prepared zone selection, terrain/scenery, classic fallback and authored overrides |
| Characters | Skeletal model lookup, modular equipment, customization data and animation |
| Presentation | Existing frontend/combat UI, water surfaces and profile-driven weather |
| Tooling | Offline preparation, editor imports, catalogs and focused validation |

The current import/travel path supports prepared outdoor, city, and housing profiles; unsupported dungeons fail explicitly. Protocol coverage is version-specific, not a complete retail implementation.

Some source assets, animation controllers, and customization effects are incomplete or unsupported. Owned equipment and backpack UI are supported; loot, guild emblems, weapon effects and inactive weapon sheathing remain incomplete. Exact facial deformation and some animation interpolation remain incomplete.

Prepared, imported, visually checked, and cooked assets are separate milestones. Use import reports for coverage and validate packaging/rendering in the consumer. Local asset counts and historical test results are not core guarantees.

See [README.md](README.md) for checks and [CONTRIBUTING.md](CONTRIBUTING.md) for scope. Improvements should address existing systems rather than grow a general client-completeness roadmap.
