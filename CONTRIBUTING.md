# Developer guidelines

## Mission

Camelot Core contains essential game mechanics, networking, rendering, and data interfaces used by the existing Camelot Enhanced Client. Contributions improve these shared systems while allowing independent host projects to use them without inheriting another project's content or configuration.

A possible downstream use is not, by itself, a reason to add another core subsystem.

## Permitted uses and project policy

Use is limited to:

1. Improving Camelot Core and upgrading the existing Camelot Enhanced Client through compatible mechanics, networking, rendering, and UI contributions.
2. Building distinct projects that do not create another high-quality game client. Examples include DAoC-inspired cutscenes or movies, bot navmesh frameworks, and item catalog inspectors or analyzers.

Creating another high-quality game client from this core, including a competing or rebranded version of the existing enhanced client, is outside the permitted scope. A fork may be used to develop contributions or a permitted distinct project. Forking solely to rename the project, republish it as one's own creation, or take credit for existing work is prohibited.

All uses outside the two permitted categories fall outside this project's policy.

Retain author/contributor attribution and provenance. Credit your changes accurately without claiming authorship of the original core or other contributors' work. Rebranding does not remove these requirements.

These are project usage and contribution rules. This document does not provide a software license or rights to third-party code, game assets, trademarks, or licensed content. No `LICENSE` file is currently included in this repository.

## Contribution scope

Changes must directly improve existing mechanics, protocol correctness, rendering, UI interfaces, catalogs, imports, performance, tests, or documentation. Keep changes focused; omit unrelated features and speculative roadmaps. Discuss new public systems or dependencies in a repository issue before implementation.

Host projects own maps, sequences, branding, replacement art, material tuning, server deployment, bot behavior, analysis dashboards, and application-specific UI. Navigation frameworks and inspectors should consume core interfaces rather than become mandatory core dependencies.

Do not add personal accounts/characters, credentials, database snapshots, workstation paths, local repair history, or demonstration-world presets. Existing project-specific helpers are not a precedent for expanding the core's mission.

## Architecture and compatibility

- Keep session/transport ownership in the game-instance network subsystem. UI and rendering consume state rather than open parallel gameplay connections.
- Preserve server authority for connected gameplay; do not invent confirmed combat results, rewards, privileges, or destinations.
- Keep runtime code buildable without editor modules; guard asset authoring/import code with `WITH_EDITOR`.
- Keep host assets/settings external. Prefer explicit inputs and configurable data over hardcoded maps, machine paths, and personal presets.
- Preserve public APIs, serialized fields, packet layouts, and asset identities where possible. Explain intentional incompatibilities and supply migrations/redirects where needed.
- Builds, startup, and tests must not regenerate, rewrite, or delete another host project's content. Asset-changing tools require explicit targets and deliberate invocation.
- Initialize required `ThirdParty` Git submodules at the recorded revisions. Update dependency pins deliberately and include relevant upstream validation; ordinary builds must not fetch a moving branch or modify upstream source.
- UI upgrades use existing session/mechanics interfaces and support host presentation without changing unrelated consumers.

## Pull requests and verification

Document types and methods at their declarations with concise Doxygen `///` comments so clangd can display their purpose on hover. Place documentation before Unreal reflection macros. Explain units, ownership, server authority, return values and non-obvious constraints; use `@param` and `@return` where they clarify the contract. Add implementation comments for the reason behind unusual logic rather than restating each statement. Keep comments accurate when behavior changes.

Describe the shared-core problem, resulting behavior, affected interfaces, and migration requirements. Include only relevant documentation and justified dependencies.

Build affected Editor and Game targets for runtime changes. Run applicable native/Python regressions and Unreal automation. Use disposable hosts/accounts for imported-content and live-server tests, never another developer's game project or live data. Report what passed and what remains unverified; compilation does not replace visual or cooked-runtime checks.

Use synthetic fixtures and neutral placeholders. Preserve attribution for derived work. Improvements must be reviewable independently of a particular developer's client installation.
