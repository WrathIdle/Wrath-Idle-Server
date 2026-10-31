#include "IdleQuest.h"
#include "IdleNavigation.h"
#include "IdleQuestPolicy.h"
#include "IdleWorld.h"
#include "IdleTravel.h"
#include "PlayerbotsExecutor.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameEventMgr.h"
#include "Item.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "StatsWeightCalculator.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <limits>
#include <vector>

namespace WrathIdle
{
static_assert(QuestExplorationEventFlag == QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT);

namespace
{
bool SimpleQuestObject(uint32 entry, uint32 questId)
{
    auto object = sObjectMgr->GetGameObjectTemplate(entry);
    return object && object->type == GAMEOBJECT_TYPE_GOOBER && !object->ScriptId && object->AIName.empty() &&
        !object->goober.lockId && !object->goober.eventId && !object->goober.spellId &&
        !object->goober.linkedTrapId && !object->goober.gossipID &&
        (object->goober.questId <= 0 || uint32(object->goober.questId) == questId);
}

bool QuestObjectAvailable(Player* player, GameObjectData const& spawn)
{
    if (player->GetMap()->GetGORespawnTime(spawn.spawnId))
        return false;
    auto bounds = player->GetMap()->GetGameObjectBySpawnIdStore().equal_range(spawn.spawnId);
    if (bounds.first == bounds.second)
        return !player->GetMap()->IsGridLoaded(spawn.posX, spawn.posY);
    for (auto it = bounds.first; it != bounds.second; ++it)
    {
        auto object = it->second;
        if (object->isSpawned() && object->getLootState() == GO_READY && player->CanSeeOrDetect(object) &&
            !object->HasGameObjectFlag(GO_FLAG_IN_USE | GO_FLAG_NOT_SELECTABLE))
            return true;
    }
    return false;
}

uint32 ParentArea(uint32 id)
{
    auto area = sAreaTableStore.LookupEntry(id);
    return area ? area->zone : 0;
}

bool SafeQuestWork(Player* player, QuestIntent const& intent)
{
    if (intent.step == QuestStep::UseObject || intent.step == QuestStep::Gather)
        return true;
    auto creature = sObjectMgr->GetCreatureTemplate(intent.creature);
    return intent.step == QuestStep::Fight && creature && !creature->rank &&
        creature->minlevel <= player->GetLevel();
}

bool RelevantPickup(Player* player, uint32 questId)
{
    auto quest = sObjectMgr->GetQuestTemplate(questId);
    return quest && (!quest->GetEventIdForQuest() || sGameEventMgr->IsActiveEvent(quest->GetEventIdForQuest())) &&
        AutomaticQuestLevel(player->GetLevel(), uint32(player->GetQuestLevel(quest)),
        quest->GetRequiredClasses() || quest->GetRewSpell() || quest->GetRewSpellCast());
}

bool PickupSpawnAvailable(Player* player, CreatureData const& spawn, uint32 questId)
{
    auto creature = sObjectMgr->GetCreatureTemplate(spawn.id);
    if (!FriendlySpawn(player, spawn) || !creature || !(creature->npcflag & UNIT_NPC_FLAG_QUESTGIVER))
        return false;
    auto const& live = player->GetMap()->GetCreatureBySpawnIdStore();
    auto bounds = live.equal_range(spawn.spawnId);
    for (auto it = bounds.first; it != bounds.second; ++it)
    {
        Creature* npc = it->second;
        if (npc->IsAlive() && player->CanSeeOrDetect(npc) && npc->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER) &&
            npc->hasQuest(questId) && player->GetQuestDialogStatus(npc) != DIALOG_STATUS_NONE)
            return true;
    }
    // Scripted offers need a live confirmation. World records alone do not imply the script offers this quest.
    return bounds.first == bounds.second && !creature->ScriptID;
}
}

namespace
{
bool WorldMap(uint32 map)
{
    auto entry = sMapStore.LookupEntry(map);
    return entry && entry->map_type == MAP_COMMON;
}

std::vector<TransitLink> const& StaticPortals()
{
    static std::vector<TransitLink> const links = []
    {
        std::vector<TransitLink> result;
        for (auto const& [id, portal] : sObjectMgr->GetAllAreaTriggerTeleports())
        {
            auto trigger = sObjectMgr->GetAreaTrigger(id);
            if (!trigger || !WorldMap(trigger->map) || !WorldMap(portal.target_mapId))
                continue;
            result.push_back({TransitKind::AreaPortal, {trigger->map, trigger->x, trigger->y, trigger->z},
                {portal.target_mapId, portal.target_X, portal.target_Y, portal.target_Z}, id, 0, 0, 0, 1});
        }
        for (auto const& [id, spawn] : sObjectMgr->GetAllGOData())
        {
            auto object = sObjectMgr->GetGameObjectTemplate(spawn.id);
            if (!object || object->type != GAMEOBJECT_TYPE_SPELLCASTER || !WorldMap(spawn.mapid))
                continue;
            auto spell = sSpellMgr->GetSpellInfo(object->spellcaster.spellId);
            if (!spell)
                continue;
            for (uint32 effect = 0; effect < MAX_SPELL_EFFECTS; ++effect)
            {
                if (spell->Effects[effect].Effect != SPELL_EFFECT_TELEPORT_UNITS)
                    continue;
                auto destination = sSpellMgr->GetSpellTargetPosition(spell->Id, SpellEffIndex(effect));
                if (!destination || !WorldMap(destination->target_mapId) || id > UINT32_MAX)
                    continue;
                result.push_back({TransitKind::ObjectPortal, {spawn.mapid, spawn.posX, spawn.posY, spawn.posZ},
                    {destination->target_mapId, destination->target_X, destination->target_Y, destination->target_Z},
                    spawn.id, uint32(id), 0, 0, 2});
            }
        }
        std::sort(result.begin(), result.end(), [](auto const& a, auto const& b)
        {
            return std::tie(a.kind, a.id, a.source) < std::tie(b.kind, b.id, b.source);
        });
        return result;
    }();
    return links;
}

struct FlightStop
{
    uint32 node;
    uint32 spawn;
    WorldPoint position;
};

std::vector<FlightStop> FlightStops(Player* player)
{
    // Static node matching is independent of reputation/phase. Those checks remain character-specific below.
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    static std::unordered_map<uint32, std::vector<FlightStop>> cache;
    auto [entry, inserted] = cache.try_emplace(player->GetTeamId(true));
    if (inserted)
        for (auto const& [id, spawn] : sObjectMgr->GetAllCreatureData())
        {
            auto creature = sObjectMgr->GetCreatureTemplate(spawn.id);
            if (!creature || !(creature->npcflag & UNIT_NPC_FLAG_FLIGHTMASTER) || !WorldMap(spawn.mapid) ||
                id > UINT32_MAX)
                continue;
            uint32 node = sObjectMgr->GetNearestTaxiNode(spawn.posX, spawn.posY, spawn.posZ, spawn.mapid,
                player->GetTeamId(true));
            auto taxi = sTaxiNodesStore.LookupEntry(node);
            if (taxi && WorldDistance({spawn.mapid, spawn.posX, spawn.posY, spawn.posZ},
                {taxi->map_id, taxi->x, taxi->y, taxi->z}) < 100)
                entry->second.push_back({node, uint32(id), {spawn.mapid, spawn.posX, spawn.posY, spawn.posZ}});
        }
    return entry->second;
}
}

std::vector<TransitLink> WorldTransitLinks(Player* player)
{
    std::vector<TransitLink> result;
    for (auto const& portal : StaticPortals())
    {
        if (portal.kind == TransitKind::ObjectPortal)
        {
            auto spawn = sObjectMgr->GetGameObjectData(portal.source);
            auto object = spawn ? sObjectMgr->GetGameObjectTemplate(spawn->id) : nullptr;
            if (!object || !(spawn->phaseMask & player->GetPhaseMask()) || object->spellcaster.partyOnly)
                continue;
            auto addon = sObjectMgr->GetGameObjectTemplateAddon(spawn->id);
            auto faction = addon ? sFactionTemplateStore.LookupEntry(addon->faction) : nullptr;
            if (faction && faction->IsHostileTo(*player->GetFactionTemplateEntry()))
                continue;
        }
        result.push_back(portal);
    }
    std::map<uint32, FlightStop> stops;
    for (auto const& stop : FlightStops(player))
    {
        auto spawn = sObjectMgr->GetCreatureData(stop.spawn);
        if (spawn && FriendlySpawn(player, *spawn) && player->m_taxi.IsTaximaskNodeKnown(stop.node))
        {
            auto [it, inserted] = stops.try_emplace(stop.node, stop);
            if (!inserted && stop.spawn < it->second.spawn)
                it->second = stop;
        }
    }
    for (auto const& [source, destinations] : sTaxiPathSetBySource)
        for (auto const& [destination, path] : destinations)
        {
            auto from = stops.find(source), to = stops.find(destination);
            if (!path || from == stops.end() || to == stops.end() || path->ID >= sTaxiPathNodesByPath.size())
                continue;
            float length = 0;
            TaxiPathNodeEntry const* previous = nullptr;
            for (auto node : sTaxiPathNodesByPath[path->ID])
            {
                if (previous && node && previous->mapid == node->mapid)
                    length += WorldDistance({node->mapid, node->x, node->y, node->z},
                        {previous->mapid, previous->x, previous->y, previous->z});
                previous = node;
            }
            result.push_back({TransitKind::Flight, from->second.position, to->second.position,
                from->second.spawn, source, destination, path->price, std::max(1.0f, length / 32.0f)});
        }
    return result;
}

bool SupportedQuest(Quest const* quest)
{
    if (!quest || quest->GetSuggestedPlayers() > 1 || quest->GetTimeAllowed() ||
        !SupportedObjectiveFlags(*quest) ||
        (quest->GetFlags() & (QUEST_FLAGS_RAID | QUEST_FLAGS_FLAGS_PVP | QUEST_FLAGS_UNAVAILABLE)))
        return false;
    for (int32 entry : quest->RequiredNpcOrGo)
        if (entry < 0 && !SimpleQuestObject(uint32(-int64(entry)), quest->GetQuestId()))
            return false;
    return true;
}

std::vector<uint32_t> QuestProgress(Player* player, uint32_t questId)
{
    std::vector<uint32_t> result{uint32(player->GetQuestStatus(questId)),
        uint32(player->GetQuestRewardStatus(questId))};
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return result;
    uint16 slot = player->FindQuestSlot(questId);
    if (slot < MAX_QUEST_LOG_SIZE)
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            result.push_back(player->GetQuestSlotCounter(slot, i));
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        result.push_back(quest->RequiredItemId[i] ?
            std::min(player->GetItemCount(quest->RequiredItemId[i]), quest->RequiredItemCount[i]) : 0);
    return result;
}

QuestIntent PlanQuest(Player* player, uint32_t questId, bool interact)
{
    QuestIntent result;
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!SupportedQuest(quest))
    {
        result.reason = "This quest requires an objective handler that is not implemented yet.";
        return result;
    }
    if (player->GetQuestRewardStatus(questId))
    {
        result.step = QuestStep::Complete;
        result.reason = "Quest reward was received.";
        return result;
    }
    QuestStatus status = player->GetQuestStatus(questId);
    if (status == QUEST_STATUS_FAILED)
    {
        result.reason = "Quest failed; it remains in the journal for review.";
        return result;
    }
    if (status == QUEST_STATUS_NONE && (!player->CanTakeQuest(quest, false) || !player->CanAddQuest(quest, false)))
    {
        result.reason = "Quest prerequisites are not met, or the quest journal is full.";
        return result;
    }
    bool turnIn = status == QUEST_STATUS_COMPLETE;
    std::vector<uint32_t> kills;
    std::vector<uint32_t> objects;
    std::vector<uint32_t> neededItems;
    if (status == QUEST_STATUS_INCOMPLETE)
    {
        uint16 slot = player->FindQuestSlot(questId);
        if (slot >= MAX_QUEST_LOG_SIZE)
        {
            result.reason = "Quest journal state changed; refresh and retry.";
            return result;
        }
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            if (quest->RequiredNpcOrGo[i] && player->GetQuestSlotCounter(slot, i) < quest->RequiredNpcOrGoCount[i])
            {
                int32 entry = quest->RequiredNpcOrGo[i];
                if (entry > 0)
                    kills.push_back(uint32(entry));
                else
                    objects.push_back(uint32(-int64(entry)));
            }
        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            if (quest->RequiredItemId[i] &&
                player->GetItemCount(quest->RequiredItemId[i]) < quest->RequiredItemCount[i])
                neededItems.push_back(quest->RequiredItemId[i]);
        if (kills.empty() && objects.empty() && neededItems.empty())
        {
            result.reason = "Waiting for authoritative quest completion; no supported objective remains.";
            return result;
        }
    }
    bool objective = !kills.empty() || !objects.empty() || !neededItems.empty();
    // Opening a container is a normal spell cast. Its loot window arrives on a later update.
    if (interact && player->GetLootGUID().IsGameObject())
        if (GameObject* object = player->GetMap()->GetGameObject(player->GetLootGUID()))
            if (HelpsQuestObjective(0, {}, neededItems, sObjectMgr->GetGameObjectQuestItemList(object->GetEntry())) &&
                object->IsAtInteractDistance(player) && player->CanSeeOrDetect(object))
            {
                ObjectGuid guid = object->GetGUID();
                for (uint8 i = 0; i < MAX_NR_LOOT_ITEMS + MAX_NR_QUEST_ITEMS; ++i)
                    if (object->loot.LootItemInSlot(i, player))
                    {
                        WorldPacket take(CMSG_AUTOSTORE_LOOT_ITEM);
                        take << i;
                        player->GetSession()->HandleAutostoreLootItemOpcode(take);
                        if (!object->loot.LootItemInSlot(i, player))
                            ++result.lootCount;
                    }
                WorldPacket money(CMSG_LOOT_MONEY);
                player->GetSession()->HandleLootMoneyOpcode(money);
                WorldPacket release(CMSG_LOOT_RELEASE);
                release << guid;
                player->GetSession()->HandleLootReleaseOpcode(release);
                result.step = QuestStep::Interacted;
                result.reason = "Collected the quest container's available loot.";
                return result;
            }
    auto relations = turnIn ? sObjectMgr->GetCreatureQuestInvolvedRelationMap() :
        sObjectMgr->GetCreatureQuestRelationMap();
    float nearest = std::numeric_limits<float>::max();
    uint32 entry = 0;
    bool objectDestination = false;
    bool waitingForObject = false;
    // Static records only choose a destination; interactions resolve and validate a live object below.
    for (auto const& spawn : MapCreatures(player->GetMapId()))
    {
        if (!(spawn.phaseMask & player->GetPhaseMask()))
            continue;
        bool matches = HelpsQuestObjective(spawn.id, kills, neededItems,
            sObjectMgr->GetCreatureQuestItemList(spawn.id));
        if (!objective)
        {
            auto range = relations->equal_range(spawn.id);
            for (auto it = range.first; it != range.second; ++it)
                matches = matches || it->second == questId;
        }
        if (!matches)
            continue;
        if (status == QUEST_STATUS_NONE && !PickupSpawnAvailable(player, spawn, questId))
            continue;
        float distance = player->GetExactDistSq(spawn.posX, spawn.posY, spawn.posZ);
        if (distance >= nearest)
            continue;
        nearest = distance;
        entry = spawn.id;
        result.destination.Relocate(spawn.posX, spawn.posY, spawn.posZ, spawn.orientation);
    }
    auto objectRelations = turnIn ? sObjectMgr->GetGOQuestInvolvedRelationMap() : sObjectMgr->GetGOQuestRelationMap();
    for (auto const& spawn : MapQuestObjects(player->GetMapId()))
    {
        if (spawn.mapid != player->GetMapId() || !(spawn.phaseMask & player->GetPhaseMask()))
            continue;
        auto object = sObjectMgr->GetGameObjectTemplate(spawn.id);
        bool matches = std::find(objects.begin(), objects.end(), spawn.id) != objects.end() ||
            (object && object->type == GAMEOBJECT_TYPE_CHEST &&
                HelpsQuestObjective(0, {}, neededItems, sObjectMgr->GetGameObjectQuestItemList(spawn.id)));
        if (!objective)
        {
            auto bounds = objectRelations->equal_range(spawn.id);
            for (auto it = bounds.first; it != bounds.second; ++it)
                matches = matches || it->second == questId;
        }
        float distance = player->GetExactDistSq(spawn.posX, spawn.posY, spawn.posZ);
        if (!matches || distance >= nearest)
            continue;
        if (objective && !QuestObjectAvailable(player, spawn))
        {
            waitingForObject = true;
            continue;
        }
        nearest = distance;
        entry = spawn.id;
        objectDestination = true;
        result.spawn = spawn.spawnId;
        result.destination.Relocate(spawn.posX, spawn.posY, spawn.posZ, spawn.orientation);
    }
    if (!entry)
    {
        result.step = waitingForObject ? QuestStep::Waiting : QuestStep::Blocked;
        result.reason = waitingForObject ? "Waiting for an eligible quest object to respawn or become available." :
            "No supported quest destination exists on the current map in this phase.";
        return result;
    }
    auto objectTemplate = objectDestination ? sObjectMgr->GetGameObjectTemplate(entry) : nullptr;
    bool gather = objective && objectTemplate && objectTemplate->type == GAMEOBJECT_TYPE_CHEST;
    if (objectDestination && objective)
        result.tolerance = 0.5f;
    result.step = objective ? (objectDestination ? (gather ? QuestStep::Gather : QuestStep::UseObject) :
        QuestStep::Fight) : QuestStep::Travel;
    result.creature = entry;
    result.reason = objective ? (objectDestination ? "Traveling to use the remaining quest object." :
        "Hunting the remaining quest objective.") :
        (turnIn ? "Returning to the quest giver for the reward." : "Traveling to accept the selected quest.");
    if ((objective && !objectDestination) || !interact)
        return result;
    WorldObject* npc = nullptr;
    if (objectDestination)
    {
        GameObject* object = nullptr;
        auto bounds = player->GetMap()->GetGameObjectBySpawnIdStore().equal_range(result.spawn);
        for (auto it = bounds.first; it != bounds.second; ++it)
            if (it->second->GetEntry() == entry && it->second->isSpawned() &&
                player->CanSeeOrDetect(it->second))
                object = it->second;
        if (!object)
            return result;
        result.destination = object->GetPosition();
        if (!object->IsAtInteractDistance(player))
        {
            result.reason = "Moving within the quest object's interaction range.";
            return result;
        }
        if (objective)
        {
            if (gather)
            {
                if (object->getLootState() != GO_READY || object->HasGameObjectFlag(GO_FLAG_IN_USE | GO_FLAG_NOT_SELECTABLE))
                {
                    result.step = QuestStep::Waiting;
                    result.reason = "Waiting for the quest container to become available.";
                }
                else if (PlayerbotsExecutor::OpenObject(player, object))
                {
                    result.step = QuestStep::Waiting;
                    result.reason = "Opening the quest container using its normal lock and skill rules.";
                }
                else
                {
                    result.step = QuestStep::FailedInteraction;
                    result.reason = "The quest container could not be opened with current skills and keys.";
                }
                return result;
            }
            if (!SimpleQuestObject(entry, questId) || object->getLootState() != GO_READY ||
                object->HasGameObjectFlag(GO_FLAG_IN_USE | GO_FLAG_NOT_SELECTABLE | GO_FLAG_LOCKED))
                return result;
            auto before = QuestProgress(player, questId);
            WorldPacket use(CMSG_GAMEOBJ_USE);
            use << object->GetGUID();
            player->GetSession()->HandleGameObjectUseOpcode(use);
            result.step = before != QuestProgress(player, questId) ?
                QuestStep::Interacted : QuestStep::FailedInteraction;
            result.reason = result.step == QuestStep::Interacted ? "Quest object advanced the objective." :
                "Using the quest object did not advance the objective.";
            return result;
        }
        npc = object;
    }
    else
        npc = player->FindNearestCreature(entry, INTERACTION_DISTANCE);
    if (!npc || !player->CanInteractWithQuestGiver(npc))
        return result;
    if (turnIn)
    {
        uint32 choice = 0;
        float bestUpgrade = 0;
        uint64 bestPrice = 0;
        StatsWeightCalculator weights(player);
        weights.SetItemSetBonus(false);
        weights.SetOverflowPenalty(false);
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            auto item = sObjectMgr->GetItemTemplate(quest->RewardChoiceItemId[i]);
            if (!item)
                continue;
            float upgrade = 0;
            if (player->CanUseItem(item) == EQUIP_ERR_OK)
            {
                uint8 slot = player->FindEquipSlot(item, NULL_SLOT, true);
                if (slot < EQUIPMENT_SLOT_END && !(item->InventoryType == INVTYPE_2HWEAPON &&
                    player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND)))
                {
                    Item* previous = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                    upgrade = weights.CalculateItem(item->ItemId, 0, slot) - (previous ?
                        weights.CalculateItem(previous->GetEntry(), previous->GetItemRandomPropertyId(), slot) : 0);
                    upgrade = std::max(0.0f, upgrade);
                }
            }
            uint64 price = uint64(item->SellPrice) * quest->RewardChoiceItemCount[i];
            if (upgrade > bestUpgrade || (upgrade == bestUpgrade && price > bestPrice))
            {
                choice = i;
                bestUpgrade = upgrade;
                bestPrice = price;
            }
        }
        WorldPacket packet(CMSG_QUESTGIVER_CHOOSE_REWARD);
        packet << npc->GetGUID() << questId << choice;
        player->GetSession()->HandleQuestgiverChooseRewardOpcode(packet);
    }
    else
    {
        WorldPacket packet(CMSG_QUESTGIVER_ACCEPT_QUEST);
        packet << npc->GetGUID() << questId << uint32(0);
        player->GetSession()->HandleQuestgiverAcceptQuestOpcode(packet);
    }
    result.step = player->GetQuestStatus(questId) != status || player->GetQuestRewardStatus(questId) ?
        QuestStep::Interacted : QuestStep::FailedInteraction;
    return result;
}
std::vector<QuestListing> ListQuests(Player* player)
{
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    static std::unordered_map<uint32, std::vector<uint32>> catalogs;
    auto [catalog, inserted] = catalogs.try_emplace(player->GetMapId());
    if (inserted)
    {
        auto& result = catalog->second;
        auto relations = sObjectMgr->GetCreatureQuestRelationMap();
        for (auto const& spawn : MapCreatures(player->GetMapId()))
        {
            auto bounds = relations->equal_range(spawn.id);
            for (auto it = bounds.first; it != bounds.second; ++it)
                result.push_back(it->second);
        }
        auto objectRelations = sObjectMgr->GetGOQuestRelationMap();
        for (auto const& spawn : MapQuestObjects(player->GetMapId()))
        {
            auto bounds = objectRelations->equal_range(spawn.id);
            for (auto it = bounds.first; it != bounds.second; ++it)
                result.push_back(it->second);
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
    }
    std::vector<QuestListing> result;
    auto visibleIds = catalog->second;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        if (uint32 id = player->GetQuestSlotQuestId(slot))
            visibleIds.push_back(id);
    std::sort(visibleIds.begin(), visibleIds.end());
    visibleIds.erase(std::unique(visibleIds.begin(), visibleIds.end()), visibleIds.end());
    for (uint32 id : visibleIds)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(id);
        if (!quest || player->GetQuestRewardStatus(id))
            continue;
        auto status = player->GetQuestStatus(id);
        if (status == QUEST_STATUS_NONE && (!player->CanTakeQuest(quest, false) ||
            (quest->GetEventIdForQuest() && !sGameEventMgr->IsActiveEvent(quest->GetEventIdForQuest()))))
            continue;
        result.push_back({id, uint32(status), SupportedQuest(quest), quest->GetTitle()});
    }
    return result;
}

namespace
{
std::vector<QuestWorkCandidate> QuestWork(Player* player, std::vector<uint32_t> const& avoided,
    std::vector<uint32_t> const* queued, uint32_t completedQuest)
{
    std::vector<QuestWorkCandidate> candidates;
    Quest const* completed = sObjectMgr->GetQuestTemplate(completedQuest);
    uint32 completedArea = completed && completed->GetZoneOrSort() > 0 ? uint32(completed->GetZoneOrSort()) : 0;
    for (auto const& listing : ListQuests(player))
    {
        if (!listing.supported || listing.status != QUEST_STATUS_INCOMPLETE ||
            std::find(avoided.begin(), avoided.end(), listing.id) != avoided.end() ||
            (queued && std::find(queued->begin(), queued->end(), listing.id) == queued->end()))
            continue;
        QuestIntent intent = PlanQuest(player, listing.id, false);
        Quest const* quest = sObjectMgr->GetQuestTemplate(listing.id);
        uint32 area = quest && quest->GetZoneOrSort() > 0 ? uint32(quest->GetZoneOrSort()) : 0;
        auto creature = intent.step == QuestStep::Fight ? sObjectMgr->GetCreatureTemplate(intent.creature) : nullptr;
        uint32 requiredLevel = creature && !creature->rank && !creature->npcflag && creature->ModExperience > 0 ?
            creature->minlevel : 0;
        bool field = intent.step == QuestStep::Fight || intent.step == QuestStep::UseObject ||
            intent.step == QuestStep::Gather;
        candidates.push_back({listing.id, true, field, SafeQuestWork(player, intent),
            player->GetExactDist2d(intent.destination.GetPositionX(), intent.destination.GetPositionY()),
            area, RelatedQuestAreas(area, player->GetAreaId(), ParentArea) ||
                RelatedQuestAreas(area, completedArea, ParentArea), requiredLevel});
    }
    return candidates;
}
}

uint32_t ChooseQuest(Player* player, std::vector<uint32_t> const& avoided, uint32_t completedQuest)
{
    if (uint32 pickup = ChooseQuestPickup(player, avoided))
        return pickup;
    auto work = QuestWork(player, avoided, nullptr, completedQuest);
    if (uint32 nearby = NearbyQuestWork(work))
        return nearby;
    std::vector<QuestPlanCandidate> candidates;
    std::vector<QuestPlanCandidate> localRewards;
    for (auto const& listing : ListQuests(player))
    {
        if (!listing.supported || std::find(avoided.begin(), avoided.end(), listing.id) != avoided.end() ||
            (listing.status == QUEST_STATUS_NONE && !RelevantPickup(player, listing.id)))
            continue;
        QuestIntent intent = PlanQuest(player, listing.id, false);
        if (intent.step == QuestStep::Blocked || intent.step == QuestStep::Complete)
            continue;
        if ((intent.step == QuestStep::Fight || intent.step == QuestStep::UseObject || intent.step == QuestStep::Gather) &&
            !SafeQuestWork(player, intent))
            continue;
        float distance = player->GetExactDist2d(intent.destination.GetPositionX(), intent.destination.GetPositionY());
        if (listing.status == QUEST_STATUS_COMPLETE && distance <= 600)
        {
            uint32 area = player->GetMap()->GetAreaId(player->GetPhaseMask(), intent.destination.GetPositionX(),
                intent.destination.GetPositionY(), intent.destination.GetPositionZ());
            if (LocalQuestReward(distance, RelatedQuestAreas(area, player->GetAreaId(), ParentArea)))
                localRewards.push_back({listing.id, true, distance});
        }
        candidates.push_back({listing.id, listing.status != QUEST_STATUS_NONE, distance});
    }
    if (NeedsLocalQuestExperience(work, player->GetLevel()))
        return AcceptedQuestFirst(localRewards); // Take local rewards, or hunt safely until the objectives are eligible.
    return AcceptedQuestFirst(candidates);
}

uint32_t ChooseNearbyQuest(Player* player, std::vector<uint32_t> const& avoided,
    std::vector<uint32_t> const* queued, uint32_t completedQuest)
{
    return NearbyQuestWork(QuestWork(player, avoided, queued, completedQuest));
}

bool ExplainQuestHunting(Player* player, std::vector<uint32_t> const& avoided,
    std::vector<uint32_t> const& unavailable, std::string& reason)
{
    auto work = QuestWork(player, avoided, nullptr, 0);
    bool blockedAccepted = std::any_of(unavailable.begin(), unavailable.end(), [player, &avoided](uint32 id)
    {
        return player->GetQuestStatus(id) == QUEST_STATUS_INCOMPLETE &&
            std::find(avoided.begin(), avoided.end(), id) == avoided.end();
    });
    switch (AutomaticQuestFallback(NeedsLocalQuestExperience(work, player->GetLevel()), blockedAccepted))
    {
        case QuestFallback::Blocked:
            reason = "Accepted quest work could not be completed. Resume to retry, or review the quest journal.";
            return false;
        case QuestFallback::Experience:
            reason = "Earning experience until nearby accepted quest enemies fit the character's level.";
            return true;
        case QuestFallback::NoQuest:
            reason = "Earning experience because no eligible quest work is currently available.";
            return true;
    }
    return false;
}

uint32_t ChooseQuestPickup(Player* player, std::vector<uint32_t> const& avoided,
    std::vector<uint32_t> const* queued)
{
    std::vector<QuestPickupCandidate> candidates;
    for (auto const& listing : ListQuests(player))
    {
        if (!listing.supported || listing.status != QUEST_STATUS_NONE ||
            std::find(avoided.begin(), avoided.end(), listing.id) != avoided.end() ||
            (!queued && !RelevantPickup(player, listing.id)) ||
            (queued && std::find(queued->begin(), queued->end(), listing.id) == queued->end()))
            continue;
        QuestIntent intent = PlanQuest(player, listing.id, false);
        candidates.push_back({listing.id, intent.step == QuestStep::Travel, true,
            player->GetExactDist2d(intent.destination.GetPositionX(), intent.destination.GetPositionY())});
    }
    return NearbyQuestPickup(candidates);
}

}
