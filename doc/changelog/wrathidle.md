# WrathIdle server changelog

## Unreleased

### 2026-10-31

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
  installed hashes, records source revisions and restarts the deployed local stack.
- WoW 3.3.5a/build 12340, upstream core/database compatibility and saved profile schema 3 remain
  the compatibility identifiers. Runtime test execution remains maintainer-controlled.
- Validation includes source review and whitespace checks. Build and deployment results and
  matching client/server revisions are recorded in the deployment handoff.
