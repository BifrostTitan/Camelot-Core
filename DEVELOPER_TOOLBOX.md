# Editor and developer tools

The toolbox authors and inspects existing core data. It is not a host setup service or an authorization system.

## Profiles and imports

Open **Window > DAoC Zone Toolbox** outside PIE. Create/open a profile, choose source data, and edit rendering settings. Saving/regenerating a profile does not save its level. Refresh geometry from the matching zone while preserving appearance overrides.

Replacements match exact NIF names; a null local replacement retains the original. Asset copying places selected dependencies in the host library without deleting sources or granting redistribution rights.

Set **Source Plugin Directory** to this repository and supply explicit export/converter/cache/interpreter paths. Preparation/imports run only when invoked. Inspect manifests first. Background preparation may continue after closing the tab; imports/mesh builds can block the editor thread.

Shared-preset actions can update multiple profiles. Review their affected scope; host presets belong in host data. Existing named demonstration presets are not recommended defaults for independent projects.

## Inspection and performance

Authorized controls provide flight, fixture identification, copying source NIF names, and regeneration. Inspection reports IDs, placement, scale, mesh/materials, LODs, and replacement state. Bounds picking is identification rather than exact triangle/leaf-alpha picking.

Connected tools require server privilege acknowledgement. Offline development requires explicit offline/developer flags. UI input cannot grant privilege. See [ADMIN_COMMANDS.md](ADMIN_COMMANDS.md).

Render distances retain entity state while culling presentation. Scenery instancing, grass density/culling, LOD, and Nanite are independent controls. Measure them in the host scene; blanket tuning and local benchmark claims do not belong in core defaults.
