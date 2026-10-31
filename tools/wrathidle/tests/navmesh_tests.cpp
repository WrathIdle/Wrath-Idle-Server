#include "NavmeshRoute.h"
#include "RegionalRoute.h"
#include "DetourAlloc.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace
{
void Require(bool value, char const* message)
{
    if (!value)
        throw std::runtime_error(message);
}
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Specify the installed v20 mmaps directory");
        std::filesystem::path directory(argv[1]);
        dtNavMeshParams parameters{};
        std::ifstream map(directory / "000.mmap", std::ios::binary);
        Require(bool(map.read(reinterpret_cast<char*>(&parameters), sizeof(parameters))), "Cannot read map 0");
        std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh(dtAllocNavMesh(), dtFreeNavMesh);
        Require(mesh && dtStatusSucceed(mesh->init(&parameters)), "Cannot initialize map 0");
        unsigned loaded = 0;
        for (auto const& file : std::filesystem::directory_iterator(directory))
        {
            auto name = file.path().filename().string();
            if (!name.starts_with("000") || file.path().extension() != ".mmtile")
                continue;
            std::ifstream tile(file.path(), std::ios::binary);
            std::array<uint32_t, 14> header{};
            Require(bool(tile.read(reinterpret_cast<char*>(header.data()), sizeof(header))), "Cannot read tile header");
            Require(header[0] == 0x4d4d4150 && header[2] == 20, "Navigation replay requires v20 tiles");
            Require(header[3] >= sizeof(dtMeshHeader) && header[3] <= 64 * 1024 * 1024, "Invalid tile size");
            dtMeshHeader geometry{};
            Require(bool(tile.read(reinterpret_cast<char*>(&geometry), sizeof(geometry))), "Cannot read tile geometry");
            if (geometry.bmax[0] < -350.0f || geometry.bmin[0] > 180.0f ||
                geometry.bmax[2] < -9550.0f || geometry.bmin[2] > -8650.0f)
                continue;
            tile.seekg(sizeof(header));
            std::unique_ptr<unsigned char, decltype(&dtFree)> data(
                static_cast<unsigned char*>(dtAlloc(header[3], DT_ALLOC_PERM)), dtFree);
            Require(bool(data), "Cannot allocate tile");
            Require(bool(tile.read(reinterpret_cast<char*>(data.get()), header[3])), "Cannot read tile data");
            Require(dtStatusSucceed(mesh->addTile(data.get(), int(header[3]), DT_TILE_FREE_DATA, 0, nullptr)),
                "Cannot load tile");
            data.release();
            ++loaded;
        }
        Require(loaded > 0, "No pilot navigation tiles found");

        using namespace WrathIdle;
        struct Replay
        {
            char const* name;
            RoutePoint start, goal;
        };
        std::array<Replay, 4> const cases{{
            {"post-kill approach", {-8963.151f, -80.13532f, 88.68036f}, {-8957.506f, -49.09657f, 91.98698f}},
            {"ghost recovery", {-8992.914f, -146.53072f, 82.244064f}, {-9183.76f, -67.6272f, 75.5871f}},
            {"Goldshire service", {-8907.632f, -43.38396f, 88.74723f}, {-9462.29f, 87.82f, 58.337784f}},
            {"food vendor", {-9462.29f, 87.82f, 58.337784f}, {-9462.66f, 16.19f, 57.05f}}
        }};
        dtQueryFilter filter;
        filter.setIncludeFlags(0x01 | 0x02 | 0x08 | 0x10);
        unsigned checked = 0;
        for (auto const& replay : cases)
            for (bool reverse : {false, true})
            {
                auto const& start = reverse ? replay.goal : replay.start;
                auto const& goal = reverse ? replay.start : replay.goal;
                RoutePoint next{};
                RouteDiagnostic diagnostic;
                if (!PlanNavmeshLeg(mesh.get(), start, goal, filter, next, diagnostic))
                {
                    std::cerr << replay.name << " reverse=" << reverse << " stage=" << diagnostic.stage
                        << " status=" << diagnostic.status << '\n';
                    Require(false, "Recorded route must produce a complete bounded walking leg");
                }
                float dx = next.x - start.x, dy = next.y - start.y;
                Require(dx * dx + dy * dy <= 80.01f * 80.01f, "Walking leg exceeds its distance limit");
                Require(FinitePosition(next.x, next.y, next.z), "Invalid walking destination");
                std::cout << replay.name << " reverse=" << reverse << " polygons=" << diagnostic.polygons
                    << " step=(" << next.x << ',' << next.y << ',' << next.z << ")\n";
                ++checked;
            }
        RoutePoint next{};
        RouteDiagnostic diagnostic;
        Require(!PlanNavmeshLeg(mesh.get(), cases[2].start, cases[2].goal, filter, next, diagnostic, 1024),
            "Replay must reproduce the shared-query budget failure");
        Require(dtStatusDetail(diagnostic.status, DT_OUT_OF_NODES), "Expected exhausted route-search nodes");
        Require(!CompleteQuery(DT_SUCCESS | DT_PARTIAL_RESULT), "Partial routes must remain rejected");
        Require(!CompleteQuery(DT_SUCCESS | DT_BUFFER_TOO_SMALL), "Truncated routes must remain rejected");
        // Probe the boundary of a real loaded polygon, rather than assuming a particular map geometry.
        std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query(dtAllocNavMeshQuery(), dtFreeNavMeshQuery);
        Require(query && CompleteQuery(query->init(mesh.get(), 8192)), "Cannot initialize boundary replay");
        float from[]{cases[0].start.y, cases[0].start.z, cases[0].start.x};
        float extents[]{3, 5, 3}, nearest[3];
        dtPolyRef polyRef = 0;
        Require(CompleteQuery(query->findNearestPoly(from, extents, &filter, &polyRef, nearest)) && polyRef,
            "Cannot locate replay polygon");
        dtMeshTile const* replayTile = nullptr;
        dtPoly const* replayPoly = nullptr;
        Require(CompleteQuery(mesh->getTileAndPolyByRef(polyRef, &replayTile, &replayPoly)), "Invalid replay polygon");
        float center[3]{};
        for (int i = 0; i < replayPoly->vertCount; ++i)
            for (int axis = 0; axis < 3; ++axis)
                center[axis] += replayTile->verts[replayPoly->verts[i] * 3 + axis] / replayPoly->vertCount;
        auto v0 = &replayTile->verts[replayPoly->verts[0] * 3];
        auto v1 = &replayTile->verts[replayPoly->verts[1] * 3];
        float edge[3]{(v0[0] + v1[0]) / 2, (v0[1] + v1[1]) / 2, (v0[2] + v1[2]) / 2};
        float ox = edge[0] - center[0], oy = edge[2] - center[2];
        float length = std::sqrt(ox * ox + oy * oy);
        Require(length > 0, "Replay boundary requires a nondegenerate polygon");
        auto outside = [&](float offset)
        {
            return RoutePoint{edge[2] + oy * offset / length, edge[0] + ox * offset / length, edge[1]};
        };
        float gap = 0;
        Require(ProjectCorridorSurface(*query, &polyRef, 1, outside(0.001f), next, gap) && gap <= 0.01f,
            "Sub-centimeter edge roundoff must project onto the actual route surface");
        Require(!ProjectCorridorSurface(*query, &polyRef, 1, outside(0.25f), next, gap),
            "Surface correction must reject genuine departures from the corridor");
        struct RegionalReplay
        {
            char const* name;
            uint32_t map;
            RoutePoint start, goal;
        };
        // Read-only spawn snapshots from playercreateinfo and quest-giver world records.
        // Production routes contain no corresponding race, area, NPC, or coordinate table.
        std::array<RegionalReplay, 13> const regionalCases{{
            {"Human", 0, {-8949.95f, -132.493f, 83.5312f}, {-8947.64f, -132.319f, 83.7199f}},
            {"Orc and Troll", 1, {-618.518f, -4251.67f, 38.718f}, {-610.073f, -4253.52f, 39.0393f}},
            {"Dwarf and Gnome", 0, {-6240.32f, 331.033f, 382.758f}, {-6236.74f, 331.113f, 382.911f}},
            {"Night Elf", 1, {10311.3f, 832.463f, 1326.41f}, {10314.0f, 829.83f, 1326.48f}},
            {"Undead", 0, {1676.71f, 1678.31f, 121.67f}, {1671.89f, 1678.29f, 120.802f}},
            {"Tauren", 1, {-2917.58f, -257.98f, 52.9968f}, {-2912.7f, -257.54f, 53.0241f}},
            {"Blood Elf", 530, {10349.6f, -6357.29f, 33.4026f}, {10352.0f, -6359.93f, 34.1146f}},
            {"Draenei", 530, {-3961.64f, -13931.2f, 100.615f}, {-3962.0f, -13926.3f, 101.238f}},
            {"Den exit reported failure", 1, {-601.1341f, -4194.8887f, 41.099037f},
                {-484.0743f, -4215.6387f, 50.11948f}},
            {"Collection next nearby object", 1, {-551.918f, -4292.43f, 38.7974f},
                {-556.425f, -4288.72f, 37.4423f}},
            {"Collection next field object", 1, {-556.425f, -4288.72f, 37.4423f},
                {-489.093f, -4301.17f, 42.8668f}},
            {"Eastern Kingdoms regional", 0, {-8907.632f, -43.38396f, 88.74723f},
                {-11407.1f, 1966.15f, 10.5123f}},
            {"Kalimdor regional", 1, {-618.518f, -4251.67f, 38.718f}, {1919.9f, -4123.8f, 44.3715f}}
        }};
        auto tile = [](RoutePoint point)
        {
            return RegionalRoute::Tile{int(32 - point.x / 533.33333333f), int(32 - point.y / 533.33333333f)};
        };
        for (auto const& replay : regionalCases)
        {
            RegionalRoute route;
            Require(route.Begin(directory, replay.map, tile(replay.start), tile(replay.goal)),
                "Cannot initialize a spawn-derived regional route");
            auto progress = RegionalRoute::Progress::Loading;
            unsigned updates = 0;
            while (progress == RegionalRoute::Progress::Loading && ++updates <= 129)
                progress = route.Update(replay.start, replay.goal, filter, next, diagnostic);
            std::cout << replay.name << " tiles=" << updates - 1 << " stage=" << diagnostic.stage
                << " surface gap=" << diagnostic.surfaceGap << '\n';
            Require(progress == RegionalRoute::Progress::Ready, "Spawn-derived regional route must be complete");
            float dx = next.x - replay.start.x, dy = next.y - replay.start.y;
            Require(dx * dx + dy * dy <= 80.01f * 80.01f, "Regional movement must retain bounded walking legs");
            if (std::string_view(replay.name).starts_with("Den exit"))
                for (bool reverse : {false, true})
                {
                    auto position = reverse ? replay.goal : replay.start;
                    auto destination = reverse ? replay.start : replay.goal;
                    unsigned steps = 0;
                    while (++steps <= 32)
                    {
                        float x = destination.x - position.x, y = destination.y - position.y;
                        if (x * x + y * y <= 3.0f * 3.0f)
                            break;
                        auto step = route.Update(position, destination, filter, next, diagnostic);
                        Require(step == RegionalRoute::Progress::Ready,
                            "Every reported Den route leg must remain valid in both directions");
                        x = next.x - position.x;
                        y = next.y - position.y;
                        Require(x * x + y * y > 0.01f * 0.01f && x * x + y * y <= 80.01f * 80.01f,
                            "Replayed walking legs must advance and stay bounded");
                        position = next;
                    }
                    Require(steps <= 32, "The Den route must finish within bounded replay steps");
                    std::cout << "Den complete replay reverse=" << reverse << " steps=" << steps - 1 << '\n';
                }
        }
        std::cout << "Navigation replay passed: " << checked << " routes; search-budget regression reproduced; "
            << loaded << " tiles loaded\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
