# WrathIdle game server

The custom x64 client and this server exchange WrathIdle commands and state through dedicated native world packets. The authenticated session owns the character and the server owns all game decisions, inventory transactions and execution.

Work on wrathidle/game in both repositories. The client repository owns Interface/WrathIdle and embeds its Lua sources in WowClient64.exe. Its docs/native-packets.md defines the matching wire contract, docs/world-first-design.md ranks UI improvements, and docs/maintainer-testing.md supplies the acceptance checklist and packet fixtures.

Play WrathIdle.cmd launches the local stack and custom client. Install-Gameplay.ps1 deploys the staged matching server, retires the old managed UI archive, records hashes, and restarts the local server. The native client suppresses old patch UI scripts.

Profiles load paused. All testing is controlled by the maintainer. Build and deployment checks establish artifact readiness.

## Playerbots integration

The core includes the `Playerbot` branch of
[mod-playerbots/azerothcore-wotlk](https://github.com/mod-playerbots/azerothcore-wotlk)
through revision `3f2969ddb0da42b48b4fca050c02b19163ba1e20`, preserving its Git
history and authors. [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
is pinned to `e88cc4099e69f35126b28942a412ca702d20453b` in
`tools/wrathidle/playerbots-dependency.json`. Its source retains the upstream GPL
notices and AUTHORS file. No Dungeon Clear code is included in this slice.

`tools/wrathidle/Setup-Playerbots.ps1` prepares that exact module revision and applies
`playerbots-managed.patch`. It refuses an unexpected checkout instead of resetting
local changes. The Windows build runs this preparation automatically. The module
checkout is an external dependency; its WrathIdle changes are carried in the patch.

The patch adds an opt-in externally controlled AI context. Ordinary Playerbots keep
their normal behavior. A WrathIdle context has no autonomous tick, chat commands,
packet-triggered actions, or bot cheats. Its default strategies are cleared, and
WrathIdle pulses the selected class strategy only after evaluating the current goal
and profile. A listener admits learned spell actions aimed at the character, owned pet,
or controller-selected enemy; autonomous movement, target changes, shopping, item
creation helpers, and cheat feeding actions are blocked. Cached entity/item targets are
cleared after each pulse. Pause, completion, disconnect, and logout detach the context.
Start refuses to replace an AI owned by another controller.

WrathIdle selects eligible live creatures and applies the eight ordered custom
ability rules before the optional class rotation. Loot, repair,
selling, and buying retain the core transaction paths and WrathIdle's protected
items, gold reserve, and visit budget. Corpse release/reclaim also remain core
transactions, with Playerbots handling the walk. Missing or incomplete paths pause
the routine; the adapter does not teleport to bypass them.

With the prepared local database running, run
`tools/wrathidle/Install-PlayerbotsConfig.ps1 -ProvisionDatabase` once for the local realm.
It uses the existing character-database connection for a separate `acore_playerbots`
database and installs the module configuration under `runtime/server/etc/configs/modules`.
The provisioning switch uses `runtime/mysql-admin.cnf` to create that database and
grant the existing realm account access to that database only. Omitting the switch
updates configuration without provisioning. Existing configuration is backed up
before updating the managed settings. Random
population, automatic alt login, account pools, random guild/arena teams, manual
self-bot mode, and bot cheats are disabled. On the next server start, the upstream
database updater creates/populates its database and applies its module SQL updates.
The installer does not start or stop services.

Server logs under `module.wrathidle` record controller attachment and decisions,
including why execution paused. The native packet contract is documented in the client repository;
saved profile schema 3 adds independent talent and class-preset preferences. Schema 1/2
profiles migrate, preserving queues, protection, equipment policy, and spending limits.
The old default enemy band 1-6 becomes automatic; other explicit bands are preserved.

## Navigation and target discovery

Quest, trainer, vendor, and destination discovery use map-indexed server spawn records
with character-specific phase and eligibility checks. Live entities are resolved and
validated again before any transaction. Repair/selling and food destinations are selected
by service flags, faction, vendor stock, and complete path availability. No region-specific
coordinate, service NPC entry, or quest-ID override is used in production planning.
Automatic service trips return to the actual interrupted location. A map change during
a service trip pauses rather than interpreting old coordinates on another map.

Local routes use a dedicated 8192-node search for complete navmesh corner paths, then validate
and execute a normal smooth walking path to a point at most 80 yards along that
plan. Corner paths themselves are never executed. Terrain loading follows the requested
start/destination corridor, capped at 25 grids and 2,000 yards per local route request;
it does not preload an authored region or an entire continent. Missing, incomplete,
truncated, invalid, or shortcut
paths are rejected. A rejected route/leg logs its map, start, destination, path type,
and point count, or the route query's stage/status/polygon counts. Three execution
failures still pause required travel. A wandering enemy whose approach becomes
unreachable is skipped temporarily so another candidate can be selected.

Longer same-map trips use a private planning mesh. A coarse tile graph comes from the
installed navigation files; the planner loads a corridor and its neighboring tiles,
at most one tile per controller update, 128 tiles and 128 MiB per request. It uses a
65,535-node query and requires a complete route before producing an 80-yard walking
leg. The core validates and executes that leg normally. Tile adjacency alone never
permits movement. Missing or disconnected data fail within these bounds. This avoids
loading distant live grids or generating a continent-wide Playerbots graph.

Walking-leg validation uses the core's normal smooth-path settings. The optional
climb check compares against a distant steer corner and rejected ordinary rising
terrain in the first native test; WrathIdle no longer enables that check. The
dedicated route search avoids exhausting the core's shared 1024-node query on the
longer local trips. Both the entire route and its executable walking
leg must still be complete. Intermediate leg heights are projected onto polygons
in that route so interpolated corner heights do not leave them above uneven ground.
Projection uses the actual polygon detail surface, permitting at most a 0.01-yard
lateral correction for floating-point error at polygon boundaries. Larger departures
from the corridor are rejected. Quest enemy selection also respects the temporary
unreachable-target list, so a rejected creature cannot be selected again on every
quest update before its retry interval expires.

Target discovery queries live creatures instead of a fixed creature-entry list.
Candidates must remain unclaimed, alive, attackable, visible, normal-rank, unowned,
non-summoned, and within the configured level limits.
Critters, service NPCs, and creatures with no experience multiplier are excluded.
The routine may walk toward a reachable candidate within three times Search Distance;
engagement still requires Search Distance and line of sight. Full scans run at most
every two seconds, with eight reachability attempts per scan. Failed candidates are
skipped for up to fifteen seconds so an unreachable nearby enemy cannot monopolize
selection. When no live candidate is available, leveling seeks a suitable phase-compatible
normal creature spawn from world data on the current map. Actual engagement still
requires a valid live target. Unreachable/empty locations are skipped temporarily.
Item-stock goals still use this hunting policy; item-specific drop-source planning
is not implemented.

Low-health retreat selects a reachable friendly guard within 120 yards, rather than
requesting a path to a fixed refuge. If no such guard exists, the routine pauses
with that reason. This does not guarantee survival or remove normal enemy attacks.

The Travel panel lists nearby friendly quest/service NPCs from server spawn records.
Older fixed-destination travel goals remain paused for replacement; their saved numeric
values are never reinterpreted as world spawn IDs. Transport and inter-map route planning
remain unfinished, so same-map regional walking does not establish world-wide travel.

On 30 October 2026 the navigation changes compiled successfully into the Release
worldserver used by the local launcher. The rebuilt C++ policy suite passed, including
the navigation cases, and addon protocol checks passed. The addon was installed and
hash-verified in the configured original client, and the launcher's file-readiness
check passed. The previous worldserver was backed up; deployment hashes and rollback
location are recorded in the workspace `.tools/navigation-deployment.json`.
No applications were started or stopped. Native gameplay verification remains pending.
The existing offline waypoint checker validates the historical waypoint pairs,
not the new dynamic service routes.

The optional installed-map regression suite replays the recorded post-kill, corpse,
Goldshire, and food-vendor routes in both directions through the production route
planner. It also reproduces the exhausted 1024-node search and checks that partial
and truncated routes remain rejected. Additional spawn-derived cases cover all ten
race starts and longer trips on both main continents through the regional planner.
The reported Den-to-boar route is replayed through all legs in both directions, with
additional boundary checks that accept tiny rounding offsets and reject real gaps.
`tools/wrathidle/tests/class_data.py` checks all ten configured talent templates against
installed Talent/TalentTab DBC data, allocating 71 points with the core's tier and
dependency rules without starting the game. Enable navigation replay when configuring the standalone
policy suite with `WRATHIDLE_DETOUR_LIBRARY` pointing to the built Detour library and
`WRATHIDLE_MMAP_DIRECTORY` pointing to the installed v20 mmaps directory. No map data
is copied into the repository. These offline checks do not establish live vendor
transactions or retreat acceptance.
