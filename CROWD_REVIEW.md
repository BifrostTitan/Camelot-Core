# Crowd performance

The live renderer caches exact appearance identity, avoids redundant animation changes on hidden skeletal bodies, updates weapon instances incrementally, and resolves individual sound/visibility targets instead of rebuilding complete target lists. Appearance changes invalidate the cache; selection or fallback restores the confirmed clip and phase.

## Measured CPU improvement

Comparable warmed profiling windows used 1,000 equipped NPCs and remote players with four repeated appearances:

| CPU stage | Before ms/frame | After ms/frame |
| --- | ---: | ---: |
| Character refresh | 7.01 | 2.75 |
| Combat dispatch | 3.50 | 0.07 |
| Live crowd refresh | 7.24 | 2.02 |
| Appearance keys, included in refresh stages | 9.02 | 0.00 |
| Entire game thread | 30.23 | 18.70 |

Scopes overlap and must not be added together. The bounded CPU weapon-pose pool remained the largest measured crowd-specific cost at 4.62 ms/frame; weapon transforms cost 0.52 ms/frame.

## Rendering results and limits

Unprofiled measurements used UE 5.8.3 Development Editor, DX12/SM6, 1920×1080, RTX 3090 and i9-11900F, with a ten-second warmup and fifteen-second measurement. Classic loopback packets supplied movement, melee, hit reactions, death and revival.

| Workload | Mean FPS | Mean frame ms | p95 frame ms |
| --- | ---: | ---: | ---: |
| 1,000 equipped NPCs/remote players, four appearances | 52.6 | 19.026 | 24.190 |
| 200 normal-size NPCs, two appearances, radius 700 server units | 95.2 | 10.499 | 12.303 |

The comparable 1,000-character baseline measured 35.2 FPS, an approximately 49% improvement. All characters used GPU batches with no action-pool misses or full-presentation fallbacks. Results exclude server simulation, production terrain, UI, spell effects, hundreds of unique outfits and packaged performance. These are workload measurements, not frame-rate guarantees.

NPC packet size 50 represents normal scale. Half-size NPC results cannot serve as a normal-size baseline. Standalone renderer measurements also exclude networking and hit-reaction processing.

## Design constraints

Epic's crowd implementation uses prepared geometry, bone reduction, representation LODs and shared animation. Camelot Core keeps modular body, armor, hair and weapons to preserve equipment changes, and reserves full presentation for protected characters. Confirmed player combat requires independent action timing. Profile appearance diversity, active pose counts, pool misses and game-thread cost in each host scene. See [CROWD_RENDERING.md](CROWD_RENDERING.md).
