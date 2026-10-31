#pragma once

#include "NavmeshRoute.h"
#include "DetourAlloc.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <queue>
#include <set>

namespace WrathIdle
{
// A private planning mesh avoids loading distant terrain, creatures, and collision grids into the live map.
// Only complete routes produce movement. File presence is a coarse search hint, never proof of walkability.
class RegionalRoute
{
public:
    enum class Progress { Loading, Ready, Failed };
    using Tile = std::pair<int, int>;

    bool Begin(std::filesystem::path const& directory, uint32_t map, Tile start, Tile end)
    {
        _failed = true;
        _directory = directory;
        _map = std::to_string(map);
        if (_map.size() < 3)
            _map.insert(0, 3 - _map.size(), '0');
        std::ifstream file(directory / (_map + ".mmap"), std::ios::binary);
        dtNavMeshParams parameters{};
        if (!file.read(reinterpret_cast<char*>(&parameters), sizeof(parameters)))
            return false;
        _mesh.reset(dtAllocNavMesh());
        if (!_mesh || !CompleteQuery(_mesh->init(&parameters)))
            return false;

        std::set<Tile> available;
        std::error_code error;
        std::filesystem::directory_iterator files(directory, error);
        if (error)
            return false;
        for (std::filesystem::directory_iterator endFiles; files != endFiles; files.increment(error))
        {
            if (error)
                return false;
            auto name = files->path().filename().string();
            if (name.size() != _map.size() + 11 || !name.starts_with(_map) || !name.ends_with(".mmtile"))
                continue;
            auto digits = name.substr(_map.size(), 4);
            if (digits.find_first_not_of("0123456789") != std::string::npos)
                continue;
            int x = std::stoi(digits.substr(0, 2)), y = std::stoi(digits.substr(2, 2));
            if (x < 64 && y < 64)
                available.emplace(x, y);
        }
        if (error)
            return false;
        if (!available.count(start) || !available.count(end))
            return false;
        std::queue<Tile> pending;
        std::map<Tile, Tile> previous{{start, start}};
        pending.push(start);
        while (!pending.empty() && !previous.count(end))
        {
            auto current = pending.front();
            pending.pop();
            for (Tile delta : {Tile{1, 0}, Tile{-1, 0}, Tile{0, 1}, Tile{0, -1}})
            {
                Tile next{current.first + delta.first, current.second + delta.second};
                if (available.count(next) && previous.emplace(next, current).second)
                    pending.push(next);
            }
        }
        if (!previous.count(end))
            return false;
        std::set<Tile> corridor;
        auto tile = end;
        while (true)
        {
            for (int x = -1; x <= 1; ++x)
                for (int y = -1; y <= 1; ++y)
                {
                    Tile neighbor{tile.first + x, tile.second + y};
                    if (available.count(neighbor))
                        corridor.insert(neighbor);
                }
            if (tile == start)
                break;
            tile = previous.at(tile);
        }
        if (corridor.size() > 128)
            return false;
        _tiles.assign(corridor.begin(), corridor.end());
        _next = 0;
        _bytes = 0;
        _failed = false;
        return true;
    }

    Progress Update(RoutePoint start, RoutePoint goal, dtQueryFilter const& filter, RoutePoint& next,
        RouteDiagnostic& diagnostic)
    {
        if (_failed || !_mesh)
            return Progress::Failed;
        // At most one tile per controller update; no full-map terrain preload or background compilation.
        if (_next < _tiles.size())
        {
            auto [x, y] = _tiles[_next++];
            auto digits = [](int value) { return (value < 10 ? "0" : "") + std::to_string(value); };
            std::ifstream file(_directory / (_map + digits(x) + digits(y) + ".mmtile"), std::ios::binary);
            std::array<uint32_t, 14> header{};
            if (!file.read(reinterpret_cast<char*>(header.data()), sizeof(header)) ||
                header[0] != 0x4d4d4150 || header[2] != 20 || header[3] < sizeof(dtMeshHeader) ||
                header[3] > 16 * 1024 * 1024 || _bytes + header[3] > 128 * 1024 * 1024)
            {
                _failed = true;
                return Progress::Failed;
            }
            std::unique_ptr<unsigned char, decltype(&dtFree)> data(
                static_cast<unsigned char*>(dtAlloc(header[3], DT_ALLOC_PERM)), dtFree);
            if (!data || !file.read(reinterpret_cast<char*>(data.get()), header[3]) ||
                !CompleteQuery(_mesh->addTile(data.get(), int(header[3]), DT_TILE_FREE_DATA, 0, nullptr)))
            {
                _failed = true;
                return Progress::Failed;
            }
            data.release();
            _bytes += header[3];
            return Progress::Loading;
        }
        if (PlanNavmeshLeg(_mesh.get(), start, goal, filter, next, diagnostic, 65535))
            return Progress::Ready;
        _failed = true;
        return Progress::Failed;
    }

private:
    std::filesystem::path _directory;
    std::string _map;
    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> _mesh{nullptr, dtFreeNavMesh};
    std::vector<Tile> _tiles;
    size_t _next = 0;
    size_t _bytes = 0;
    bool _failed = false;
};
}
