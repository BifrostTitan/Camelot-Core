# Mob movement

DOL owns mob routes, movement speed, combat and position. Enable its existing `LocalPathingMgr` with a matching 64-bit `dol_detour.dll` in the server runtime's `lib` directory and compatible `pathing/zoneNNN.nav` files. Restart the server after installing these files. Its startup log must select `LocalPathingMgr` and load the zone mesh; `NullPathingMgr` means routes are disabled. Zones without navigation data retain DOL's existing fallback behavior.

Build the native library from DOLSharp's `Pathing/Detour` directory. DOLSharp's README links its navigation generator and a prepared mesh archive. Use meshes built for the server's zone geometry. Local client scenery additions do not automatically update server navigation.

The server mass-spawn commands project generated positions onto nearby navigation surfaces instead of copying the spawning player's height. Region and heading are retained. Missing meshes or failed projections retain the original server fallback. Movement continues through classic-compatible waypoint packets, so both clients follow the same route.

The client interpolates those waypoints and samples the imported terrain's exact rendered triangles for ground mobs. A shared spatial floor cache preserves bridge/building surfaces, with at most 16 static collision queries per frame and 4,096 cached cells. Flying creatures and mobs already underwater keep their server altitude/depth. Both the native and GPU crowd renderers use these corrected feet positions. This does not add a CharacterMovement component or a separate AI/navmesh to each replicated mob.

`DAoC.Network.SocketWorldEntry` covers buried/floating mobs, airborne flags, elevated floors and GPU crowd presentation. DOLSharp's `UT_NpcSpawnNavigation` covers spawn height, region/heading preservation and unavailable/invalid projection fallback.
