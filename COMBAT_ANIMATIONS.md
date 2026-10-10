# Animation and sound presentation

`UDaocCombatAnimationCatalog` maps confirmed session events to compatible clips/sounds. `UDaocCombatAnimationComponent` presents them for local and remote bodies. Ability requests do not play speculative hits.

## Integration

Assign the catalog, call `SetCharacter(mesh, modelId, serverObjectId)`, bind `HandleEvent`, and route locomotion through `UpdateLocomotion`. Reset on disconnect. Keep one presentation owner per visible body.

Styles and weapon families select attacks. Server cast, interrupt, spell-effect, death/revive, and sound packets drive other actions. Preserve source/target IDs and timing; discard delayed sounds after session/zone changes. Catalog keys `G_<index>` and `C_<index>` reference source animation-table columns. Unusual weapon families need explicit overrides; missing entries do not create invented clips.

## Import and compatibility

Use `tools/prepare_combat.py` and `tools/import_combat.ps1` with explicit paths. Alias clips only when hierarchy, bind pose, animation set, unit scale, and imported skeleton compatibility match. NPC rigs need compatible source animations.

Presentation uses a native blended clip instance for local players, remote players and NPCs. Attacks, casting and death blend over 0.12 seconds, hit reactions over 0.08, returns to locomotion over 0.14 and gait changes over 0.18. Walking/running retain normalized gait phase. Imported clips remain in place; confirmed positions drive movement. Animation clocks respect game pause and time dilation.

Remote players use the classic position/heading attack bit for combat idle. NPC packets lack that state, so confirmed melee involving the NPC retains a ready stance for ten seconds after its latest exchange. Movement, death and revive retain their existing rules. Ready stances use shared locomotion cohorts, leaving action slots for confirmed swings and reactions. Identical reactions received in one rendered frame restart once while every event and playback revision remains available.

Idle hits react immediately. During an outgoing attack, only the latest reaction waits for its completion. Another hit during a reaction restarts immediately from the displayed pose, including an unfinished blend. Death, revival and casting clear pending reactions. Damage does not independently cancel a server-confirmed cast.

The crowd presenter blends the same clips on the GPU and uses matching weighted CPU samples for weapon attachments. Existing saved crowd providers need the editor commandlet `-run=DaocLiveCrowd -UpgradeBlending` once; it backs up packages before adding four override layers. New providers include those layers automatically. Source animation export still approximates unavailable TCB tangents with shortest-arc SLERP.

Native tests cover fields/timing. Unreal tests require imported catalogs/clips and cover repeated-hit pose continuity, attack/reaction priority, interrupted crowd blends, death/revival and equipment reuse. Host cook rules must include dynamically loaded animation/sound assets. Import counts and local test history are not compatibility guarantees.
