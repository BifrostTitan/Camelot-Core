# Crowd rendering

`UCamelotCrowdComponent` renders repeated equipped appearances with UE 5.8 instanced skinned meshes and shared GPU animation tracks. Body, armor and hair retain their meshes, material parameters and hidden body sections. Static weapons use instancing and a bounded CPU pose pool for hand attachments. It supports conventional skeletal rendering and Nanite; no MetaHuman or Mass plugin dependency is required.

Epic's [MetaHuman crowd workflow](https://dev.epicgames.com/documentation/metahuman/create-metahuman-crowds-in-unreal-engine) uses full actors nearby, prepared GPU animation at distance, and limits on animation diversity. Nanite alone does not remove per-character animation and component costs.

## Prepare an appearance

1. Assemble one template using the existing equipment renderer. Each batch represents that exact body, equipment, hair, textures and dyes. Skeletal children must attach directly at the body origin; rigid weapons attach to named body sockets/bones.
2. In the editor, call `PrepareTemplateForInstancing`. This enables unified bone maps on the selected meshes and the necessary material usage flags. Set `bSavePackages=true` before cooking; originals are backed up under the host's `Saved/CrowdPreparationBackups`. It does not reduce mesh detail or globally enable Nanite.
3. Call `PrepareAnimationSet` with compatible animation sequences and a new `/Game/` package path. Existing assets are not overwritten. The saved `UCamelotCrowdAnimationSet` references the per-mesh providers, meshes and clips needed by a cook. Load existing sets rather than recreate them.
4. Create the crowd component on a visible actor. Pass the equipped template, the saved set's providers, and a track budget to `Initialize`. Initialization rejects missing unified bone maps, incompatible provider layouts and unsupported attachment transforms. Use the ordinary presenter when initialization fails. Material overrides are copied into the batch so destroying the template does not invalidate them.

Preparation is an editor operation. Packaged consumers load saved assets; they must not generate providers during play. Asset preparation, battlefield layouts and appearance presets belong to the consumer project.

## Supply state and animation

- `AddCharacter`, `UpdateCharacter` and `RemoveCharacter` use stable consumer/server object IDs. Updates supply world transforms and a track index; IDs remain valid when other instances are removed.
- `PlayTrack` starts a looping or clamped sequence for a shared cohort. Allocate independent tracks for unrelated one-shot events so one attack never restarts someone else's animation. The budget is 1–128 tracks per appearance batch, independent of character count. Split a batch or retain full presenters when the required timing diversity exceeds that budget.
- For independently timed confirmed actions, call `ConfigureActionPool` after initialization. The first requested tracks remain locomotion cohorts; the rest become bounded action slots. `PlayCharacterAction` shares only compatible sequence/rate/phase/hold states, returns completed actions to locomotion, and optionally holds the final pose for death. `SetCharacterLocomotion` changes the return track without cancelling an action; explicit interruption is required to leave a held pose. No combat state is simulated here.
- Exact animation phase is the default. Consumers may explicitly allow a small phase tolerance for distant players; retain exact presentation nearby. Pool exhaustion returns `false` without changing the character or restarting another player's action. Promote to the ordinary presenter or another prepared batch and preserve the confirmed event. `GetActionPoolMissCount` exposes capacity failures.
- Use `MoveCharacter` for interpolated/authoritative transforms while preserving an active action. It maintains previous-frame instance transforms and accepts an explicit teleport flag. `UpdateCharacter` remains the manual track-changing API and cancels a managed action. Modular parts always animate together; only occupied tracks evaluate CPU weapon attachment poses.
- Keep local, selected and nearby interactive characters on the existing skeletal/combat presenter. Consumers own significance, representation transitions and event-to-clip mapping. Rendering owns no connection, simulation or authoritative combat state.

## Live mobs and remote players

`UDaocNetworkSubsystem` automatically routes eligible live NPCs and remote players through `UCamelotLiveCrowdPresenter`. It uses the existing replicated transforms, appearance revisions and confirmed combat animation state. It does not change packets, server simulation, sounds or effect dispatch. Offline consumers can still use the crowd component directly.

Each batch represents an exact body/equipment/customization appearance. Armor, hair, dyes, hidden sections and weapons are copied from the established equipment presenter. Size and world transform are per character. Appearance changes remove the old instance and rebuild the representation. The local player and selected target always retain full skeletal presentation; `FullCharacterDistance` optionally reserves an additional radius in server units.

The live adapter supplies idle/walk/run cohorts and independently timed confirmed melee, ranged, casting, interruption, death and revive states. Looping casts and held death poses do not return to locomotion early. Promotion restores the current animation position. Unsupported clips/assets, unfinished provider compilation, material incompatibility and exhausted track/appearance budgets retain the full presenter. Deletion, culling, disconnect and renderer/world replacement remove instances; empty appearance batches expire after two seconds.

Defaults on the network subsystem:

| Setting | Default | Purpose |
| --- | ---: | --- |
| `bUseLiveCrowd` | `true` | Enable prepared live GPU presentation |
| `FullCharacterDistance` | `0` | Optional additional full-presentation radius; local/selected protection is unconditional |
| `LiveCrowdMaxBatches` | `32` | Bound concurrent exact appearances; remaining characters use full presentation |
| `LiveCrowdPhaseTolerance` | `0.04` seconds | Maximum visual phase difference allowed when sharing compatible actions; set zero for exact phase |

Each live appearance has 128 tracks: 16 phased idle/walk/run/combat-idle tracks and 112 action slots. Combat idle remains a shared loop between confirmed swings. Only occupied tracks evaluate weapon attachment poses. Phase tolerance affects visual timing only. `LiveCrowdCharacterCount`, `LiveCrowdBatchCount`, `LiveCrowdPoseCount` and `LiveCrowdPoolMissCount` expose actual routing and capacity. Matching appearances share poses; hundreds of unique outfits are not equivalent to hundreds of repeated models.

Each prepared track supports four weighted clip layers. New actions blend from the current mix, including unfinished hit reactions, and completed transitions collapse to one clip. Compatible completed action tracks merge within the original phase tolerance to free slots; merging cannot accumulate timing drift. Allocation also reclaims completed movement transitions before reporting pool exhaustion. Weapon poses use the same clips, positions and weights. `GetCharacterPose` exposes all contributors for continuous promotion. Overflow retains full presentation with its current pose. Recently promoted equipment is cached for three seconds, hidden, detached and not ticking while dormant.

Visible native blends finish before a character joins the GPU batch. Offscreen characters join using their confirmed clip and phase even when Unreal has paused their native pose updates; shadow visibility alone cannot keep them on full presentation. CSV counters distinguish pending native blends, missing providers/clips, retry cooldowns and action/locomotion fallbacks.

### Prepare live assets

Run the editor commandlet against the host's imported equipment and combat catalogs:

```powershell
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $HostProject `
    -run=DaocLiveCrowd '-Models=players' -unattended -NullRHI `
    -CamelotNoAutoConnect -DaocOffline
```

`players` selects the equipment catalog's figure models. Numeric model lists and `-Output=/Game/...` support project-specific selection. `-ListOnly` lists candidates without preparing them. The optional `-ContentRoot` mount is intended for isolated fixtures. Models lacking body or idle assets are reported and retain full presentation. Other preparation failures return a nonzero exit code.

The commandlet saves compatible combat/locomotion providers for each visible body, armor and hair mesh, reusing matching providers across variants. Existing incompatible providers require a new output path. It enables instancing mesh/material settings and backs up modified originals under `Saved/CrowdPreparationBackups`; catalog backups go under `Saved/LiveCrowdPreparationBackups`. Rigid equipment materials are prepared for instanced-static usage by default; `-SkipRigidMaterials` leaves that step to explicit appearance preparation. Runtime never authors providers or changes material usage.

For existing single-layer providers, run the same editor commandlet with `-UpgradeBlending`. It checks the catalog's provider packages, backs them up under `Saved/AnimationBlendAssetsBackup`, and saves four override layers. Meshes and clip lists are retained. Older single-layer providers remain usable but promote interrupted blends that exceed their two-clip transition capability.

`DA_LiveCrowdCatalog` is assigned to the root client asset catalog so provider/mesh/animation dependencies are reachable by cooking. A custom host must retain its root catalog as a cook dependency or explicitly configure the live catalog. A successful editor run does not establish a packaged/Shipping performance result.

Full fallback characters keep separate transient render actors attached to the zone transform. This preserves independent UE animation update-rate scheduling; equipped modular parts share their character owner.

Live presentation caches exact appearance identity between changes. Confirmed combat continues advancing while the GPU owns the pose; selection/fallback restores the full skeletal clip and phase. Weapon transforms use incremental instance updates. Mapped sounds and line-of-sight checks resolve individual targets instead of building the entire target list per event. `CamelotNetwork`, `CamelotLive` and `CamelotCrowd` CSV categories expose these CPU stages during profiling.

`DAoC.Network.SocketWorldEntry` with `-CamelotRequireLiveCrowd` exercises 200 mobs plus an equipped remote player through classic loopback packets, including movement, selection, culling, melee, cast, death/revive and deletion. `Camelot.Crowd.LivePresentation` checks saved-provider routing, exact appearance limits, action-pool exhaustion, preserved fallback phase, loop interruption and teardown. The ordinary socket regression remains available without that flag.

## Performance and validation

[Measured performance](CROWD_REVIEW.md) records CPU improvements, representative results and workload limits. Repeated appearances share batches and poses; unique outfits and independently timed actions increase cost. Profile the intended scene at consistent resolution, lighting and character scale.

Validation covers provider reloads, appearance invalidation, modular poses, material lifetime, fallback phase, movement, combat/casting, death/revival, capacity failure and teardown. Socket checks exercise client integration using mock traffic; they do not establish server throughput or packaged performance. Keep generated traces, captures and scene presets outside published source.
