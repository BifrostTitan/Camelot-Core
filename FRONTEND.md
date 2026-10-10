# Frontend integration

`ADaocFrontendHUD` supplies login, realm selection, roster, creation, preview, and deletion. `UDaocNetworkSubsystem` owns transport and authenticated state. UI upgrades preserve that boundary.

## Host setup

Enable `UseFrontend` in `[Camelot.Login]`; `AutoConnect=false` begins at the login form. Launcher options select frontend or direct entry. Keep credentials in private configuration or `CAMELOT_PASSWORD`.

Use a login stage with `ADaocLoginGameMode` and a separate gameplay map configured on the session. Hosts own maps, preview environments, theme assets, and startup settings. `tools/setup_client_maps.py` is an explicit authoring helper, not a runtime requirement for every consumer.

Entry follows selection, destination lookup, profile loading, collision readiness, and pawn initialization. `bWorldReady` gates gameplay. Missing/unsupported destinations report an error instead of selecting unrelated terrain.

## Themes and creation

`UDaocFrontendTheme` maps semantic images and realm preview stages to soft references. Enhanced entries can fall back to classic entries. Keep artwork in host data assets. Scene-capture previews display RGB without using capture alpha as UI transparency.

`Data/CharacterCreation.json` offers all 47 advanced classes directly, using the server's eligible races among the 18 supported appearance races. Base classes are excluded. Valkyrie and Bainshee are available for female characters; changing race or gender selects a valid class when necessary. The server validates names, race/class eligibility, attributes, and starting destinations. Creation/deletion refresh the roster. Preview and world appearance share model/equipment catalogs. Reimport creation rules into the host's theme assets after changing this catalog.

Preparation/import tools include `prepare_frontend.py`, `import_frontend.py`, `prepare_customization.py`, and `import_customization.py`. Supply explicit paths and prepare character/equipment catalogs first. Original artwork is not bundled with core source.

## Validation and limits

The protocol handles discovery, roster, name checks, creation, deletion, and selection with version-specific packing. `server/realm-discovery.patch` is a scoped compatibility reference, not automatic deployment.

Native tests cover fragmented replies and selection/creation layouts. Unreal theme/customization checks require imported assets. Exact facial deformation and eye/skin recoloring remain incomplete; transmitted data does not establish a matching visual effect. Cook the host's maps, themes, previews, and catalogs explicitly.
