# Destination selection

A region contains multiple zones. Server region plus absolute position determines the rendered zone. Region IDs, zone IDs, and map names are distinct.

Creation sends destination region and start flags; server rules choose spawn/bind locations. The encoder maps an expansion flag with a default/mainland region to the corresponding expansion region while preserving explicit custom regions.

Lookup uses [Data/WorldZones.json](Data/WorldZones.json) and host profiles. Missing profiles or unsupported types report an error. The core must not substitute unrelated terrain or relocate characters to hide missing content.

Custom starts, relocation, database repair, starter population, and account troubleshooting are host/server deployment work. Validate initial entry, same-region jumps, and region transitions with disposable fixtures.
