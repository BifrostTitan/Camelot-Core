# Trainer window

Select a nearby trainer, then use **Interact** in the target frame or press **E**. The window opens only after the server sends training options. This works with the existing i50 trainer and normal class trainers; no server packet changes are required.

**Specializations** and **Realm abilities** have separate scrollable pages. Each line shows its current rank, selected rank and estimated point cost. Use its slider or minus/plus buttons, then **Train**. Specializations can allocate several ranks. Realm abilities offer one upgrade at a time because the classic packet reports only the next rank's cost and requirements.

The server remains authoritative. Multi-rank specialization training sends one classic request, waits for that line's confirmed rank increase, then sends the next. Other purchases remain disabled while an allocation is pending. An unchanged refresh, duplicate acknowledgement or another page's update cannot advance the queue. Missing confirmations stop it after five seconds and require renewed interaction before another purchase; rejection messages remain in chat. Completed purchases are retained if the window is closed partway through an allocation.

The window closes on Escape, Close, changing targets, leaving the trainer's 192-unit interaction range, combat/death, zone travel or disconnect. Closing discards unsubmitted slider selections and stops further queued requests. A request already accepted by the server cannot be undone by closing the window.

## Integration

The server must have populated skill tables. An empty `ClassXSpecialization` table produces no trainable class lines; missing ability, spell and style tables also leave skill menus empty. Restore those definitions on the server rather than inventing client-side skills. Existing characters acquire their class specializations and abilities through DOL's normal login refresh, then unlock additional spells/styles by training.

`tools/restore_server_skills.py` restores the ten empty skill tables from a reviewed JSON snapshot of the official `Dawn-of-Light/db-public` repository. Stop the server first, supply the database/source/backup/report paths, and use a fresh backup filename. It backs up the complete database, imports in one transaction and refuses to overwrite populated skill tables. Player records and world tables are preserved; realm abilities still require the server's normal points and prerequisites.

`UDaocNetworkSubsystem` exposes `RequestInteract`, `GetTrainerState`, `RequestTraining` and `CloseTrainer`. Training requests include the snapshot revision; stale selections cannot purchase a different line after an update. Session revisions are distinct. The reusable Slate window uses the existing UI theme and gameplay scale.

Trainer lists use server opcode `0x7B`; interaction and training use classic client opcodes `0x7A` and `0x7C`. Realm ability indices use DOL's offset of 100. Full-width `UpdatePoints` totals supersede the trainer header's one-byte totals, preserving i50 specialization balances above 255. Live player-sheet updates provide the training level after an i50 promotion.

Point estimates use the reported next cost and increasing specialization rank costs. Server auto-training credits are not advertised by these packets; the slider conservatively uses the available point total rather than inventing credits. Champion training trees and future spell/style previews use different packets and are outside this window.

`daoc_trainer_protocol_tests` exercises versions 1.68, 1.72, 1.73 and 1.74, exact wire bytes, list validation, costs, queued acknowledgements, timeouts and lifecycle cancellation. `Camelot.UI.RenderPanels` checks trainer rows and closure and captures both pages for layout review.
