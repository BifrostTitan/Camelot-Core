# Inventory and equipment

The session owns confirmed equipment and the 40-slot backpack. `GetInventoryState` exposes server item names, levels, stack counts, models, dye/emblem, effects, weight, condition, durability, quality, and bonus percentages. Connected gameplay uses DOL's existing classic packets; server eligibility, stacking, exchanges, item effects, and stat changes remain authoritative.

## Player interface

Open **Character → Equipment** with **I** or the Character button. The page includes armor/accessory slots around a rotating player preview, weapon/quiver slots, a backpack grid, and selected-item actions.

- Drag items between slots to move, exchange, stack, equip, or unequip them according to server rules. Dropping replaces no local state before confirmation.
- Double-click a backpack item or choose **Equip** to request server-selected equipment placement. Double-click equipment or choose **Unequip** to request the first empty backpack slot. A full or unavailable backpack blocks this convenience action.
- Choose a quantity and **Split** to request movement into an empty backpack slot.
- **Use**, **Use 1**, and **Use 2** send the classic slot action and two charged-item action types. Server rules determine what the selected item supports.
- **Details** requests the server's full item description, including bonuses and restrictions. Requests are serialized because classic replies carry no slot/request ID. Responses must match the requested item name and unchanged inventory revision; unrelated or stale detail windows are ignored. An unanswered request can be retried after five seconds.

Tooltips and the selection summary show reported item metadata. `UCamelotUITheme::ItemIcons` maps server model IDs to host-owned textures. Without artwork, slots display the server item name. No item-icon atlas mapping is inferred.

The preview uses the existing character mesh, appearance, and equipment catalog. Its separate stage is independent of battlefield scale. A 512×640 capture updates at up to 15 Hz while visible; closing the panel or selecting another page releases its actor, lights, texture residency requests, and render target. It does not create previews for other players.

## Integration

`RequestMoveItem(FromSlot, ToSlot, Count, ExpectedRevision)`, `RequestUseItem(Slot, UseType, ExpectedRevision)`, and `RequestInspectItem(Slot, ExpectedRevision)` are Blueprint-callable session APIs. Pass the revision returned by `GetInventoryState`. Revisions also distinguish replacement connections. Count zero requests the whole stack; destination 100 requests server-selected equipment placement.

The parser supports version 1.68 and 1.72–1.74 inventory layouts, including the Catacombs armor-extension byte. Equipment/backpack refreshes replace their own ranges; subsequent update packets preserve unrelated slots. Empty records remove items. The ordered world-initialization reply confirms initially empty lists, since DOL omits packets for empty collections. Inventory packets continue to update the local rendered appearance, and ordinary equipment packets update remote bodies. Zone travel retains session inventory; disconnect/failure clears it. Storage-window contents do not overwrite owned inventory.

Only owned equipment slots and backpack destinations are enabled in these actions. Loot pickup, storage, trade, shops, currency, item destruction, and item-to-hotbar bindings remain separate future work.

## Verification

`daoc_inventory_protocol_tests` checks all supported layouts, fragmented packets, snapshot/partial updates, appearance updates, stack counts, classic action bytes, stale revisions, item details, malformed input, and cleanup. Existing character tests retain initial inventory/appearance coverage.

`DAoC.Network.SocketWorldEntry` verifies inventory reception and move/use/details requests through a local mock server, Slate drops, cross-session rejection, imported equipment, and preview allocation/release. `Camelot.UI.RenderPanels` captures the Equipment page at 720p alongside existing layouts. Mock rendering does not replace live server or mouse/keyboard playtesting.
