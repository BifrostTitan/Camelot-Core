# Authorized administration interface

The existing developer panel sends supported DOL commands through the active session. The server controls privileges/results; the panel cannot grant them.

A privileged session opens it with **Ctrl+Alt+A** after debug authorization; Escape closes it. It displays region/zone, server coordinates, target, and bounded history. A sent request is not proof of completion.

## Command boundaries

NPC commands use server model/template IDs and server position. Model and template IDs are distinct. Spawn/edit commands may persist database changes; use a disposable development server and its command help.

Teleport commands use absolute server coordinates and a region ID, not Unreal coordinates or a zone ID. The normal transition path resolves and renders the destination.

Weather commands affect the server region. Authorized local `/client` inspection, regeneration, flight, and weather controls affect the consumer's presentation. Regeneration does not teleport or edit server data.

`/cmdhelp` queries the server registry; `/adminhelp` requires a server implementing that extension. Core documentation must not assume an account was promoted, a server patched, or a database populated.
