# DOLSharp cache-key comparisons

[rvr-performance.patch](rvr-performance.patch) contains the cumulative radius-query, cache-membership, snapshot and cache-key optimizations with regression tests. Packet formats and gameplay rules are preserved.

Default tuple comparison boxes numeric components. `WorldObjectKeyComparer` directly compares region/object IDs and packs them into a unique 32-bit hash. Equivalent independently constructed keys retain the same identity. Both object and housing cache initializers supply the comparer; key types, public APIs and locking remain unchanged.

## Measurements

Actual world-update calls used synthetic players and mock packet sends to isolate cache work. Optimized .NET 10 assemblies, tiered compilation disabled, five rounds of 1,000 warmed operations; medians. Broadcast results isolate timestamp writes with fresh tuple keys; the baseline already includes pooled snapshots.

| Cached players | Old entries | Before ms/update | After ms/update | Before bytes/update | After bytes/update |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 5% | 0.1934 | 0.0823 | 199,240 | 55,240 |
| 1,000 | 100% | 0.3316 | 0.1947 | 199,240 | 55,240 |
| 1,500 | 5% | 0.3214 | 0.1298 | 297,120 | 81,120 |
| 1,500 | 100% | 0.5022 | 0.3213 | 297,120 | 81,120 |

| Recipients | Before ms/broadcast | After ms/broadcast | Before bytes/broadcast | After bytes/broadcast |
| --- | ---: | ---: | ---: | ---: |
| 1,000 | 0.1098 | 0.0265 | 168,000 | 24,000 |
| 1,500 | 0.1792 | 0.0404 | 252,000 | 36,000 |

At 1,000 cached players with 5% old entries, update time fell 57% and allocation fell 72% beyond pooled snapshots. Movement timestamp writes still allocate one tuple per recipient.

## Reproduce and validate

Set `$ServerLib` to independently built DOLSharp assemblies. Cache benchmarks also require `Tests.dll`. Synthetic harnesses do not connect to a live server.

```powershell
$env:DOTNET_TieredCompilation = '0'
dotnet run --project tests/server_cache_benchmark/Benchmark.csproj -c Release -p:ServerLib=$ServerLib -- --writes
Remove-Item Env:DOTNET_TieredCompilation
```

Omit `--writes` to measure world updates.

Regressions cover ordering, filtering, boundaries, owned pets, concurrent access, fresh snapshots and equivalent keys. Baseline and patched suites retain two existing failures: `ApplyEffectOnTarget_50ConBuffOnL50NPC_51Constitution` and `GetArmorAbsorb_LevelZeroNPCHasOneConstitution_Circa5Percent`.

Results exclude socket transmission and live battlefield contention; they do not establish whole-server throughput or client FPS. Validate both classic and enhanced clients in the intended deployment.
