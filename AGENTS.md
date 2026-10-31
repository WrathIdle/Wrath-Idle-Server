# AGENTS.md

AzerothCore is a C++ MMORPG server emulator for World of Warcraft 3.3.5a (WotLK), built with CMake, backed by MySQL.

## Agent rules

- **Do not configure or build unless explicitly asked.** Builds are slow and rarely needed for code changes.
- **Never edit SQL files outside `data/sql/updates/pending_db_*/` unless explicitly requested.** `data/sql/base/`, `data/sql/archive/`, and `data/sql/updates/db_*/` are immutable.
- Formatting follows `.editorconfig`: UTF-8, LF, max 120 cols, trailing newline, no trailing whitespace; 4-space indent for C++ (tabs forbidden), 2-space for JSON/YAML/sh/ts/js.
- **Do not write or add live-stack e2e tests unless the user explicitly asks.** Do not create `e2e/local/` scratch tests to validate a fix. Existing `e2e/` tests that break after a change may be updated. Do not mention missing coverage unless the user asked about it.
- Planning docs go in `.agents/plans/<task-slug>/` (gitignored), named `<task-slug>.<TYPE>.md` (`PLAN`, `REQUIREMENTS`, `ANALYSIS`, …).
- **Credit upstream authors.** Code, a mechanism, or data mirrored from another core (TrinityCore, cMaNGOS, …) is committed with `--author` naming the original commit's author (extra sources as `Co-authored-by`), even when rewritten against AC or confirmed by own sniffs; find them in the upstream file's commit history.

## Mandatory reading per task

Read the matching doc(s) BEFORE starting the task:

- Compiling, configuring, or running tests → `.agents/docs/build.md`
- Writing or modifying C++ → `.agents/docs/cpp-guidelines.md`
  - Script work (under `src/server/scripts/`) → also `.agents/docs/cpp-scripts.md`
- Creating or modifying SQL → `.agents/docs/sql-guidelines.md`
  - SmartAI work (`smart_scripts` data) → also `.agents/docs/cpp-scripts.md`
- Reviewing a changeset or PR → `.agents/docs/code-review.md`
- Self-reviewing, or opening or updating a PR → also `.agents/docs/self-review-rules.md`
- Touching a subsystem that has a doc in `.agents/docs/systems/` → read that doc too
- Changing existing live-stack e2e (`e2e/`), and only when the user asked or an existing test broke → `e2e/README.md` and `.agents/docs/e2e-policy.md`
- Capturing a lesson or adding/updating agent docs → `.agents/docs/README.md`

## Repository layout

- `src/common/` — networking (Asio), crypto, config, logging, shared utilities.
- `src/server/game/` — core gameplay; compiled into worldserver.
- `src/server/scripts/` — content scripts grouped by region (`EasternKingdoms/`, `Northrend/`, …), class (`Spells/spell_mage.cpp`, …), and domain (`Commands/`, `Pet/`, `OutdoorPvP/`, `World/`).
- `src/server/database/` — DB abstraction and schema updater.
- `src/server/shared/` — code shared by auth and world servers.
- `src/server/apps/{authserver,worldserver}/` — entry points (ports 3724 and 8085).
- `src/test/` — unit tests + mocks.
- `e2e/` — live-stack Go e2e (AzerothGhost harness); see `e2e/README.md`.
- `data/sql/` — `base/` (historical schema), `updates/db_*/` (merged), `updates/pending_db_*/` (in-flight), `custom/` (gitignored).
- `modules/` — external modules (see below).
- `apps/` — helper scripts; `apps/codestyle/` holds the lint scripts.
- `conf/dist/` — distributed config templates; `conf/*.conf` is gitignored.
- `deps/` — vendored third-party dependencies.

## Modules

External modules live in `modules/`, each a subdir with its own `CMakeLists.txt`. Disable with `-DDISABLED_AC_MODULES="mod1;mod2"`. See `modules/how_to_make_a_module.md`.

## Persisting lessons

When a user correction reveals a lesson that generalizes, offer to persist it into these docs (placement per `.agents/docs/README.md`): use the `/self-improve` skill if installed, otherwise suggest the user to install it and read this page: https://www.azerothcore.org/wiki/agentic-engineering

## WrathIdle development rules

- Work on `wrathidle/game` in each repository you change. When a change affects both client and server, update both in the same task and identify the matching commits in the handoff.
- Codex may stop, deploy, start, and restart the client and server as needed for deployment without requesting confirmation. Deploy matching versions together when a shared interface changes.
- The maintainer controls all testing. Prepare test cases and instructions. Run a test only when the maintainer explicitly delegates it. This includes automated tests, smoke checks, login, gameplay, UI, and reconnect testing. Building and installing artifacts, checking deployed hashes, and confirming process startup are deployment operations. Report deployment results and test results separately.
- Use role-based wording in project-authored instructions, documentation, and test handoffs. Refer to the user as the maintainer.

## Build resource limits

A previous run launched client and server builds concurrently and bypassed the existing
low-resource server build wrapper. Server projects also enabled multiprocess compilation,
so the outer parallel-job setting did not bound the number of compiler workers.
The maintainer reported a complete computer lockup requiring a restart during those builds.

- Never build the client and server concurrently. Run one build at a time across this workspace.
- Use one MSBuild worker and one compiler worker. Set CMake parallelism to 1, MSBuild /m:1,
  BuildInParallel=false, CL_MPCount=1, and UseMultiToolTask=false. Explicitly disable
  MultiProcessorCompilation through the existing tools/wrathidle/LowResourceBuild.targets
  in the server repository. Apply these limits to client builds as well.
- Never invoke a direct build command that bypasses those limits. The server's
  tools/wrathidle/Build-Incremental.ps1 is the starting point for bounded server builds.
  Verify that the resource-limit targets file is imported before starting compilation.
- Before each build, check for active compiler, linker and build processes. Wait for any
  active build to finish. Never start a second build to work around an active-build refusal.
- Require at least 8 GiB of available physical memory before starting. Run at BelowNormal
  priority. Monitor available memory during compilation and linking. If it falls below
  4 GiB, stop only the build process tree started for this task and report the resource limit.
  Use a monitored launcher that enforces this stop condition before starting a build.
- Prefer the smallest affected incremental target. Inspect rebuild scope after shared-header
  changes. If safe resource limits cannot be enforced, report the blocker and leave the build
  pending for the maintainer.
- Following a reported lockup or forced restart, do not automatically resume interrupted
  builds. Finish the requested investigation and safeguards first.
