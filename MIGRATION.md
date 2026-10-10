# Asset preparation and import

Conversion is offline. Runtime systems consume saved assets/catalogs; spawning and travel never extract archives or convert NIF files. Hosts own generated content.

## Workflow

1. Inventory/extract authorized source files without modifying the installation.
2. Prepare models, textures, terrain, placements, equipment, and animations.
3. Import manifests through editor commandlets.
4. Bind catalogs/profiles in the host and validate results.
5. Configure cooking and verify the packaged consumer separately.

Python preparation uses Pillow/NumPy where required. Native converters need external dependencies, including `niflib++` for `tools/nif-export`; the layout is not self-contained here.

`tools/migrate_client.py` stages are `inventory`, `extract`, `catalog`, `characters`, `animations`, `equipment`, `zones`, and `report`. Supply explicit `--client`, `--output`, `--converter`, and `--zone-exporter` paths as needed. `--models`, `--zones`, and `--failed-only` narrow work.

`tools/migrate_all.ps1` combines stages. Override `-Client`, `-Cache`, `-Project`, and `-Engine` for the intended host. `-PrepareOnly` avoids Unreal writes; `-SkipCombat` omits combat preparation/import. Import helpers must explicitly target the intended authoring or disposable host.

## Integrity and coverage

Extraction checks CRCs, sizes, path boundaries, and loose-file precedence. Reports retain missing/unsupported inputs. Caches reuse only compatible conversions. Close the editor before imports and run writes sequentially. `DAOC_MIGRATION_RETRY=1` retries repaired failures. Reimport successful assets only deliberately. `--allow-incomplete-fixtures` explicitly records omitted scenery and is disabled by default.

Client catalogs map IDs to soft references; explicit zone profiles override classic profiles. Keep imported geometry separate from authored appearance. Equipment has its own catalog. Include dynamic references through host asset-manager/cook rules.

Inventory, preparation, `unreal-import.json`, equipment, and coverage reports distinguish available, prepared, imported, and checked data. Prepared files are not automatically usable or visually verified assets. Do not publish databases, backups, credentials, or licensed source content with core documentation.
