# DOLSharp radius queries

[rvr-performance.patch](rvr-performance.patch) contains the cumulative radius-query, cache-membership, snapshot and cache-key optimizations with regression tests. Packet formats and gameplay rules are preserved.

Player-cache cleanup uses a `HashSet<GamePlayer>` instead of repeatedly scanning visible-player arrays. Radius queries use a pooled, query-owned membership set instead of repeated `ArrayList.Contains` inside cell locks. Neighboring zones share the set, preserving ordered results while removing quadratic duplicate detection. Up to 32 sets are retained and cleared after use.

## Measurements

Actual `Zone.GetObjectsInRadius` calls used synthetic active players and radius 4,096. Optimized assemblies, five rounds of 1,000 warmed queries; median milliseconds per query.

| Nearby players | Cell path | Before | After | Speedup |
| --- | --- | ---: | ---: | ---: |
| 1,000 | Distance checked | 0.7411 | 0.0690 | 10.7x |
| 1,000 | Fully enclosed | 0.5428 | 0.0545 | 10.0x |
| 1,500 | Distance checked | 1.5626 | 0.1047 | 14.9x |
| 1,500 | Fully enclosed | 1.5352 | 0.0924 | 16.6x |

Warm queries add 72 bytes per call for scope/pool bookkeeping. At 100 players, the fully enclosed path increased from 0.0119 to 0.0139 ms.

## Reproduce and validate

Set `$ServerLib` to independently built DOLSharp assemblies. Cache benchmarks also require `Tests.dll`. Synthetic harnesses do not connect to a live server.

```powershell
dotnet run --project tests/server_radius_benchmark/Benchmark.csproj -c Release -p:ServerLib=$ServerLib
```

Regressions cover ordering, filtering, boundaries, owned pets, concurrent access, fresh snapshots and equivalent keys. Baseline and patched suites retain two existing failures: `ApplyEffectOnTarget_50ConBuffOnL50NPC_51Constitution` and `GetArmorAbsorb_LevelZeroNPCHasOneConstitution_Circa5Percent`.

Results exclude socket transmission and live battlefield contention; they do not establish whole-server throughput or client FPS. Validate both classic and enhanced clients in the intended deployment.
