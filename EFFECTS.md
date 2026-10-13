# Combat particles

`UDaocEffectCatalog` contains host-owned Niagara systems and classic effect bindings. `spells.csv` supplies client presentation IDs and casting, projectile, impact and resist stages; `speffects.csv` selects NIFs, attachment nodes, duration, playback rate and auto-scaling. These IDs are distinct from learned ability and icon IDs.

The offline converter reads particle textures, blending, initial color, emission rates, capacity, lifetime variation, spawn volume, velocities, gravity, rotation and color/grow/fade modifiers. UE5.8 lightweight Niagara emitters evaluate the supported motion on the GPU. Original textures provide the artwork. Effects do not create gameplay damage, lights, collision, shadows or per-particle actors.

## Runtime

Confirmed network events enter one world effect subsystem for local players, remote players and NPCs. The prepared catalog loads asynchronously. Pooled components reuse imported systems and materials; casting does not extract files, build shaders or synchronously load individual effects.

The default limits are 128 active systems, 32,768 reserved particles, 16 admission attempts per frame and 1,024 queued requests. Local-player involvement receives priority and can replace remote effects. Remote requests are distance filtered and older requests expire. Particle reservations include the lifetime tail, and forced replacements terminate their particles before releasing capacity. These limits bound effect workload, not total scene cost or a promised frame rate.

AoE result packets coalesce the caster's release within a rendered frame while retaining separate projectile and impact requests for each target. Projectile arrival uses server travel time. Cast interruption cancels gathering, and zone/disconnect cleanup cancels active and delayed work. Entity removal invalidates its pending attachments. Sound suppression does not suppress visuals.

Visible characters use skeletal sockets. Instanced characters reuse the existing shared weapon-pose cache, without evaluating another skeleton per effect. Hand effects use an approximate body-relative position if no sampled socket is available. `SetCatalog` lets consumers bind their own prepared effects. The subsystem's Game config properties set its runtime budgets.

## Import

Initialize the required Git submodules, build the portable adapter in `Extras/EffectImport` with CMake, and install Pillow for the Python preparation step. Use extracted client tables and effect textures:

```text
daoc_effect_export <extracted-client/effects> <decoded.json>
python Extras/EffectImport/prepare.py --client <extracted-client> --decoded <decoded.json> --output <prepared-folder>
UnrealEditor-Cmd <host.uproject> -run=DaocEffects -Input=<prepared-folder/manifest.json> -AllowCommandletRendering -RenderOffscreen -unattended
```

Import uses a real rendering device to validate GPU-only emitters. Save and close the host editor first. The commandlet writes `/Game/DAoC/Effects/DA_Effects` and its systems/materials; texture assets use the existing object-texture importer. Include the catalog and its hard-referenced dependencies in host cooking rules. Source artwork and generated content belong to the host project.

`coverage.json` records conversion gaps. This pass supports alpha/additive sprite effects with constant directional gravity. Mesh layers, mesh particles, point gravity, radial forces and unusual blend modes remain incomplete. Legacy angular sampling is approximated by Niagara cones, and variable-lifetime grow/fade uses a curve normalized to nominal lifetime. Animated NIF scene-node motion and persistent pulse/focus visuals need additional conversion. A spell mapping can therefore contain only some of its original visual layers.

`DAoC.Effects.BudgetAndLifecycle` checks actual particle generation, casting/interruption, staged arrival, AoE coalescing, a thousand-caster request burst, resource bounds and cleanup with imported systems. Editor and game builds pass. Representative fire and poison sprites were checked in an offscreen render; complete visual parity and battlefield frame-time measurements remain separate validation work.

The legacy field interpretation was cross-checked against [OpenMW's NIF particle implementation](https://github.com/OpenMW/openmw/blob/master/components/nifosg/particle.cpp) and [loader](https://github.com/OpenMW/openmw/blob/master/components/nifosg/nifloader.cpp). Epic's [Niagara systems-as-a-service guidance](https://dev.epicgames.com/community/learning/tutorials/0q6O/unreal-engine-optimizing-niagara-systems-as-a-service) describes further batching options for high event counts.
