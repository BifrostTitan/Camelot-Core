# DOLSharp world-update snapshots

[rvr-performance.patch](rvr-performance.patch) contains the cumulative radius-query, cache-membership, snapshot and cache-key optimizations with regression tests. Packet formats and gameplay rules are preserved.

World updates check timestamp age before resolving region/object entries. Each pass takes a fresh snapshot using rented arrays cleared and returned on disposal, including exceptions. The cache lock is released before lookup, sending or removal. Fresh snapshots preserve additions made by earlier sends. Owned pets, ordering, visibility and update intervals retain their existing behavior.

## Measurements

Actual world-update calls used synthetic players and mock packet sends to isolate cache work. Optimized .NET 10 assemblies, tiered compilation disabled, five rounds of 1,000 warmed operations; medians. Empty NPC/item/door spatial results isolate cache processing.

| Cached players | Old entries | Before | After | Time reduction |
| --- | ---: | ---: | ---: | ---: |
| 1,000 | 0% | 0.3496 | 0.2066 | 40.9% |
| 1,000 | 5% | 0.3386 | 0.1982 | 41.5% |
| 1,000 | 100% | 0.3639 | 0.3276 | 10.0% |
| 1,500 | 0% | 0.5477 | 0.3347 | 38.9% |
| 1,500 | 5% | 0.5425 | 0.2983 | 45.0% |
| 1,500 | 100% | 0.5069 | 0.4954 | 2.3% |

Warm allocation fell from 263,336 to 199,240 bytes/update at 1,000 cached players and from 393,216 to 297,120 at 1,500: approximately 24% less. Cold rentals may allocate.

## Reproduce and validate

Set `$ServerLib` to independently built DOLSharp assemblies. Cache benchmarks also require `Tests.dll`. Synthetic harnesses do not connect to a live server.

```powershell
$env:DOTNET_TieredCompilation = '0'
dotnet run --project tests/server_cache_benchmark/Benchmark.csproj -c Release -p:ServerLib=$ServerLib
Remove-Item Env:DOTNET_TieredCompilation
```

Regressions cover ordering, filtering, boundaries, owned pets, concurrent access, fresh snapshots and equivalent keys. Baseline and patched suites retain two existing failures: `ApplyEffectOnTarget_50ConBuffOnL50NPC_51Constitution` and `GetArmorAbsorb_LevelZeroNPCHasOneConstitution_Circa5Percent`.

Results exclude socket transmission and live battlefield contention; they do not establish whole-server throughput or client FPS. Validate both classic and enhanced clients in the intended deployment.
