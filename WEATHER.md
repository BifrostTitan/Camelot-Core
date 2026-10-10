# Weather presentation

`UDaocWeatherSubsystem` consumes session weather and zone policy. It owns no socket or authoritative weather simulation. Presentation is disabled during login/loading and exposed through state, overrides, and `OnWeatherChanged`.

## Policies and assets

Profiles support always sunny, persistent precipitation, server weather, and server-plus-cycle presentation. Storms take priority over cosmetic cycles, which use clock intervals, zone ID, and seed.

Configure host policies explicitly instead of inheriting demonstration-zone presets. Hosts supply compatible Niagara systems, materials, collections, and saved weather catalogs. Toolbox preparation requires its expected source dependencies and deliberate editor invocation; it is not universal bundled setup.

Transitions blend precipitation, fog, and lighting controls. Camera-local particles and shelter traces limit cost. Overrides affect the consumer only. Surface wetness/snow needs compatible materials and is not automatic.

## Protocol and checks

Packet `0x92` supplies front X, width, fog diffusion, speed, intensity, and a reserved field. The moving front applies between `frontX - width` and `frontX`; profiles choose rain/snow because the packet lacks that discriminator.

State resets on disconnect, selection, and region changes; same-region travel retains the front. Check transitions, shelter, overrides, cooking, and particle/material rendering. Decoding does not prove visible precipitation.
