# Combat

Connected combat uses the existing DOL session. The client sends requests; the server owns damage, healing, eligibility, range checks, costs, rewards, and confirmed results. Presentation follows server events.

## Interfaces

| Layer | Responsibility |
| --- | --- |
| `Private/Camelot` | Portable parsing, learned catalogs, state, requests and bounded events |
| `UDaocNetworkSubsystem` | Transport, coordinate/entity lookup, Blueprint state and events |
| `UDaocCombatComponent` | Local targeting, three hotbars/input and feedback |
| `UDaocCombatAnimationComponent` | Confirmed animation and sound |
| `ADaocCombatHUD` | Shared status box, menu pages, hotbars, effects, chat, and bounded nameplates |

For a custom pawn, add `UDaocCombatComponent`, call `SetNetworkClient`, and identify the local pawn through `LocalPlayerPawn`. Bind your inputs or use `BindDefaultInput`. Build UI from `GetState`, `GetLearnedAbilities`, `GetEffects`, and `GetTargets`. Use `AssignHotbar` with current learned entries and `OnCombatEvent` for presentation. Remote NPCs consume server events rather than local input controllers.

Built-in controls provide targeting, attack toggle, and eight slots per bar with up to three bars. Auto-fill populates the default bar as a testing convenience. Extra bars use Shift/Ctrl shortcuts and independent learned-ability assignments. A queued request does not prove acceptance.

## State and protocol rules

Resources are server percentages, not inferred absolute totals. Effects remain until server updates remove them. Attack/death/revive follows confirmation. Cooldown/cast clocks support display and request gating, not gameplay simulation. Entity removal and disconnect clear stale state.

Hybrid ability indexes are relative to the first non-specialization at the request boundary. List-spell requests use required level and line index; cooldown identity additionally uses spell position. Icons, database IDs, and animation IDs are distinct. Multi-page catalogs commit only when complete; replacements rebuild session-local bindings.

Requests preserve feet position, heading, and visibility flags. Visibility uses scene collision; unknown entities are blocked. Melee events do not supply absolute damage, so UI must not invent damage numbers from health percentages.

## Validation and limits

Native tests cover indexes, cooldowns, effects, casts, death/revive, visibility fields, malformed input, and cleanup. Unreal integration checks session/component/rendering boundaries; imported content and live behavior need separate validation.

Inventory and equipment use confirmed session state and classic item requests; see [INVENTORY.md](INVENTORY.md). Loot is not implemented. Persisted hotbars and broader effects remain incomplete. See [UI.md](UI.md) and [COMBAT_ANIMATIONS.md](COMBAT_ANIMATIONS.md).
