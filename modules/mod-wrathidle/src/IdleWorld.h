#pragma once

#include "CreatureData.h"
#include "GameObject.h"
#include "GridDefines.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "DBCStores.h"
#include "Player.h"

#include <unordered_map>
#include <mutex>
#include <vector>

namespace WrathIdle
{
// World records describe destinations. Live entities are always resolved again before interaction.
inline std::vector<CreatureData> const& MapCreatures(uint32 map)
{
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    static std::unordered_map<uint32, std::vector<CreatureData>> index;
    auto [it, inserted] = index.try_emplace(map);
    if (inserted)
        for (auto const& [id, spawn] : sObjectMgr->GetAllCreatureData())
            if (spawn.mapid == map)
            {
                it->second.push_back(spawn);
                it->second.back().spawnId = id;
            }
    return it->second;
}

inline std::vector<GameObjectData> const& MapQuestObjects(uint32 map)
{
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    static std::unordered_map<uint32, std::vector<GameObjectData>> index;
    auto [it, inserted] = index.try_emplace(map);
    if (inserted)
        for (auto const& [id, spawn] : sObjectMgr->GetAllGOData())
            if (spawn.mapid == map)
            {
                it->second.push_back(spawn);
                it->second.back().spawnId = id;
            }
    return it->second;
}

inline bool FriendlySpawn(Player* player, CreatureData const& spawn)
{
    // The core updates this index as game events activate or deactivate spawns.
    GridCoord grid = Acore::ComputeGridCoord(spawn.posX, spawn.posY);
    uint8 spawnMode = spawn.mapid == player->GetMapId() ? player->GetMap()->GetSpawnMode() : 0;
    auto const& active = sObjectMgr->GetGridObjectGuids(spawn.mapid, spawnMode, grid.GetId()).creatures;
    if (active.find(spawn.spawnId) == active.end())
        return false;
    auto creature = sObjectMgr->GetCreatureTemplate(spawn.id);
    auto faction = creature ? sFactionTemplateStore.LookupEntry(creature->faction) : nullptr;
    return creature && faction && (spawn.phaseMask & player->GetPhaseMask()) &&
        !faction->IsHostileTo(*player->GetFactionTemplateEntry());
}
}
