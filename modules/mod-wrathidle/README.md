# WrathIdle native command center

WrathIdle uses the original Windows WoW **3.3.5a, build 12340** client and the
native FrameXML interface in `native/Interface/FrameXML/WrathIdle`, packaged in an MPQ. Player-facing features use native WoW
frames, buttons, edit boxes, dropdowns, checkboxes, and scrolling text. There is
no ImGui frontend or replacement renderer in the active game path.

The server module owns goals, rule evaluation, loot policy, recovery thresholds,
and town services. A managed Playerbots context executes walking, melee, selected
spell casts, and food use. Client inputs cannot move or cast directly on this dedicated
server. The native interface sends configuration and management commands over self-directed
`WRI2` control whispers; no addon installation is required. Ownership comes from the authenticated session, not a
character ID supplied by the addon. Profiles and goals use character settings;
inventory and money stay in the ordinary core transaction paths.

## Local playtest

The prepared workspace launcher `Play WrathIdle.cmd` starts the database on
127.0.0.1:3308, the login server on 3724, the world server on 8086, then opens
`the asset installation's Wow.exe`. The original installation and game assets stay
outside this repository. Closing the client leaves the local services running.

Start auth before world: auth startup marks realms offline, and world startup
advertises readiness. Reversing the order can temporarily hide a running realm.

Run `tools/wrathidle/Install-NativeUI.ps1` to build and install the interface.
`-StageOnly` prepares verified output without changing the client. The packager uses
StormLib 9.40 (`stormlib_dll.zip` from the upstream release, x64 DLL under
`.tools/stormlib-9.40/x64`), resolves the effective installed FrameXML TOC, and writes
`Data/enUS/patch-enUS-W.MPQ` for this installation. Extracted original resources stay
outside the repository. The existing custom locale patch remains installed.

The narrowly scoped build-12340 FrameXML compatibility patch checks complete known
instruction windows before changing the executable; unknown windows are rejected.
The mechanism follows [allow-custom-gluexml-mithril](https://github.com/suprsokr/allow-custom-gluexml-mithril).
Executable and previous MPQ backups, retired WrathIdle addon files, installation
hashes, and resource provenance are recorded under the workspace `.tools/native-ui`.
No other addon is moved. Restore the backed-up executable, previous MPQ (or remove
the managed MPQ on the first install), and retired addon to roll back the UI.
`Install-Addon.ps1` now forwards to the native installer for compatibility.

The native UI and WRI2 server must be deployed together. Restart the client for MPQ
changes; reloading Lua alone cannot mount a newly installed archive. Profiles load
paused. Valid v1 character settings migrate once to `wrathidle.v2`, retaining item
protection, queue contents, elapsed limits, and combat settings. Goals gain stable
identifiers; malformed or duplicate identifiers are rejected without replacing data.

Log in with a legal Wrath race/class combination and enter the world. The manager opens
automatically. `/wi` and the WrathIdle button reopen it after closing.

- **Goals:** queue level, stock, selected-quest, named-travel, and class-training goals;
  make a goal next or cancel it using its persistent identity.
- **Quests:** browse paginated eligible quests, queue a selected quest, or avoid it
  during automatic selection. Level goals choose supported nearby quests before
  falling back to the existing hunting policy. Quest goals accept, fight/loot, and
  turn in using core handlers and authoritative quest state. Current handlers cover
  ordinary creature kills, creature quest-item drops indexed by `creature_questitem`,
  simple deliveries, quest-giver objects, and unscripted object-use objectives.
  Quest-item containers indexed by `gameobject_questitem` use normal opening spells and
  loot transactions, including the core's key/skill requirements. Exploration, quest-item
  use, and scripted mechanics remain unavailable. No completion credit
  or missing items are generated.
- **Travel:** browse nearby quest givers, trainers, and services supplied by world data;
  queue a destination by its persistent spawn identity. Complete walking paths are required.
- **Character:** inspect gear/talents/bags, toggle the class preset and spending earned
  talent points, automatic equipment, and class/weapon/riding training; allow or defer
  binding upgrades, lock equipment/bag slots, and queue an eligible trainer trip.
- **Upkeep:** configure recovery, reserves, visit budgets, food, targets, selling,
  and protected items. Eligible upgrades are checked before selling. Larger bags
  use the core's bag exchange transaction, including its occupied-content checks.
- **Decisions:** review pending equipment upgrades, explicitly equip/bind one,
  and read the last 200 session decisions. Commands revalidate item ownership,
  legality, combat state, and locked slots before making an inventory change.
- The persistent bottom activity strip replaces action, stance, and pet bars.
  It shows activity, reason, health/resources, start/resume, pause, and town services.

The controller accepts **all legal solo Wrath race/class combinations through level 80**.
The shared class presets are Arms Warrior, Retribution Paladin, Beast Mastery Hunter,
Combat Rogue, Shadow Priest, Blood Death Knight, Enhancement Shaman, Frost Mage,
Affliction Warlock, and Feral Druid. Learned abilities and ranks govern execution;
Druids use learned caster/bear abilities until their cat form becomes available.
Existing incompatible talents produce a decision, and are never reset automatically.
Talent spending uses earned points and the configured class template through normal
core prerequisites. Recovery includes mana; supplies include usable drinks, ammunition,
and purchasable reagents for learned spells. Mage conjuring and hunter feeding use
normal learned spells and actual carried items. Warlocks earn required shards through
normal learned Drain Soul casts; no pet happiness, reagents, or class unlocks are granted.

The **full gameplay plan remains in progress**. Cross-map transport routing, mount and
bag purchasing, loot-source planning for ordinary stock goals, exploration, quest-item
use, and essential scripted class/Death Knight chains remain unfinished. Accepting a
level-80 goal is not evidence of an uninterrupted level-1-to-80 run. Location planning
has no authored region rectangle, fixed vendor IDs, or fixed town/travel coordinates.
These changes require user gameplay and visual testing; offline checks establish
build/protocol/package behavior only.

### Quest travel decisions

Before leaving a quest hub, automatic leveling collects supported eligible quests from
quest givers within 80 yards. After accepting one, it checks for the next nearby pickup
before committing to field work. It respects prerequisites, journal capacity, and avoidance.
Before turning in a completed quest, it prefers unfinished accepted objectives within
120 yards, extended to 600 yards for quests in the completed quest's positive area
identifier from server quest data. These are general work budgets, not region-specific
coordinates or quest IDs. A bounded cluster prevents unrelated long detours.
Selected quest goals apply the same rule to the following consecutive quest goals in
the queue; an intervening travel, training, level, or item goal ends that group.
Explicit quest goals collect only other consecutive queued quests at the hub.
Avoided, unsupported, unsafe, and distant objectives do not delay the turn-in.
This batches nearby field work without changing the saved goal order or abandoning quests.

Ordinary kill and object-counter quests are supported even when the core adds the
CAST credit flag: its quest loader sets KILL, CAST, and SPEAKTO together for these
counters. The CAST bit alone does not mean a spell-use handler is required. Explicit
exploration/event requirements still require a separate handler.

The work cluster also considers the character's current area and server-provided
parent/subarea relationships. Accepted work takes priority over seeking new quests
outside the hub. A nearby accepted normal-creature objective up to three levels
above the character keeps automatic leveling focused on gaining experience locally
before outbound deliveries. Nearby rewards in the current area remain eligible to
grant experience and unlock the next local quest. Far-away, avoided, unsupported,
elite, and much higher-level objectives do not impose this hold. Mixed-level spawn
ranges can supply local hunting locations; actual pulls still use each live creature's
level. Seasonal pickups tied to inactive events are excluded, and scripted NPC
offers need live confirmation rather than being inferred from static quest relations.

Destination selection considers all unfinished kill and creature-drop objectives together.
An unfinished item requirement no longer hides a kill requirement, and all missing quest
items are considered. Accepted journal quests and object quest givers are included in the
catalog. Object-use support is restricted to ordinary unlocked goober objects without
scripts, gossip, spells, events, or linked traps, using normal core interaction handlers.
Progress is read back from quest status, objective counters, and inventory.

Three rejected interactions or ten minutes of active quest work without progress stop
an explicit quest goal. Automatic leveling temporarily skips that quest and rechecks
skipped quests after ten running minutes. Combat, recovery, service trips, and paused time
do not consume the quest-work timer. Resume starts a fresh attempt. No quest is abandoned.

Profiles load paused after reconnect or server restart. No catch-up rewards are
created. The default keeps protected items and sells only poor-quality loot.
New profiles select usable healing food from carried items and nearby vendor stock;
Food ID 0 selects this automatic policy. Existing explicit item preferences remain intact.
Food purchases retain normal prices, stock, reserve, and visit-budget checks.
New profiles use automatic character-relative enemy levels, an 80 target level, a
100-copper reserve, and a 100,000,000-copper visit ceiling so high-level riding fees can
fit. Money on hand and the reserve still constrain every purchase. Existing saved
spending limits are retained, including the old 500-copper limit; review that limit
before expecting higher-level training. Budget/reserve fields support the core money cap.
The controller uses corpse retrieval; spirit resurrection is unavailable.

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
including why execution paused. The native interface wire protocol remains WRI2;
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

## Build and checks

`tools/wrathidle/Build-Windows.ps1` builds **only the server and policy tests**.
All WrathIdle build entrypoints default to one build/compiler worker. The imported
`LowResourceBuild.targets` disables the compiler's separate `/MP` parallelism.
`Build-Incremental.ps1` refuses simultaneous builds, checks available memory, and
runs below normal priority. `Build-Module.ps1` compiles only the four WrathIdle
translation units, rebuilds their containing library, and links a staged executable
without changing the output directory for dependency projects. Use it only when
core/dependency sources and configuration are unchanged and their existing libraries
are up to date. It is not a substitute for building changed core/dependency code.
It uses the prepared CMake 4.4.4, Visual Studio 2022, MySQL 8.4.13, and vcpkg
checkout in the workspace `.tools` directory. The dependency revision used for
the prepared environment is `817892b0ecbc6af994dd6df999c73faf795adbf2`.
The script copies `libmysql.dll` and its matching OpenSSL runtime dependencies.

`tools/wrathidle/Get-ServerData.ps1` obtains the official v20 data package used
by this fork. Point `DataDir` at the extracted directory. Enable
`EnablePlayerSettings = 1` and `WrathIdle.Enable = 1` on the dedicated server.

Focused checks:

```text
ctest --test-dir build-policy -C Release --output-on-failure
lua51 tools/wrathidle/tests/native_protocol.lua
python apps/codestyle/codestyle-cpp.py
```

The Lua checks cover negotiation, sender filtering, complete snapshots, revisioned
commands, timeout recovery, disconnect handling, and bounded history. They do not
prove in-game rendering or gameplay.

## Acceptance

Before the Playerbots integration, the 25 October 2026 native-client check completed a level-1-to-2 goal through
eight combat/loot cycles: 400 XP, 13 loot stacks, 4 copper, and no deaths.
All five addon panels were opened, goal edits were acknowledged, combat
Pause/Resume worked, and the routine paused automatically at goal completion.
A follow-up queued goal survived `/reload` while paused.

This short run does not complete release acceptance. Targeted ability/recovery
checks, Goldshire routing and its reported retreat-path failure, corpse recovery,
concurrent players, reconnect/restart safety, and the original 60-minute
unattended run remain pending. Adding a goal after queue completion also leaves
the previous completion explanation visible until another state transition.
Later roadmap features are labelled unavailable.

The earlier run does not validate the Playerbots adapter. Its live acceptance is:
complete a Northshire level/item goal, pause and resume during travel and combat,
verify configured abilities and food recovery, visit Goldshire within the reserve
and budget, then reconnect and confirm the profile returns paused. Confirm the
log contains `Playerbots managed execution attached` and no random bots were created.
