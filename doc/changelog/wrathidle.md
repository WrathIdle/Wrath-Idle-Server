# WrathIdle server changelog

## Unreleased

### 2026-10-31

#### Event-aware service discovery

- Fixed friendly NPC discovery selecting database-only spawns from inactive game events.
  Trainer, service, destination and quest-giver selection now consult the core grid-spawn index
  on every lookup, using the current map difficulty and existing phase/faction checks.
- This prevents repeated training attempts at an absent Arena Tournament trainer. Ordinary trainers
  remain eligible. Event spawns become eligible when the core activates them, including after map
  indexes have been cached. No trainer IDs, event IDs or coordinates are overridden in production.
- Wire version 1 and existing client builds remain compatible. Source and installed data were inspected.
  The guarded Release build passed with zero warnings/errors, installed hashes were verified, and server
  startup was confirmed. Existing local PCH/build-diagnostics edits are recorded in the deployment manifest.
  Trainer gameplay acceptance remains maintainer-controlled.

#### Empty native packet strings

- Fixed the native update writer calling ByteBuffer::append with zero bytes for an empty string.
  A normal empty class-decision field caused a server assertion after character login.
  The writer retains the uint16 zero length and omits the empty data append.
- Wire version 1 and saved profiles remain compatible with the deployed client. The client reader
  already accepts zero-length strings. The existing update_classstate packet fixture contains this case.
- Validation. The monitored Release build succeeded with zero warnings and errors. Installed hashes
  match the deployment record, and worldserver startup was confirmed. The rebuild includes existing
  local module-PCH and build-diagnostics changes. Login, UI refresh and reconnect remain maintainer-controlled.

- Added mandatory change documentation and versioning rules to `AGENTS.md` so future work records
  its scope, rationale, compatibility requirements, and validation status alongside the implementation.
  Keep affected documentation and release metadata consistent, preserve upstream version identifiers,
  and record matching revisions and installed hashes for client/server deployments.
- Documentation only; no runtime version bump or migration is required. Validation: inspected the
  documentation diff and checked whitespace. Builds, deployment, and live testing were not performed.

#### Native packet migration and game branch

- Checkpointed the existing WrathIdle game work on wrathidle/game, preserving Playerbots history,
  and added shared coordination, deployment authority, testing ownership and role-based wording.
- Registered CMSG_WRATHIDLE_COMMAND at 0x521 and SMSG_WRATHIDLE_UPDATE at 0x522. Wire version 1
  replaces the WRI2 whisper transport and uses authenticated sessions, monotonic request IDs,
  expected revisions and typed, bounded payloads. Existing gameplay validation remains in charge.
- Moved game-owned FrameXML sources to the client repository and retired the addon and MPQ UI
  installers. The normal launcher opens WowClient64.exe. Upgrade the client and server together.
- Added monitored, sequential builds with a workspace lock, below-normal priority, at most half
  the logical processors, five default compiler workers and a 4 GiB memory stop threshold.
  Deployment stages and backs up both executables, retires the managed UI archive, verifies
  installed hashes, records source revisions, local modifications, generated build identities and the packet contract
  hash, and restarts the deployed local stack. Deployment records identify installed, ready
  and rolled-back phases. Rollback waits for deployed processes to exit before restoring files.
- WoW 3.3.5a/build 12340, upstream core/database compatibility and saved profile schema 3 remain
  the compatibility identifiers. Runtime test execution remains maintainer-controlled.
- Validation includes source review and whitespace checks. Build and deployment results and
  matching client/server revisions are recorded in the deployment handoff.

- Large-module builds now use bounded per-file compiler processes so compiler memory is released
  between files. Process-tree shutdown uses the Windows taskkill tool scoped to the launched build PID.
- Aligned staged executable output directories with MSBuild target metadata for consistent incremental linking.
