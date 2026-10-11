# Networking

`UDaocNetworkSubsystem` owns the TCP session, authentication, roster, world initialization, entity state, appearance/equipment updates, and combat traffic. It is a game-instance subsystem with a game-world tick. Pawns and UI consume it; zone renderers do not own connections.

The session survives ordinary map travel and renderer replacement. Remote entities use a transient rendering actor. `UDaocNetworkComponent` remains a serialized compatibility shell and does not open a second connection.

## Configuration and integration

Use a private copy of [CamelotLogin.example.ini](CamelotLogin.example.ini). Settings come from Game.ini, an optional `-CamelotConfig` file, and command-line overrides. A nonempty `CAMELOT_PASSWORD` overrides the configured password. Never commit/log credentials or put passwords in command-line arguments.

Server addresses currently require numeric IPv4. Supply the host's server, maps, catalogs, and credentials explicitly; script defaults are not portable configuration.

```cpp
auto* Session = UDaocNetworkSubsystem::Get(this);
Session->SetCredentials(Account, Password, Character);
Session->Connect();
```

Observe `ConnectionState`, `LastNetworkError`, and `OnConnectionChanged`. `AccountCharacters` exposes the roster; an empty selection waits for `SelectCharacter`. Selection sends the canonical server name. Disconnect clears session/entity state.

## World and rendering

`Data/WorldZones.json` resolves server region/position into destination metadata. The host supplies prepared profiles and the gameplay map. `bWorldReady` gates initialization until terrain/collision are ready; `WorldRevision` handles repeated transitions and reused IDs. Unavailable/unsupported destinations fail explicitly. Runtime travel never imports assets.

`bManageZoneTravel=false` retains explicit coordinates for isolated fixtures. Conversion uses the active renderer's transform/scale. Render-distance culling preserves entity state and does not expand the server's replication range.

Prepared live mobs and remote players use the shared GPU crowd presenter by default. The local player and selected target keep full presentation; unsupported appearances and exhausted budgets fall back automatically. Appearance changes, combat events, nameplates and despawning retain server object IDs. See [CROWD_RENDERING.md](CROWD_RENDERING.md) for preparation, routing limits and metrics. The classic wire protocol is unchanged.

## Verification

Recoverable gameplay payload errors appear in Unreal's log with opcode, payload size, reason and occurrence count. The native queue coalesces repeated opcodes and holds at most 32 entries; Unreal limits each opcode to one warning per five seconds. Logs exclude packet bodies, credentials and character identities. Rejections preserve confirmed state and continue with the next complete frame. Invalid framing, handshake failures, timeouts and server-requested disconnects remain terminal because continued processing cannot establish a valid session or safe stream alignment.

Native tests cover framing, fragmented replies, entity state, and version-specific layouts. `DAoC.Network.SocketWorldEntry` adds Unreal socket/rendering checks requiring imported assets. `DAoC.Network.CamelotCoreIdentity` checks standalone plugin discovery, catalog availability, and the old-module redirect. Invoke live tests deliberately using disposable data.

See [COMBAT.md](COMBAT.md), [FRONTEND.md](FRONTEND.md), and [STARTING_LOCATIONS.md](STARTING_LOCATIONS.md).
