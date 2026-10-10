# Gameplay UI

`ADaocCombatHUD` presents the existing combat and network interfaces through Slate. Host projects supply artwork and optional `UCamelotUITheme` data assets; the core supplies reusable `SCamelotPanel` and `SCamelotResourceBar` widgets. Panels have solid backgrounds, including administration and the fixture inspector.

## Controls and presentation

- **Left Alt** toggles the cursor for buttons, hotbar assignment, and clickable nameplates.
- **Enter** opens chat; Enter sends, and Escape returns to gameplay. Movement and camera input pause while typing. Failed sends preserve the draft.
- **1–8** activate the default bar. Two optional bars use **Shift + 1–8** and **Ctrl + 1–8**. Bar visibility is exposed through `SetVisibleHotbarCount` for a future settings panel; gameplay has no add/hide buttons.
- Drag an icon from Skills onto a hotbar slot to assign its learned ability. Drops replace only the destination slot; other components, stale abilities, specializations, and hidden bars cannot receive assignments. Slots show mapped artwork, cooldowns, and name/level tooltips.
- **Ctrl + O** clears the hovered visible hotbar slot. Clearing requires the cursor and is disabled during chat/admin interaction. It does not clear an entire bar or send a gameplay request.
- **I** or **Character menu** opens/closes the character panel and makes the cursor available. Its widget switcher selects scrollable **Character**, **Skills**, **Effects**, and **Equipment** pages. Opening the menu allows gameplay input; chat continues to pause it while typing.
- **Tab**, **T**, and **Backspace** retain target cycling, self-targeting, and target clearing. **F** retains attack toggle.
- The player status box combines health, mana, endurance and the selected target's name/health. Resources display reported percentages; unknown values show a dash. Casting, effects, and feedback follow existing server state; cancelable effects offer a cancellation request.

Character shows progress/resource totals and the character-selection snapshot's class, race, and level. Skills groups learned specializations, spells, styles, songs, and abilities, with icons on the left, names in the middle, and levels/bonuses on the right. Supply artwork through the theme's `AbilityIcons` map, keyed by reported icon ID. Unmapped artwork uses a neutral placeholder; no atlas-index guesses are made. Effects offers cancellation for effects the server marks cancelable. Equipment presents the player preview, equipment slots, backpack, drag/drop, stack splitting, item use, and server item details; see [INVENTORY.md](INVENTORY.md). Full live attributes, guild details, realm rank and party state remain unavailable. Skills displays learned ranks; purchases use the separate server-driven [trainer window](TRAINING.md). Gameplay panels omit control tutorials and implementation-status text.

The chat panel retains the latest 100 server messages, including their chat categories, across pawn replacement and zone travel. Its virtualized list lays out visible rows and reuses retained rows as combat messages arrive. Readers keep their scroll position; the view follows incoming messages when already at the end. Disconnect clears the history. Ordinary text and commands such as `/group`, `/guild`, and `/send` use the existing classic-compatible command packet. Input is limited to 254 printable ASCII characters; the server controls channel routing and permissions. Incoming text is displayed as plain text.

## Crowd bounds

Nameplates require a visible, registered rendered body. They respect hidden bodies, camera projection, scene occlusion, and distance, and avoid overlapping labels. Known player realms select friendly/hostile colors. Name and guild appear without a background or health bar by default. Set `bShowNameplateDetails` to show the plate and reported health; `bShowNameplates` still hides the entire label. Selecting a target continues to show its reported health in the status box. Selected targets receive first overlap priority.

Names use a bold font with a small shadow. Same-realm players and mobs are cyan; opposing-realm players are red. Other mobs use gold/yellow, faded royal blue, light green, gray, orange, red, purple, and dark purple difficulty colors. Unknown realm/level data stays neutral. The palette is editable through the theme's Nameplates properties. Guild text remains muted.

Mob ranges follow DOLSharp's documented classic band width (`player level / 10 + 1`), with yellow ending at the player's level and orange starting above it. Gray uses `GameLiving.NoXPForLevel` exactly through level 50, with its `GetConLevel <= -3` fallback beyond that. For example, at level 40: 36–40 yellow, 31–35 blue, 26–30 green, and 25 or lower gray. Dark purple extends the top purple range for especially large level gaps; it is a visual difficulty cue, not a guarantee about solo outcomes. Labels use reported levels; server effective-level overrides and group XP rules can affect actual rewards.

Defaults are a 4,000 cm range, at most 48 candidate labels, and six occlusion traces per frame. Candidates and interface state refresh at 10 Hz; projection follows every rendered frame. Occlusion checks rotate through candidates. Set `bShowNameplates`, `bShowNameplateDetails`, `NameplateDistance`, `MaximumNameplates`, and `NameplateTraceBudget` on the HUD to tune a host project. Labels use Canvas rather than an actor/widget for every remote player.

During zone travel or hidden gameplay, the HUD clears the target snapshot, nameplate candidate indices, and occlusion cache together. Gameplay returning triggers an immediate refresh. Both nameplate drawing passes reject stale indices, including an empty or shrinking target list. `Camelot.UI.NameplatesAcrossZoneTravel` exercises these transitions through the real HUD.

## Integration and limits

The existing game mode uses this HUD. Custom game modes can select `ADaocCombatHUD` and use a pawn with `UDaocCombatComponent` attached to the session. Set `UITheme` before the HUD is constructed. `UIScale` defaults to 0.75 and accepts 0.5–1.5. Panels retain their viewport anchors as their contents shrink. `UIScale` and `bShowNameplateDetails` are Blueprint/config properties ready for a future settings page. Its palette, font size, and icon map belong to the host; changing the theme does not change game mechanics. Gameplay UI is hidden during the frontend.

Hotbars contain 24 independent bindings (eight per bar), with one visible by default. Automatic assignment is disabled by default; `bAutoFillHotbar` optionally fills the default bar. Assignments and visibility belong to the pawn component; persistence across pawn replacement/restarts remains outside this implementation. Loot, trading/storage, group management and settings UI remain future work. Live battle performance and keyboard/mouse interaction still need host playtesting.

`Camelot.UI.ChatAndNameplatePolicy` checks chat validation, session history, hotbar bounds, and label selection with 1,000 entities. `Camelot.UI.RenderPanels` captures 1080p/720p layouts, all menu pages with three bars, switcher selection, and admin opacity over contrasting backgrounds with a real RHI. `DAoC.Network.SocketWorldEntry` checks chat packets, rendered-body anchors, and third-bar ability requests using a local mock server and imported host assets.
