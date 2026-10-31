#include "IdleModel.h"
#include "IdleClassPolicy.h"
#include "IdleUpkeep.h"
#include "IdleQuest.h"
#include "IdleQuestPolicy.h"
#include "QuestDef.h"
#include "IdleNavigation.h"
#include "IdleWorld.h"
#include "RegionalRoute.h"
#include "DetourExtended.h"
#include "PlayerbotsExecutor.h"
#include "Bag.h"
#include "IdleWire.h"
#include "CellImpl.h"
#include "Config.h"
#include "Corpse.h"
#include "Creature.h"
#include "EventMap.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemPackets.h"
#include "LootMgr.h"
#include "Log.h"
#include "Map.h"
#include "MoveSpline.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <deque>
#include <cmath>
#include <list>
#include <limits>

namespace WrathIdle
{
namespace
{
bool Enabled = false;
constexpr char Storage[] = "wrathidle.v2";
constexpr char LegacyStorage[] = "wrathidle.v1";
constexpr uint32 ThinkEvent = 1;
constexpr uint32 SecondEvent = 2;
constexpr uint32 SnapshotEvent = 3;
constexpr uint32 SaveEvent = 4;
constexpr uint32 SearchEvent = 5;
constexpr uint32 RetryPathsEvent = 6;
constexpr uint32 UpkeepEvent = 7;
constexpr uint32 ClassActionEvent = 8;
constexpr uint32 SuppliesEvent = 9;
constexpr uint32 TrainingEvent = 10;
constexpr uint32 HuntRetryEvent = 11;

struct Runtime : DataMap::Base
{
    Model model;
    EventMap events;
    uint32 requestId = 0;
    uint32 lastRequestId = 0;
    bool ready = false;
    bool running = false;
    bool recovering = false;
    bool wasDead = false;
    bool loaded = true;
    bool manualTown = false;
    uint32 townStage = 0; // 1 repair/sell, 2 food, 3 return to the interrupted goal
    uint32 townMap = 0;
    Position goalLocation;
    ServiceDestination repairService;
    ServiceDestination foodService;
    std::deque<SupplyRequest> classSupplies;
    bool suppliesReady = true;
    uint32 supplyFailures = 0;
    bool trainingReady = true;
    uint32 trainingFailures = 0;
    Position trainingLocation;
    bool searchReady = true;
    bool upkeepReady = true;
    bool foodNeeded = false;
    bool classReady = true;
    std::string classDecision;
    uint32 spent = 0;
    QuestAttempt questAttempt;
    bool questWorking = false;
    uint32 questRetrySeconds = 0;
    uint32 automaticQuest = 0;
    std::vector<uint32> unavailableQuests;
    uint32 serviceGoal = 0;
    uint32 completedServiceGoal = 0;
    uint32 pathFailures = 0;
    uint32 stalledSeconds = 0;
    Position lastPosition;
    Position destination;
    std::unique_ptr<RegionalRoute> regionalRoute;
    uint32 regionalMap = 0;
    Position regionalGoal;
    ObjectGuid target;
    ObjectGuid lootTarget;
    ObjectGuid searchTarget;
    uint32 huntSpawn = 0;
    uint32 huntMap = 0;
    std::vector<uint32> unavailableHunts;
    ObjectGuid retreatGuard;
    std::vector<ObjectGuid> unreachableTargets;
    std::deque<time_t> deaths;
    std::string action = "Paused";
    std::string reason = "Choose a goal, then Start.";
    uint32 seconds = 0;
    uint32 deathCount = 0;
    uint32 lootCount = 0;
    uint32 travelSeconds = 0;
    uint32 serviceSeconds = 0;
    uint64 earned = 0;
    uint64 paid = 0;
    uint64 xp = 0;
    uint32 previousMoney = 0;
    uint32 previousXp = 0;
    uint32 previousLevel = 0;
    uint32 previousNextXp = 0;
    uint32 commandWindow = 0;
    uint32 commandCount = 0;
};

Runtime* State(Player* player)
{
    return player ? player->CustomData.Get<Runtime>(Storage) : nullptr;
}

void RecordMoney(Player* player, Runtime& state)
{
    uint32 money = player->GetMoney();
    if (money >= state.previousMoney)
        state.earned += money - state.previousMoney;
    else
        state.paid += state.previousMoney - money;
    state.previousMoney = money;
}

void Send(Player* player, std::string_view kind, std::vector<uint32> const& values = {},
    std::vector<std::string> const& strings = {})
{
    if (player->GetSession()->IsSocketClosed())
        return;
    uint16 type = 0;
    for (uint16 i = 1; i < std::size(WrathIdleWire::Updates); ++i)
        if (kind == WrathIdleWire::Updates[i])
            type = i;
    if (!type || values.size() != WrathIdleWire::ValueCounts[type] ||
        strings.size() != WrathIdleWire::StringCounts[type])
        return;
    Runtime* state = State(player);
    WorldPacket packet(SMSG_WRATHIDLE_UPDATE);
    packet << uint16(WrathIdleWire::Version) << type << uint32(state ? state->requestId : 0);
    packet << uint16(values.size());
    for (uint32 value : values)
        packet << value;
    packet << uint8(strings.size());
    for (auto const& text : strings)
    {
        std::string bounded = text.substr(0, WrathIdleWire::MaxString);
        packet << uint16(bounded.size());
        if (!bounded.empty())
            packet.append(bounded.data(), bounded.size());
    }
    player->SendDirectMessage(&packet);
}

void Explain(Player* player, Runtime& state, std::string action, std::string reason)
{
    if (state.action == action && state.reason == reason)
        return;
    state.action = std::move(action);
    state.reason = std::move(reason);
    LOG_INFO("module.wrathidle", "{}: {} - {}", player->GetName(), state.action, state.reason);
    if (state.ready)
        Send(player, "event", {}, {state.action + ": " + state.reason});
}

void Stop(Player* player, Runtime& state, std::string const& reason)
{
    state.running = false;
    state.questWorking = false;
    state.regionalRoute.reset();
    PlayerbotsExecutor::Detach(player);
    state.target.Clear();
    state.searchTarget.Clear();
    state.retreatGuard.Clear();
    Explain(player, state, "Paused", reason);
}

void Store(Player* player, Runtime const& state, bool save)
{
    if (!state.loaded)
        return;
    auto values = Encode(state.model);
    player->UpdatePlayerSetting(Storage, 0, static_cast<uint32>(values.size()));
    for (size_t i = 0; i < values.size(); ++i)
        player->UpdatePlayerSetting(Storage, static_cast<uint32>(i + 1), values[i]);
    if (save)
        player->SaveToDB(false, false);
}

std::vector<Item*> Bags(Player* player)
{
    std::vector<Item*> items;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            items.push_back(item);
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = player->GetBagByPos(bagSlot))
            for (uint8 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(slot))
                    items.push_back(item);
    return items;
}

uint32 FreeSlots(Player* player)
{
    uint32 count = 0;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (!player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++count;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        if (Bag* bag = player->GetBagByPos(slot))
            if (!bag->GetTemplate()->BagFamily)
                count += bag->GetFreeSlots();
    return count;
}

uint32 Durability(Player* player)
{
    uint32 percent = 100;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (uint32 maximum = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY))
                percent = std::min(percent, item->GetUInt32Value(ITEM_FIELD_DURABILITY) * 100 / maximum);
    return percent;
}

void Status(Player* player, Runtime const& state)
{
    if (auto preset = PresetForClass(player->getClass()))
        Send(player, "classstate", {player->getClass()}, {std::string(preset->name), state.classDecision});
    Send(player, "status", {state.model.revision, state.running}, {state.action, state.reason});
    Send(player, "stats", {state.seconds, static_cast<uint32>(state.xp),
        static_cast<uint32>(state.earned), static_cast<uint32>(state.paid), state.deathCount,
        state.lootCount, state.travelSeconds, state.serviceSeconds, FreeSlots(player), Durability(player),
        player->GetMoney(), player->GetItemCount(CarriedFoodEntry(player, state.model.profile)), player->GetLevel(),
        state.model.goals.empty() ? 0 : (state.model.goals.front().kind == 0 ? player->GetLevel() :
            player->GetItemCount(state.model.goals.front().target)),
        state.model.goals.empty() ? 0 : state.model.goals.front().elapsedSeconds});
}

void Snapshot(Player* player, Runtime const& state)
{
    Send(player, "begin", {state.model.revision});
    auto const& values = state.model.profile.values;
    Send(player, "profile", std::vector<uint32_t>(values.begin(), values.end()));
    for (uint32 i = 0; i < MaxRules; ++i)
    {
        auto const& r = state.model.profile.rules[i];
        Send(player, "rule", {i, r.spell, r.minPower, r.maxHealth, r.self, r.missingAura});
    }
    for (uint32 id : state.model.profile.protectedItems)
        Send(player, "keep", {id});
    for (auto const& g : state.model.goals)
        Send(player, "goal", {g.kind, g.target, g.count, g.area, g.limitSeconds, g.elapsedSeconds, g.id});
    for (uint32 id : state.model.profile.avoidedQuests)
        Send(player, "avoid", {id});
    Send(player, "policy", {state.model.profile.autoEquip, state.model.profile.autoTrain,
        state.model.profile.equipBoe, state.model.profile.lockedSlots});
    Send(player, "classpolicy", {state.model.profile.autoTalents, state.model.profile.classPreset});
    Send(player, "end", {state.model.revision});
    Status(player, state);
}

bool Supported(Player* player)
{
    return SupportedProgression(player->getRace(), player->getClass(), player->GetLevel()) &&
        sChrRacesStore.LookupEntry(player->getRace()) &&
        sObjectMgr->GetPlayerInfo(player->getRace(), player->getClass()) && !player->GetGroup() &&
        FinitePosition(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
}

enum class WalkResult
{
    Arrived,
    Pending,
    Blocked
};

WalkResult Walk(Player* player, Runtime& state, Position destination, float tolerance = 3.0f,
    bool pauseOnFailure = true)
{
    if (!FinitePosition(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()) ||
        !Acore::IsValidMapCoord(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()))
    {
        if (pauseOnFailure)
            Stop(player, state, "The selected destination has invalid world coordinates.");
        return WalkResult::Blocked;
    }
    float ground = player->GetMap()->GetHeight(player->GetPhaseMask(), destination.GetPositionX(),
        destination.GetPositionY(), destination.GetPositionZ() + 5.0f, true, 20.0f);
    if (ground > INVALID_HEIGHT)
        destination.m_positionZ = ground;
    if (player->GetExactDist(&destination) <= tolerance)
    {
        state.regionalRoute.reset();
        PlayerbotsExecutor::StopMovement(player);
        return WalkResult::Arrived;
    }
    if (player->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED) || player->IsBeingTeleported())
        return WalkResult::Pending;
    if (!player->movespline->Finalized() && state.destination.GetExactDist(destination) < 3.0f)
        return WalkResult::Pending;
    Position leg = destination;
    bool routeFailed = false;
    if (player->GetExactDist2d(destination.GetPositionX(), destination.GetPositionY()) > 1800.0f)
    {
        if (!state.regionalRoute || state.regionalMap != player->GetMapId() ||
            state.regionalGoal.GetExactDist(destination) > 3.0f)
        {
            state.regionalRoute = std::make_unique<RegionalRoute>();
            state.regionalMap = player->GetMapId();
            state.regionalGoal = destination;
            auto start = Acore::ComputeGridCoord(player->GetPositionX(), player->GetPositionY());
            auto end = Acore::ComputeGridCoord(destination.GetPositionX(), destination.GetPositionY());
            routeFailed = !state.regionalRoute->Begin(std::filesystem::path(sWorld->GetDataPath()) / "mmaps",
                player->GetMapId(), {int(start.x_coord), int(start.y_coord)}, {int(end.x_coord), int(end.y_coord)});
        }
        dtQueryFilterExt filter;
        filter.setIncludeFlags(NAV_GROUND | NAV_GROUND_STEEP | NAV_WATER | NAV_MAGMA);
        RoutePoint next{};
        RouteDiagnostic diagnostic;
        auto progress = routeFailed ? RegionalRoute::Progress::Failed : state.regionalRoute->Update(
            {player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()},
            {destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()}, filter, next, diagnostic);
        if (progress == RegionalRoute::Progress::Loading)
        {
            PlayerbotsExecutor::StopMovement(player);
            return WalkResult::Pending;
        }
        routeFailed = progress == RegionalRoute::Progress::Failed;
        if (routeFailed)
            LOG_WARN("module.wrathidle", "{}: regional walking route rejected: map={} stage={} status={} polygons={}",
                player->GetName(), player->GetMapId(), diagnostic.stage, diagnostic.status, diagnostic.polygons);
        if (!routeFailed)
            leg.Relocate(next.x, next.y, next.z);
    }
    else
        state.regionalRoute.reset();
    if (routeFailed || !PlayerbotsExecutor::Walk(player, leg))
    {
        PlayerbotsExecutor::StopMovement(player);
        if (pauseOnFailure && ++state.pathFailures >= 3)
            Stop(player, state, "No complete walking path. Check navigation data or change the goal.");
        return WalkResult::Blocked;
    }
    state.destination = destination;
    state.pathFailures = 0;
    return WalkResult::Pending;
}

void BeginTown(Player* player, Runtime& state, bool manual)
{
    state.townMap = player->GetMapId();
    if (!state.townStage)
        state.goalLocation = player->GetPosition();
    uint32 flags = UNIT_NPC_FLAG_VENDOR;
    if (Durability(player) <= state.model.profile.values[RepairPercent])
        flags |= UNIT_NPC_FLAG_REPAIR;
    bool generalServices = manual || FreeSlots(player) <= state.model.profile.values[FreeBagSlots] ||
        Durability(player) <= state.model.profile.values[RepairPercent];
    if (generalServices && !FindService(player, flags, 0, state.repairService))
    {
        Stop(player, state, "No eligible vendor with the required services exists within the local route budget.");
        return;
    }
    state.foodService = {};
    uint32 carriedFood = CarriedFoodEntry(player, state.model.profile);
    bool restockFood = state.model.profile.values[FoodStock] &&
        (!carriedFood || player->GetItemCount(carriedFood) < state.model.profile.values[FoodStock]);
    if (restockFood && !FindService(player, UNIT_NPC_FLAG_VENDOR,
        state.model.profile.values[FoodItem], state.foodService, SupplyKind::Food))
    {
        Stop(player, state, "No eligible vendor stocks the configured food within the local route budget.");
        return;
    }
    state.manualTown = manual;
    state.townStage = generalServices ? 1 : 2;
    state.spent = 0;
    state.target.Clear();
    state.searchTarget.Clear();
    PlayerbotsExecutor::StopMovement(player);
    player->AttackStop();
    Explain(player, state, "Travel", "Visiting available vendors for the required supplies and repairs.");
}

void FinishTown(Player* player, Runtime& state)
{
    state.classSupplies.clear();
    if (state.manualTown)
    {
        state.townStage = 0;
        Stop(player, state, "The requested vendor services are complete. Resume when ready to continue.");
    }
    else
    {
        state.townStage = 3;
        Explain(player, state, "Travel", "Returning to the interrupted goal after vendor services.");
    }
}

bool Services(Player* player, Runtime& state)
{
    if (state.townStage && state.townMap != player->GetMapId())
    {
        state.townStage = 0;
        Stop(player, state, "The map changed during a service trip. Resume to plan services on the current map.");
        return true;
    }
    auto const& v = state.model.profile.values;
    if (state.townStage == 3)
    {
        if (Walk(player, state, state.goalLocation) != WalkResult::Arrived)
            return true;
        state.townStage = 0;
        Explain(player, state, "Resume", "Town visit complete; resuming the queued goal at its previous location.");
        return true;
    }
    if (state.townStage == 1)
    {
        Explain(player, state, "Service", "Selling permitted loot and repairing within the spending limit.");
        if (Walk(player, state, state.repairService.position) != WalkResult::Arrived)
            return true;
        Creature* vendor = player->FindNearestCreature(state.repairService.entry, INTERACTION_DISTANCE);
        if (!vendor || !player->GetNPCIfCanInteractWith(vendor->GetGUID(), UNIT_NPC_FLAG_VENDOR))
        {
            Stop(player, state, "The selected vendor is unavailable or cannot be reached.");
            return true;
        }
        for (Item* item : Bags(player))
        {
            auto proto = item->GetTemplate();
            bool protectedItem = proto->Class == ITEM_CLASS_QUEST || proto->StartQuest ||
                item->IsEquipped() || item->IsWrapped() || item->IsRefundable() || player->HasQuestForItem(item->GetEntry()) ||
                player->IsItemInEquipmentSet(item->GetGUID()) || IsPendingUpgrade(player, item) ||
                IsHealingFood(player, item->GetEntry()) || IsClassSupply(player, item->GetEntry());
            if (proto->SellPrice && CanSell(item->GetEntry(), proto->Quality, protectedItem,
                state.model.profile, state.model.goals))
            {
                WorldPackets::Item::SellItem sell{WorldPacket(CMSG_SELL_ITEM)};
                sell.VendorGuid = vendor->GetGUID();
                sell.ItemGuid = item->GetGUID();
                sell.Count = item->GetCount();
                player->GetSession()->HandleSellItemOpcode(sell);
                RecordMoney(player, state);
            }
        }
        if (player->GetNPCIfCanInteractWith(vendor->GetGUID(), UNIT_NPC_FLAG_REPAIR))
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                uint32 before = player->GetMoney();
                float discount = player->GetReputationPriceDiscount(vendor);
                ObjectGuid itemGuid;
                if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    itemGuid = item->GetGUID();
                sScriptMgr->OnPlayerBeforeDurabilityRepair(player, vendor->GetGUID(), itemGuid, discount, false);
                player->DurabilityRepair(uint16(INVENTORY_SLOT_BAG_0) << 8 | slot, true, discount, false,
                    Spendable(before, v[GoldReserve], v[VisitBudget], state.spent));
                state.spent += before - player->GetMoney();
                RecordMoney(player, state);
            }
        if (FreeSlots(player) <= v[FreeBagSlots])
        {
            Stop(player, state, "Bags remain full of protected items. Review Keep/Sell rules.");
            return true;
        }
        if (Durability(player) <= v[RepairPercent])
        {
            Stop(player, state, "Repairs exceed available funds or the spending limit.");
            return true;
        }
        state.townStage = 2;
        Store(player, state, true);
        return true;
    }
    if (state.townStage == 2)
    {
        Explain(player, state, "Service", "Restocking food within the gold reserve and visit budget.");
        if (!v[FoodStock] || !state.foodService.item)
        {
            if (!state.classSupplies.empty())
                state.townStage = 4;
            else
                FinishTown(player, state);
            return true;
        }
        if (Walk(player, state, state.foodService.position) != WalkResult::Arrived)
            return true;
        Creature* vendor = player->FindNearestCreature(state.foodService.entry, INTERACTION_DISTANCE);
        if (!vendor || !player->GetNPCIfCanInteractWith(vendor->GetGUID(), UNIT_NPC_FLAG_VENDOR))
        {
            Stop(player, state, "The selected food vendor is unavailable or cannot be reached.");
            return true;
        }
        VendorItemData const* stock = vendor->GetVendorItems();
        bool listed = false;
        if (stock)
            for (uint32 slot = 0; slot < stock->GetItemCount(); ++slot)
            {
                VendorItem const* offer = stock->GetItem(slot);
                if (!offer || offer->item != state.foodService.item)
                    continue;
                listed = true;
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(offer->item);
                if (!proto || !proto->BuyCount || offer->ExtendedCost)
                    break;
                uint32 price = static_cast<uint32>(std::floor(proto->BuyPrice * player->GetReputationPriceDiscount(vendor)));
                while (player->GetItemCount(offer->item) < v[FoodStock] &&
                    price <= Spendable(player->GetMoney(), v[GoldReserve], v[VisitBudget], state.spent))
                {
                    uint32 before = player->GetMoney();
                    uint32 count = player->GetItemCount(offer->item);
                    player->BuyItemFromVendorSlot(vendor->GetGUID(), slot, offer->item, 1, NULL_BAG, NULL_SLOT);
                    state.spent += before - player->GetMoney();
                    RecordMoney(player, state);
                    if (player->GetItemCount(offer->item) == count)
                        break;
                }
            }
        if (!listed)
        {
            Stop(player, state, "The configured food is not sold by this vendor.");
            return true;
        }
        Store(player, state, true);
        if (!state.classSupplies.empty())
            state.townStage = 4;
        else
            FinishTown(player, state);
        return true;
    }
    if (state.townStage == 4)
    {
        if (state.classSupplies.empty() || !Spendable(player->GetMoney(), v[GoldReserve], v[VisitBudget], state.spent))
        {
            FinishTown(player, state);
            return true;
        }
        auto request = state.classSupplies.front();
        Explain(player, state, "Service", "Restocking class supplies at " + request.vendor.name + ".");
        if (Walk(player, state, request.vendor.position) != WalkResult::Arrived)
            return true;
        if (BuySupply(player, state.model.profile, request, state.spent))
            state.supplyFailures = 0;
        else if (++state.supplyFailures >= 3)
        {
            Stop(player, state, "Class supplies could not be purchased after three attempts. Review stock and budget.");
            return true;
        }
        RecordMoney(player, state);
        state.classSupplies.pop_front();
        if (uint32 ammo = CarriedAmmoEntry(player))
            player->SetAmmo(ammo);
        return true;
    }
    if (state.townStage == 5)
    {
        Explain(player, state, "Training", "Visiting an eligible trainer for affordable class, weapon, or riding training.");
        if (Walk(player, state, state.trainingLocation) != WalkResult::Arrived)
            return true;
        auto trained = TrainNearby(player, state.model.profile, state.spent);
        RecordMoney(player, state);
        if (!trained.empty())
        {
            state.trainingFailures = 0;
            Explain(player, state, "Training", trained);
        }
        else if (!FindTraining(player, state.model.profile, state.spent, state.trainingLocation))
            FinishTown(player, state);
        else if (++state.trainingFailures >= 3)
            Stop(player, state, "The selected trainer did not teach an eligible ability after three attempts.");
        return true;
    }
    return false;
}

void Recover(Player* player, Runtime& state)
{
    PlayerbotsExecutor::StopMovement(player);
    player->AttackStop();
    state.recovering = true;
    PlayerbotsExecutor::StopPet(player);
    bool usesMana = player->GetMaxPower(POWER_MANA) != 0;
    Explain(player, state, "Recover", "Resting to recover health and class resources.");
    if (RecoveryComplete(player->GetHealthPct(), player->GetPowerPct(POWER_MANA), usesMana,
        state.model.profile.values[ResumeHealth]))
    {
        state.recovering = false;
        player->SetStandState(UNIT_STAND_STATE_STAND);
        return;
    }
    bool mana = usesMana && player->GetPowerPct(POWER_MANA) < 80.0f;
    if (player->HasAuraType(mana ? SPELL_AURA_MOD_POWER_REGEN : SPELL_AURA_MOD_REGEN) ||
        player->IsNonMeleeSpellCast(false))
        return;
    if (player->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
        player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
    uint32 foodEntry = mana ? CarriedDrinkEntry(player) : CarriedFoodEntry(player, state.model.profile);
    for (Item* item : Bags(player))
        if (item->GetEntry() == foodEntry)
        {
            if (!PlayerbotsExecutor::UseFood(player, item))
                player->SetStandState(UNIT_STAND_STATE_SIT);
            return;
        }
    player->SetStandState(UNIT_STAND_STATE_SIT);
}

std::list<Creature*> NearbyCreatures(Player* player, float radius)
{
    std::list<Creature*> creatures;
    auto check = [player, radius](Creature* creature)
    {
        return creature->IsAlive() && player->IsWithinDistInMap(creature, radius);
    };
    Acore::CreatureListSearcher<decltype(check)> searcher(player, creatures, check);
    Cell::VisitObjects(player, searcher, radius);
    creatures.sort([player](Creature* left, Creature* right)
    {
        float leftDistance = player->GetDistance(left);
        float rightDistance = player->GetDistance(right);
        return leftDistance == rightDistance ? left->GetGUID() < right->GetGUID() : leftDistance < rightDistance;
    });
    return creatures;
}

bool EligibleTarget(Player* player, Creature* candidate, Profile const& profile, bool quest = false)
{
    return candidate && candidate->IsAlive() && !candidate->IsInCombat() && !candidate->GetLootRecipientGUID() &&
        !candidate->IsPet() && !candidate->IsSummon() && !candidate->GetCharmerOrOwnerGUID() &&
        !candidate->IsCritter() && !candidate->GetNpcFlags() && !candidate->GetCreatureTemplate()->rank &&
        candidate->GetCreatureTemplate()->ModExperience > 0.0f && player->IsValidAttackTarget(candidate) &&
        EnemyLevelAllowed(player->GetLevel(), candidate->GetLevel(),
            profile.values[MinEnemyLevel], profile.values[MaxEnemyLevel], quest) &&
        FinitePosition(candidate->GetPositionX(), candidate->GetPositionY(), candidate->GetPositionZ()) &&
        player->CanSeeOrDetect(candidate);
}

bool EligibleHunt(Player* player, CreatureData const& spawn, Profile const& profile)
{
    auto creature = sObjectMgr->GetCreatureTemplate(spawn.id);
    auto faction = creature ? sFactionTemplateStore.LookupEntry(creature->faction) : nullptr;
    return creature && faction && (spawn.phaseMask & player->GetPhaseMask()) && !creature->rank &&
        !creature->npcflag && creature->type != CREATURE_TYPE_CRITTER && creature->ModExperience > 0.0f &&
        !(creature->unit_flags & (UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PC)) &&
        !faction->IsFriendlyTo(*player->GetFactionTemplateEntry()) &&
        EnemyLevelRangeAllowed(player->GetLevel(), creature->minlevel, creature->maxlevel,
            profile.values[MinEnemyLevel], profile.values[MaxEnemyLevel]);
}

bool SeekHuntingArea(Player* player, Runtime& state)
{
    if (state.model.goals.empty() || state.model.goals.front().kind != 0)
        return false;
    if (state.huntMap != player->GetMapId())
    {
        state.huntSpawn = 0;
        state.unavailableHunts.clear();
        state.huntMap = player->GetMapId();
    }
    auto spawn = sObjectMgr->GetCreatureData(state.huntSpawn);
    if (!spawn || !EligibleHunt(player, *spawn, state.model.profile))
    {
        state.huntSpawn = 0;
        float best = std::numeric_limits<float>::max();
        for (auto const& candidate : MapCreatures(player->GetMapId()))
        {
            if (!EligibleHunt(player, candidate, state.model.profile) ||
                std::find(state.unavailableHunts.begin(), state.unavailableHunts.end(), candidate.spawnId) !=
                    state.unavailableHunts.end())
                continue;
            float distance = player->GetExactDistSq(candidate.posX, candidate.posY, candidate.posZ);
            if (distance < best)
            {
                best = distance;
                state.huntSpawn = candidate.spawnId;
            }
        }
        spawn = sObjectMgr->GetCreatureData(state.huntSpawn);
    }
    if (!spawn)
        return false;
    Position destination(spawn->posX, spawn->posY, spawn->posZ, spawn->orientation);
    if (player->GetDistance(destination) < 30.0f)
    {
        state.unavailableHunts.push_back(state.huntSpawn);
        state.huntSpawn = 0;
        PlayerbotsExecutor::StopMovement(player);
        return true;
    }
    Explain(player, state, "Travel", "Seeking level-appropriate enemies at a location from world data.");
    if (Walk(player, state, destination, 20.0f, false) == WalkResult::Blocked)
    {
        state.unavailableHunts.push_back(state.huntSpawn);
        state.huntSpawn = 0;
    }
    return true;
}

void Search(Player* player, Runtime& state)
{
    auto const& profile = state.model.profile;
    float pullRadius = float(profile.values[SearchRadius]);
    float discoveryRadius = pullRadius * 3.0f;
    Creature* target = state.searchTarget ? player->GetMap()->GetCreature(state.searchTarget) : nullptr;
    if (!EligibleTarget(player, target, profile) || !player->IsWithinDistInMap(target, discoveryRadius))
    {
        if (state.searchTarget)
            PlayerbotsExecutor::StopMovement(player);
        state.searchTarget.Clear();
        target = nullptr;
    }
    if (!target)
    {
        if (!state.searchReady)
        {
            if (state.huntSpawn)
                SeekHuntingArea(player, state);
            return;
        }
        state.searchReady = false;
        state.events.RescheduleEvent(SearchEvent, Milliseconds(2000));
        uint32 attempts = 0;
        for (Creature* candidate : NearbyCreatures(player, discoveryRadius))
        {
            if (!EligibleTarget(player, candidate, profile) ||
                std::find(state.unreachableTargets.begin(), state.unreachableTargets.end(), candidate->GetGUID()) !=
                    state.unreachableTargets.end())
                continue;
            if (++attempts > 8)
                break;
            if ((!player->IsWithinMeleeRange(candidate) || !player->IsWithinLOSInMap(candidate)) &&
                !PlayerbotsExecutor::CanWalk(player, candidate->GetPosition()))
            {
                state.unreachableTargets.push_back(candidate->GetGUID());
                continue;
            }
            target = candidate;
            state.searchTarget = target->GetGUID();
            break;
        }
    }
    if (!target)
    {
        if (SeekHuntingArea(player, state))
            return;
        PlayerbotsExecutor::StopMovement(player);
        Explain(player, state, "Search",
            "Waiting for a reachable, unclaimed enemy within the configured level limits.");
        return;
    }
    if (player->IsWithinDistInMap(target, pullRadius) && player->IsWithinLOSInMap(target))
    {
        PlayerbotsExecutor::StopMovement(player);
        state.target = target->GetGUID();
        state.searchTarget.Clear();
        return;
    }
    Explain(player, state, "Search", "Walking toward " + target->GetName() + " before evaluating an engagement.");
    if (Walk(player, state, target->GetPosition(), 2.0f, false) == WalkResult::Blocked)
    {
        state.unreachableTargets.push_back(target->GetGUID());
        state.searchTarget.Clear();
        Explain(player, state, "Search", "The enemy's approach is blocked; looking for another reachable enemy.");
    }
}

void Retreat(Player* player, Runtime& state)
{
    Creature* guard = state.retreatGuard ? player->GetMap()->GetCreature(state.retreatGuard) : nullptr;
    if (!guard || !guard->IsAlive() || !guard->IsGuard() || !player->IsFriendlyTo(guard) ||
        !player->IsWithinDistInMap(guard, 120.0f))
    {
        guard = nullptr;
        state.retreatGuard.Clear();
        for (Creature* candidate : NearbyCreatures(player, 120.0f))
            if (candidate->IsGuard() && player->IsFriendlyTo(candidate) &&
                (player->GetDistance(candidate) <= 3.0f ||
                    PlayerbotsExecutor::CanWalk(player, candidate->GetPosition())))
            {
                guard = candidate;
                state.retreatGuard = guard->GetGUID();
                break;
            }
    }
    player->AttackStop();
    state.searchTarget.Clear();
    if (!guard)
    {
        Stop(player, state, "Health is below the retreat threshold, but no nearby friendly guard has a complete path.");
        return;
    }
    Explain(player, state, "Retreat", "Health is below the retreat threshold; moving toward " + guard->GetName() + ".");
    Walk(player, state, guard->GetPosition(), 3.0f);
}

void Think(Player* player, Runtime& state)
{
    state.questWorking = false;
    if (!state.running || !state.ready)
        return;
    if (!PlayerbotsExecutor::Attached(player))
    {
        Stop(player, state, "Playerbots execution is unavailable. Automation remains paused.");
        return;
    }
    if (!Supported(player))
    {
        Stop(player, state, "Automation requires a supported solo Wrath character, level 1-80.");
        return;
    }
    if (player->IsBeingTeleported() || !player->IsInWorld())
        return;
    if (!player->IsInCombat() && player->IsNonMeleeSpellCast(false))
        return;
    if (!player->IsAlive())
    {
        if (!state.wasDead)
        {
            state.wasDead = true;
            ++state.deathCount;
            time_t now = GameTime::GetGameTime().count();
            state.deaths.push_back(now);
            while (!state.deaths.empty() && now - state.deaths.front() > 600)
                state.deaths.pop_front();
            if (state.deaths.size() >= 3)
            {
                Stop(player, state, "Three deaths within ten minutes. Review the combat and target rules.");
                return;
            }
        }
        Explain(player, state, "Corpse recovery", "Walking back to the corpse; normal reclaim delay applies.");
        if (!player->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        {
            WorldPacket release(CMSG_REPOP_REQUEST);
            release << uint8(0);
            player->GetSession()->HandleRepopRequestOpcode(release);
            return;
        }
        if (Corpse* corpse = player->GetCorpse())
        {
            if (Walk(player, state, corpse->GetPosition(), 20.0f) == WalkResult::Arrived)
            {
                WorldPacket reclaim(CMSG_RECLAIM_CORPSE);
                reclaim << corpse->GetGUID();
                player->GetSession()->HandleReclaimCorpseOpcode(reclaim);
            }
        }
        else
            Stop(player, state, "No recoverable corpse. Spirit resurrection requires manual intervention.");
        return;
    }
    state.wasDead = false;
    auto const& v = state.model.profile.values;
    Creature* target = state.target ? player->GetMap()->GetCreature(state.target) : nullptr;
    if (target && !target->IsAlive())
    {
        state.lootTarget = target->GetGUID();
        state.target.Clear();
        target = nullptr;
        player->AttackStop();
    }
    bool questCombat = !state.model.goals.empty() && (state.model.goals.front().kind == 2 ||
        (state.model.goals.front().kind == 0 && state.automaticQuest));
    if (target && !player->IsInCombat() && !EligibleTarget(player, target, state.model.profile, questCombat))
    {
        state.target.Clear();
        target = nullptr;
        player->AttackStop();
        PlayerbotsExecutor::StopMovement(player);
    }
    // Defend against creatures already attacking us; never deliberately engage another player.
    if (!target && player->IsInCombat())
        for (Unit* attacker : player->getAttackers())
            if (Creature* creature = attacker->ToCreature())
            {
                target = creature;
                state.target = creature->GetGUID();
                break;
            }
    if (target && !player->IsInCombat() && NeedsRecovery(player->GetHealthPct(),
        player->GetPowerPct(POWER_MANA), player->GetMaxPower(POWER_MANA) != 0, v[RecoverHealth]))
    {
        state.target.Clear();
        PlayerbotsExecutor::StopPet(player);
        Recover(player, state);
        return;
    }
    if (target && target->IsAlive())
    {
        if (!player->IsValidAttackTarget(target) ||
            (target->GetLootRecipientGUID() && target->GetLootRecipientGUID() != player->GetGUID()))
        {
            state.target.Clear();
            player->AttackStop();
            return;
        }
        if (player->GetHealthPct() <= v[RetreatHealth])
        {
            Retreat(player, state);
            if (!player->IsInCombat())
            {
                state.target.Clear();
                state.recovering = true;
            }
            return;
        }
        auto preset = PresetForClass(player->getClass());
        Explain(player, state, "Combat", "Fighting " + target->GetName() + " with " +
            (state.model.profile.classPreset && preset ? std::string(preset->name) : "custom ability rules") + ".");
        float range = PlayerbotsExecutor::CombatRange(player);
        if ((range > 5.0f ? !player->IsWithinDistInMap(target, range) : !player->IsWithinMeleeRange(target)) ||
            !player->IsWithinLOSInMap(target))
        {
            bool inCombat = player->IsInCombat();
            float tolerance = player->IsWithinLOSInMap(target) ? range : 2.0f;
            if (Walk(player, state, target->GetPosition(), tolerance, inCombat) == WalkResult::Blocked && !inCombat)
            {
                state.unreachableTargets.push_back(target->GetGUID());
                state.target.Clear();
                player->AttackStop();
                Explain(player, state, "Search",
                    "The enemy's approach is blocked; looking for another reachable enemy.");
            }
            return;
        }
        PlayerbotsExecutor::StopMovement(player);
        player->SetStandState(UNIT_STAND_STATE_STAND);
        player->SetFacingToObject(target);
        player->SetSelection(target->GetGUID());
        if (!PlayerbotsExecutor::Attack(player, target) && !PlayerbotsExecutor::Attached(player))
        {
            Stop(player, state, "Combat control lost its state. Resume to restart it.");
            return;
        }
        if (player->IsNonMeleeSpellCast(false))
            return;
        for (auto const& rule : state.model.profile.rules)
        {
            uint32 spell = PlayerbotsExecutor::KnownRank(player, rule.spell);
            if (!spell || player->GetHealthPct() > rule.maxHealth ||
                player->GetPowerPct(player->getPowerType() == POWER_RUNE ? POWER_RUNIC_POWER :
                    player->getPowerType()) < rule.minPower)
                continue;
            Unit* recipient = rule.self ? player : static_cast<Unit*>(target);
            if (rule.missingAura && recipient->HasAura(spell))
                continue;
            if (PlayerbotsExecutor::Cast(player, spell, recipient))
                return;
        }
        if (state.model.profile.classPreset && state.classReady)
        {
            state.classReady = false;
            state.events.ScheduleEvent(ClassActionEvent, Milliseconds(500));
            PlayerbotsExecutor::ClassAction(player, target, true);
        }
        return;
    }
    state.retreatGuard.Clear();
    if (player->IsInCombat())
    {
        Explain(player, state, "Wait", "In combat without a supported creature target.");
        return;
    }
    if (state.lootTarget)
    {
        Creature* corpse = player->GetMap()->GetCreature(state.lootTarget);
        if (corpse && !corpse->IsAlive() && corpse->GetLootRecipientGUID() == player->GetGUID())
        {
            Explain(player, state, "Loot", "Collecting permitted loot from the defeated enemy.");
            if (Walk(player, state, corpse->GetPosition(), 3.0f) != WalkResult::Arrived)
                return;
            WorldPacket open(CMSG_LOOT);
            open << corpse->GetGUID();
            player->GetSession()->HandleLootOpcode(open);
            WorldPacket money(CMSG_LOOT_MONEY);
            player->GetSession()->HandleLootMoneyOpcode(money);
            RecordMoney(player, state);
            for (uint8 i = 0; i < MAX_NR_LOOT_ITEMS; ++i)
            {
                WorldPacket take(CMSG_AUTOSTORE_LOOT_ITEM);
                take << i;
                if (corpse->loot.LootItemInSlot(i, player))
                {
                    player->GetSession()->HandleAutostoreLootItemOpcode(take);
                    if (!corpse->loot.LootItemInSlot(i, player))
                        ++state.lootCount;
                }
            }
            WorldPacket close(CMSG_LOOT_RELEASE);
            close << corpse->GetGUID();
            player->GetSession()->HandleLootReleaseOpcode(close);
        }
        state.lootTarget.Clear();
    }
    if (state.recovering || NeedsRecovery(player->GetHealthPct(), player->GetPowerPct(POWER_MANA),
        player->GetMaxPower(POWER_MANA) != 0, v[RecoverHealth]))
    {
        Recover(player, state);
        return;
    }
    if (state.upkeepReady)
    {
        state.upkeepReady = false;
        auto talents = DevelopTalents(player, state.model.profile);
        if (talents.reason != state.classDecision)
        {
            state.classDecision = talents.changed ? "" : talents.reason;
            if (!talents.reason.empty())
                Explain(player, state, talents.changed ? "Development" : "Decision", talents.reason);
        }
        if (talents.changed)
        {
            Store(player, state, true);
            return;
        }
        auto classWork = MaintainClass(player, state.model.profile);
        if (!classWork.empty())
        {
            Explain(player, state, "Upkeep", classWork);
            return;
        }
        if (state.model.profile.classPreset && PlayerbotsExecutor::ClassAction(player, nullptr, false))
        {
            Explain(player, state, "Upkeep", "Maintaining learned class buffs, forms, and summons.");
            return;
        }
        uint32 foodEntry = CarriedFoodEntry(player, state.model.profile);
        if (!foodEntry && v[FoodStock] && Spendable(player->GetMoney(), v[GoldReserve], v[VisitBudget], 0))
        {
            ServiceDestination offer;
            if (FindService(player, UNIT_NPC_FLAG_VENDOR, 0, offer, SupplyKind::Food))
                foodEntry = offer.item;
        }
        auto food = sObjectMgr->GetItemTemplate(foodEntry);
        state.foodNeeded = food && v[FoodStock] &&
            player->GetItemCount(foodEntry) < std::max(1u, v[FoodStock] / 3) &&
            food->BuyPrice <= Spendable(player->GetMoney(), v[GoldReserve], v[VisitBudget], 0);
        if (state.suppliesReady && !state.townStage)
        {
            state.suppliesReady = false;
            state.events.ScheduleEvent(SuppliesEvent, Milliseconds(60000));
            auto supplies = PlanClassSupplies(player, state.model.profile);
            state.classSupplies.assign(supplies.begin(), supplies.end());
        }
        if (uint32 ammo = CarriedAmmoEntry(player))
            player->SetAmmo(ammo);
        std::string upkeep = MaintainEquipment(player, state.model.profile);
        if (upkeep.empty() && (state.model.goals.empty() || state.model.goals.front().kind != 4))
            upkeep = TrainNearby(player, state.model.profile, state.spent);
        if (!upkeep.empty())
        {
            RecordMoney(player, state);
            Explain(player, state, "Upkeep", upkeep);
            return;
        }
        if (state.model.profile.autoTrain && state.trainingReady && !state.townStage &&
            !state.model.goals.empty() && state.model.goals.front().kind == 0)
        {
            state.trainingReady = false;
            state.events.ScheduleEvent(TrainingEvent, Milliseconds(60000));
            if (FindTraining(player, state.model.profile, 0, state.trainingLocation))
            {
                state.goalLocation = player->GetPosition();
                state.townMap = player->GetMapId();
                state.manualTown = false;
                state.spent = 0;
                state.trainingFailures = 0;
                state.townStage = 5;
            }
        }
    }
    if (Services(player, state))
        return;
    if (state.model.goals.empty())
    {
        Stop(player, state, "All queued goals are complete.");
        return;
    }
    auto const goal = state.model.goals.front();
    ServiceDestination travel;
    bool validTravel = goal.kind == 5 && ResolveDestination(player, goal.target, travel);
    bool complete = goal.kind == 0 ? player->GetLevel() >= goal.target :
        (goal.kind == 1 ? player->GetItemCount(goal.target) >= goal.count :
            (goal.kind == 2 ? player->GetQuestRewardStatus(goal.target) :
                (goal.kind == 4 ? state.completedServiceGoal == goal.id :
                    validTravel && player->GetDistance(travel.position) <= 5.0f)));
    if (complete)
    {
        state.model.goals.erase(state.model.goals.begin());
        ++state.model.revision;
        Explain(player, state, "Goal complete", "The target was reached; advancing the queue.");
        Store(player, state, true);
        Snapshot(player, state);
        return;
    }
    if (goal.limitSeconds && goal.elapsedSeconds >= goal.limitSeconds)
    {
        Stop(player, state, "Goal time limit reached. Cancel it or revise the queue.");
        return;
    }
    if (FreeSlots(player) <= v[FreeBagSlots] || Durability(player) <= v[RepairPercent] || state.foodNeeded ||
        !state.classSupplies.empty())
    {
        BeginTown(player, state, false);
        return;
    }
    if (goal.kind == 4)
    {
        if (state.serviceGoal != goal.id)
        {
            state.serviceGoal = goal.id;
            state.spent = 0;
        }
        Position destination;
        if (!FindTraining(player, state.model.profile, state.spent, destination))
        {
            state.completedServiceGoal = goal.id;
            Explain(player, state, "Training", "No more eligible class abilities fit the reserve and visit budget.");
            return;
        }
        Explain(player, state, "Training", "Traveling to the nearest eligible class trainer.");
        if (Walk(player, state, destination) == WalkResult::Arrived)
        {
            std::string trained = TrainNearby(player, state.model.profile, state.spent, true);
            if (!trained.empty())
                Explain(player, state, "Training", trained);
            else
                Stop(player, state, "The class trainer is unavailable or cannot teach the planned ability.");
            RecordMoney(player, state);
        }
        return;
    }
    if (goal.kind == 3 || goal.kind == 5)
    {
        if (!validTravel)
            Stop(player, state, "The destination is unavailable on this map. Replace this goal from the travel catalog.");
        else
        {
            Explain(player, state, "Travel", "Traveling to " + travel.name + ".");
            Walk(player, state, travel.position, 5.0f);
        }
        return;
    }
    if (goal.kind == 0 && state.searchReady)
    {
        auto avoided = state.model.profile.avoidedQuests;
        avoided.insert(avoided.end(), state.unavailableQuests.begin(), state.unavailableQuests.end());
        if (uint32 pickup = ChooseQuestPickup(player, avoided))
            state.automaticQuest = pickup;
        else if (!state.automaticQuest || player->GetQuestStatus(state.automaticQuest) == QUEST_STATUS_COMPLETE ||
            player->GetQuestRewardStatus(state.automaticQuest))
        {
            uint32 previous = state.automaticQuest;
            state.automaticQuest = ChooseQuest(player, avoided, previous);
            if (previous && previous != state.automaticQuest && player->GetQuestStatus(previous) == QUEST_STATUS_COMPLETE)
                LOG_INFO("module.wrathidle", "{}: quest batching: completed={} next={}",
                    player->GetName(), previous, state.automaticQuest);
        }
    }
    if (goal.kind == 2 || (goal.kind == 0 && state.automaticQuest))
    {
        // General hunting destinations must not survive a switch back to accepted quest work.
        if (state.searchTarget || state.huntSpawn)
            PlayerbotsExecutor::StopMovement(player);
        state.searchTarget.Clear();
        state.huntSpawn = 0;
        state.questWorking = true;
        if (!state.searchReady)
            return;
        state.searchReady = false;
        state.events.ScheduleEvent(SearchEvent, Milliseconds(2000));
        uint32 questId = goal.kind == 2 ? goal.target : state.automaticQuest;
        if (goal.kind == 2 && player->GetQuestStatus(questId) != QUEST_STATUS_NONE)
        {
            std::vector<uint32_t> queued;
            for (auto const& pending : state.model.goals)
            {
                if (pending.kind != 2)
                    break;
                queued.push_back(pending.target);
            }
            auto avoided = state.model.profile.avoidedQuests;
            avoided.insert(avoided.end(), state.unavailableQuests.begin(), state.unavailableQuests.end());
            if (uint32 pickup = ChooseQuestPickup(player, avoided, &queued))
                questId = pickup;
            else if (player->GetQuestStatus(goal.target) == QUEST_STATUS_COMPLETE)
                if (uint32 nearby = ChooseNearbyQuest(player, avoided, &queued, goal.target))
                    questId = nearby;
        }
        bool accepting = player->GetQuestStatus(questId) == QUEST_STATUS_NONE;
        state.questAttempt.Observe(questId, QuestProgress(player, questId));
        QuestIntent intent;
        if (state.questAttempt.Failed())
            intent.reason = state.questAttempt.failures >= 3 ?
                "Quest interaction did not advance after three attempts. Review the journal." :
                "Quest made no progress during ten minutes of active work. Review the objective.";
        else
            intent = PlanQuest(player, questId, true);
        state.lootCount += intent.lootCount;
        if (goal.kind == 2 && questId != goal.target)
            intent.reason = "Combining nearby queued quest work. " + intent.reason;
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            intent.reason = quest->GetTitle() + ": " + intent.reason;
        Explain(player, state, "Quest", intent.reason);
        if (intent.step == QuestStep::Blocked)
        {
            if (goal.kind == 2)
                Stop(player, state, intent.reason);
            else
            {
                state.unavailableQuests.push_back(state.automaticQuest);
                state.automaticQuest = 0;
                state.questAttempt = {};
                state.questRetrySeconds = 0;
            }
            state.questWorking = false;
            return;
        }
        if (intent.step == QuestStep::Interacted || intent.step == QuestStep::FailedInteraction)
        {
            if (intent.step == QuestStep::Interacted)
            {
                state.questAttempt.Observe(questId, QuestProgress(player, questId));
                if (goal.kind == 0 && accepting)
                    state.automaticQuest = 0;
            }
            else
                ++state.questAttempt.failures;
            return;
        }
        if (intent.step == QuestStep::Travel || intent.step == QuestStep::UseObject || intent.step == QuestStep::Gather)
        {
            auto walking = Walk(player, state, intent.destination, intent.tolerance, goal.kind == 2);
            if (walking == WalkResult::Blocked && goal.kind == 0)
                ++state.questAttempt.failures;
            if (walking == WalkResult::Arrived &&
                (intent.step == QuestStep::UseObject || intent.step == QuestStep::Gather))
            {
                ++state.questAttempt.failures;
                LOG_WARN("module.wrathidle",
                    "{}: quest={} object={} spawn={} reached approach without interaction: "
                    "from=({},{},{}) destination=({},{},{})",
                    player->GetName(), questId, intent.creature, intent.spawn, player->GetPositionX(),
                    player->GetPositionY(), player->GetPositionZ(), intent.destination.GetPositionX(),
                    intent.destination.GetPositionY(), intent.destination.GetPositionZ());
            }
        }
        else if (intent.step == QuestStep::Fight)
        {
            Creature* candidate = nullptr;
            for (Creature* creature : NearbyCreatures(player, 180.0f))
                if (creature->GetEntry() == intent.creature &&
                    EligibleTarget(player, creature, state.model.profile, true) &&
                    std::find(state.unreachableTargets.begin(), state.unreachableTargets.end(), creature->GetGUID()) ==
                        state.unreachableTargets.end())
                {
                    candidate = creature;
                    break;
                }
            if (candidate)
                state.target = candidate->GetGUID();
            else if (!player->IsWithinDist2d(intent.destination.GetPositionX(),
                intent.destination.GetPositionY(), 15.0f))
            {
                if (Walk(player, state, intent.destination, 3.0f, goal.kind == 2) == WalkResult::Blocked && goal.kind == 0)
                    ++state.questAttempt.failures;
            }
            else
                Explain(player, state, "Quest", "Waiting near the objective for an eligible creature to respawn.");
        }
        return;
    }
    if (goal.kind == 0 && state.searchReady)
    {
        std::string reason;
        if (!ExplainQuestHunting(player, state.model.profile.avoidedQuests, state.unavailableQuests, reason))
        {
            Stop(player, state, reason);
            return;
        }
        Explain(player, state, "Leveling", reason);
    }
    Search(player, state);
}

void Command(Player* player, Runtime& state, std::string const& verb, std::vector<uint32> const& args)
{
    if (verb == "hello")
    {
        state.ready = args.size() == 1 && args[0] == Version && state.loaded;
        if (!state.ready)
        {
            Stop(player, state, "Protocol negotiation failed. Automation remains paused.");
            Send(player, "error", {}, {"Unsupported protocol or saved profile. Automation remains paused."});
            return;
        }
        Send(player, "hello", {Version});
        Send(player, "capabilities", {2,1,1,1,0});
        Snapshot(player, state);
        return;
    }
    if (!state.ready || !state.loaded)
        return;
    if (verb == "get")
    {
        Snapshot(player, state);
        return;
    }
    if (verb == "destinations" && args.size() == 1)
    {
        auto catalog = ListDestinations(player);
        constexpr size_t PageSize = 8;
        size_t pages = std::max(size_t(1), (catalog.size() + PageSize - 1) / PageSize);
        if (args[0] >= pages)
        {
            Send(player, "error", {}, {"Destination page is no longer available. Refresh the list."});
            return;
        }
        Send(player, "travel_begin", {state.model.revision, args[0], uint32(pages)});
        for (size_t i = args[0] * PageSize; i < std::min(catalog.size(), (args[0] + 1) * PageSize); ++i)
        {
            auto name = catalog[i].name;
            Send(player, "travel_item", {catalog[i].id}, {name});
        }
        Send(player, "travel_end", {state.model.revision, args[0]});
        return;
    }
    if (verb == "upgrades" && args.size() == 1)
    {
        auto items = Bags(player);
        items.erase(std::remove_if(items.begin(), items.end(), [player](Item* item)
        {
            return item->GetTemplate()->Class == ITEM_CLASS_CONTAINER || !IsPendingUpgrade(player, item);
        }), items.end());
        constexpr size_t PageSize = 8;
        size_t pages = std::max(size_t(1), (items.size() + PageSize - 1) / PageSize);
        if (args[0] >= pages)
        {
            Send(player, "error", {}, {"Upgrade page is no longer available. Refresh the list."});
            return;
        }
        Send(player, "upgrade_begin", {state.model.revision, args[0], uint32(pages)});
        for (size_t i = args[0] * PageSize; i < std::min(items.size(), (args[0] + 1) * PageSize); ++i)
        {
            Item* item = items[i];
            auto proto = item->GetTemplate();
            std::string name = proto->Name1;
            // Item GUID low parts identify this session's owned inventory objects, never another player's items.
            Send(player, "upgrade_item", {item->GetGUID().GetCounter(), item->GetEntry(),
                uint32(proto->Bonding == BIND_WHEN_EQUIPPED && !item->IsSoulBound())}, {name});
        }
        Send(player, "upgrade_end", {state.model.revision, args[0]});
        return;
    }
    if (verb == "quests" && args.size() == 1)
    {
        auto catalog = ListQuests(player);
        constexpr size_t PageSize = 8;
        size_t pages = std::max(size_t(1), (catalog.size() + PageSize - 1) / PageSize);
        if (args[0] >= pages)
        {
            Send(player, "error", {}, {"Quest page is no longer available. Refresh the list."});
            return;
        }
        Send(player, "catalog_begin", {state.model.revision, args[0], uint32(pages)});
        for (size_t i = args[0] * PageSize; i < std::min(catalog.size(), (args[0] + 1) * PageSize); ++i)
        {
            auto const& quest = catalog[i];
            std::string title = quest.title;
            Send(player, "catalog_item", {quest.id, quest.status, uint32(quest.supported)}, {title});
        }
        Send(player, "catalog_end", {state.model.revision, args[0]});
        return;
    }
    if (args.empty() || args[0] != state.model.revision)
    {
        Send(player, "error", {}, {"Settings changed. Review the refreshed profile and retry."});
        Snapshot(player, state);
        return;
    }
    Model candidate = state.model;
    auto findGoal = [&candidate](uint32 id)
    {
        return std::find_if(candidate.goals.begin(), candidate.goals.end(),
            [id](Goal const& goal) { return goal.id == id; });
    };
    bool valid = true;
    if (verb == "start" || verb == "resume" || verb == "town")
    {
        if (args.size() != 1 || !Supported(player) || (candidate.goals.empty() && verb != "town"))
            valid = false;
        else
        {
            if (!PlayerbotsExecutor::Attach(player))
            {
                Send(player, "error", {}, {"Playerbots is unavailable or another AI controls this character."});
                return;
            }
            state.running = true;
            state.pathFailures = 0;
            state.stalledSeconds = 0;
            state.searchReady = true;
            state.unreachableTargets.clear();
            state.unavailableQuests.clear();
            state.questAttempt = {};
            state.questRetrySeconds = 0;
            state.deaths.clear();
            if (verb == "town")
                BeginTown(player, state, true);
            else
                Explain(player, state, "Starting", "Evaluating the first queued goal.");
        }
    }
    else if (verb == "pause" && args.size() == 1)
        Stop(player, state, "Paused by the player.");
    else if (verb == "cancel" && args.size() == 2 && findGoal(args[1]) != candidate.goals.end())
    {
        bool active = findGoal(args[1]) == candidate.goals.begin();
        candidate.goals.erase(findGoal(args[1]));
        if (active)
            Stop(player, state, "Current goal cancelled. Resume to continue the queue.");
    }
    else if (verb == "add" && args.size() == 6 && candidate.goals.size() < MaxGoals)
    {
        Goal goal{args[1], args[2], args[3], args[4], args[5], 0, candidate.nextGoalId};
        ServiceDestination destination;
        valid = Valid(goal) && (goal.kind == 0 || goal.kind == 4 ||
            (goal.kind == 5 && ResolveDestination(player, goal.target, destination)) ||
            (goal.kind == 1 && sObjectMgr->GetItemTemplate(goal.target)) ||
            (goal.kind == 2 && SupportedQuest(sObjectMgr->GetQuestTemplate(goal.target))));
        if (valid && candidate.nextGoalId < UINT32_MAX)
        {
            candidate.goals.push_back(goal);
            ++candidate.nextGoalId;
        }
        else
            valid = false;
    }
    else if (verb == "move" && args.size() == 3 && findGoal(args[1]) != candidate.goals.end() &&
        args[2] < candidate.goals.size())
    {
        auto goal = *findGoal(args[1]);
        candidate.goals.erase(findGoal(args[1]));
        candidate.goals.insert(candidate.goals.begin() + args[2], goal);
        Stop(player, state, "Queue reordered. Resume to follow the new order.");
    }
    else if (verb == "equip" && args.size() == 2)
    {
        Item* selected = nullptr;
        for (Item* item : Bags(player))
            if (item->GetGUID().GetCounter() == args[1])
                selected = item;
        uint16 destination = 0;
        if (!selected || player->IsInCombat() || !player->IsAlive() || !IsPendingUpgrade(player, selected) ||
            player->CanEquipItem(NULL_SLOT, destination, selected, true) != EQUIP_ERR_OK ||
            uint8(destination) >= EQUIPMENT_SLOT_END || (candidate.profile.lockedSlots & (1u << uint8(destination))))
            valid = false;
        else
        {
            auto guid = selected->GetGUID();
            player->SwapItem(uint16(selected->GetBagSlot()) << 8 | selected->GetSlot(), destination);
            Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(destination));
            valid = equipped && equipped->GetGUID() == guid;
        }
    }
    else if (verb == "avoid" && args.size() <= MaxProtected + 1)
    {
        candidate.profile.avoidedQuests.assign(args.begin() + 1, args.end());
        for (uint32 id : candidate.profile.avoidedQuests)
            valid = valid && sObjectMgr->GetQuestTemplate(id);
        if (valid)
        {
            state.automaticQuest = 0;
            Stop(player, state, "Quest preferences changed. Resume to use the updated choices.");
        }
    }
    else if (verb == "policy" && args.size() == 5)
    {
        candidate.profile.autoEquip = args[1];
        candidate.profile.autoTrain = args[2];
        candidate.profile.equipBoe = args[3];
        candidate.profile.lockedSlots = args[4];
    }
    else if (verb == "classpolicy" && args.size() == 3)
    {
        candidate.profile.autoTalents = args[1];
        candidate.profile.classPreset = args[2];
    }
    else if (verb == "profile" && args.size() == SettingCount + 1)
    {
        std::copy(args.begin() + 1, args.end(), candidate.profile.values.begin());
        auto food = sObjectMgr->GetItemTemplate(candidate.profile.values[FoodItem]);
        valid = !candidate.profile.values[FoodItem] ||
            (food && food->Class == ITEM_CLASS_CONSUMABLE && food->SubClass == ITEM_SUBCLASS_FOOD);
    }
    else if (verb == "rule" && args.size() == 7 && args[1] < MaxRules)
    {
        Rule rule{args[2], args[3], args[4], args[5], args[6]};
        valid = !rule.spell || PlayerbotsExecutor::KnownRank(player, rule.spell);
        if (valid)
            candidate.profile.rules[args[1]] = rule;
    }
    else if (verb == "keep" && args.size() <= MaxProtected + 1)
    {
        candidate.profile.protectedItems.assign(args.begin() + 1, args.end());
        for (uint32 item : candidate.profile.protectedItems)
            valid = valid && sObjectMgr->GetItemTemplate(item);
    }
    else
        valid = false;
    if (!valid || !Valid(candidate.profile))
    {
        Send(player, "error", {}, {"Invalid request or unsupported pilot capability."});
        return;
    }
    candidate.revision++;
    state.model = std::move(candidate);
    if (verb == "add" && !state.running)
        Explain(player, state, "Paused", "Goal queued. Start or resume when ready.");
    Store(player, state, true);
    Send(player, "ack", {state.model.revision});
    Snapshot(player, state);
}
}

class IdleWorld final : public WorldScript
{
public:
    IdleWorld() : WorldScript("WrathIdleWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) { }
    void OnAfterConfigLoad(bool reload) override
    {
        if (!reload)
            Enabled = sConfigMgr->GetOption<bool>("WrathIdle.Enable", false) &&
                sConfigMgr->GetOption<bool>("EnablePlayerSettings", false);
    }
};

class IdlePlayer final : public PlayerScript
{
public:
    IdlePlayer() : PlayerScript("WrathIdlePlayer", {PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_BEFORE_LOGOUT,
        PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_SAVE}) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!Enabled || player->GetSession()->IsHeadless())
            return;
        auto* state = player->CustomData.GetDefault<Runtime>(Storage);
        uint32 length = player->GetPlayerSetting(Storage, 0).value;
        char const* source = Storage;
        if (!length)
        {
            source = LegacyStorage;
            length = player->GetPlayerSetting(source, 0).value;
        }
        if (length)
        {
            std::vector<uint32_t> data;
            if (length <= 512)
                for (uint32 i = 1; i <= length; ++i)
                    data.push_back(player->GetPlayerSetting(source, i).value);
            state->loaded = Decode(data, state->model);
            if (state->loaded && source == LegacyStorage)
                Store(player, *state, false);
        }
        state->previousMoney = player->GetMoney();
        state->previousXp = player->GetUInt32Value(PLAYER_XP);
        state->previousLevel = player->GetLevel();
        state->previousNextXp = player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
        state->lastPosition = player->GetPosition();
        state->events.ScheduleEvent(UpkeepEvent, Milliseconds(5000));
        state->events.ScheduleEvent(ThinkEvent, Milliseconds(250));
        state->events.ScheduleEvent(SecondEvent, Milliseconds(1000));
        state->events.ScheduleEvent(SnapshotEvent, Milliseconds(2000));
        state->events.ScheduleEvent(SaveEvent, Milliseconds(30000));
        state->events.ScheduleEvent(RetryPathsEvent, Milliseconds(15000));
        state->events.ScheduleEvent(HuntRetryEvent, Milliseconds(60000));
        player->SetClientControl(player, false);
    }

    void OnPlayerBeforeLogout(Player* player) override
    {
        if (auto* state = State(player))
        {
            Stop(player, *state, "Session ended. Resume after reconnecting.");
            Store(player, *state, false);
        }
    }

    void OnPlayerSave(Player* player) override
    {
        if (auto* state = State(player))
            Store(player, *state, false);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        Runtime* state = State(player);
        if (!state)
            return;
        if (player->GetSession()->IsSocketClosed() || player->GetSession()->IsLoggingOut())
        {
            if (state->running)
                Stop(player, *state, "Disconnected or logging out. Resume after reconnecting.");
            state->ready = false;
            return;
        }
        state->events.Update(diff);
        while (uint32 event = state->events.ExecuteEvent())
        {
            if (event == ThinkEvent)
            {
                Think(player, *state);
                state->events.ScheduleEvent(ThinkEvent, Milliseconds(250));
            }
            else if (event == SecondEvent)
            {
                state->commandCount = 0;
                ++state->seconds;
                if (state->running && !state->model.goals.empty())
                    ++state->model.goals.front().elapsedSeconds;
                if (state->running && state->questWorking)
                    ++state->questAttempt.stalledSeconds;
                if (state->running && !state->unavailableQuests.empty() && ++state->questRetrySeconds >= 600)
                {
                    state->unavailableQuests.clear();
                    state->questRetrySeconds = 0;
                }
                if (!player->movespline->Finalized())
                {
                    ++state->travelSeconds;
                    if (player->GetDistance(state->lastPosition) < 0.2f)
                        ++state->stalledSeconds;
                    else
                    {
                        state->stalledSeconds = 0;
                        state->pathFailures = 0;
                    }
                    if (state->running && state->stalledSeconds >= 20)
                        Stop(player, *state, "Movement stalled for twenty seconds. Review the route.");
                }
                if (state->running && state->townStage)
                    ++state->serviceSeconds;
                state->lastPosition = player->GetPosition();
                RecordMoney(player, *state);
                uint32 xp = player->GetUInt32Value(PLAYER_XP);
                if (player->GetLevel() > state->previousLevel)
                    state->xp += state->previousNextXp - state->previousXp + xp;
                else if (xp >= state->previousXp)
                    state->xp += xp - state->previousXp;
                state->previousXp = xp;
                state->previousLevel = player->GetLevel();
                state->previousNextXp = player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
                state->events.ScheduleEvent(SecondEvent, Milliseconds(1000));
            }
            else if (event == SnapshotEvent)
            {
                if (state->ready)
                    Status(player, *state);
                state->events.ScheduleEvent(SnapshotEvent, Milliseconds(2000));
            }
            else if (event == SaveEvent)
            {
                Store(player, *state, true);
                state->events.ScheduleEvent(SaveEvent, Milliseconds(30000));
            }
            else if (event == UpkeepEvent)
            {
                state->upkeepReady = true;
                state->events.ScheduleEvent(UpkeepEvent, Milliseconds(5000));
            }
            else if (event == ClassActionEvent)
                state->classReady = true;
            else if (event == SuppliesEvent)
                state->suppliesReady = true;
            else if (event == TrainingEvent)
                state->trainingReady = true;
            else if (event == SearchEvent)
                state->searchReady = true;
            else if (event == HuntRetryEvent)
            {
                state->unavailableHunts.clear();
                state->events.ScheduleEvent(HuntRetryEvent, Milliseconds(60000));
            }
            else if (event == RetryPathsEvent)
            {
                state->unreachableTargets.clear();
                state->events.ScheduleEvent(RetryPathsEvent, Milliseconds(15000));
            }
        }
    }
};

class IdlePackets final : public ServerScript
{
public:
    IdlePackets() : ServerScript("WrathIdlePackets", {SERVERHOOK_CAN_PACKET_RECEIVE}) { }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (!Enabled || !session || !session->GetPlayer())
            return true;
        Player* player = session->GetPlayer();
        Runtime* state = State(player);
        if (!state)
            return true;
        if (packet.GetOpcode() == CMSG_WRATHIDLE_COMMAND)
        {
            if (++state->commandCount > 8 || packet.size() < 14)
                return false;
            WorldPacket copy(packet);
            uint16 version, operation, count;
            uint32 request, revision;
            copy >> version >> operation >> request >> revision >> count;
            if (count > WrathIdleWire::MaxValues || copy.size() - copy.rpos() != size_t(count) * 4 ||
                !request || request <= state->lastRequestId ||
                !operation || operation >= std::size(WrathIdleWire::Commands))
                return false;
            state->lastRequestId = request;
            state->requestId = request;
            if (version != WrathIdleWire::Version)
            {
                state->ready = false;
                Stop(player, *state, "Incompatible native game protocol.");
                Send(player, "error", {}, {"Incompatible native game protocol."});
                state->requestId = 0;
                return false;
            }
            std::string verb = WrathIdleWire::Commands[operation];
            std::vector<uint32> args;
            if (operation > 5)
                args.push_back(revision);
            for (uint16 i = 0; i < count; ++i)
            {
                uint32 value;
                copy >> value;
                args.push_back(value);
            }
            Command(player, *state, verb, args);
            state->requestId = 0;
            return false;
        }
        std::string name = opcodeTable[static_cast<OpcodeClient>(packet.GetOpcode())]->Name;
        // Management-only sessions allow observation, social chat, and connection bookkeeping.
        // Gameplay mutations are executed locally by the activity controllers above.
        return packet.GetOpcode() == CMSG_MESSAGECHAT || name.find("QUERY") != std::string::npos ||
            packet.GetOpcode() == MSG_MOVE_WORLDPORT_ACK || packet.GetOpcode() == MSG_MOVE_TELEPORT_ACK ||
            packet.GetOpcode() == CMSG_LOGOUT_REQUEST || packet.GetOpcode() == CMSG_LOGOUT_CANCEL ||
            packet.GetOpcode() == CMSG_PING || packet.GetOpcode() == CMSG_KEEP_ALIVE ||
            packet.GetOpcode() == CMSG_TIME_SYNC_RESP || packet.GetOpcode() == CMSG_SET_ACTIVE_MOVER ||
            packet.GetOpcode() == CMSG_NAME_QUERY || packet.GetOpcode() == CMSG_INSPECT ||
            packet.GetOpcode() == CMSG_WARDEN_DATA || packet.GetOpcode() == CMSG_UPDATE_ACCOUNT_DATA ||
            packet.GetOpcode() == CMSG_REQUEST_ACCOUNT_DATA || packet.GetOpcode() == CMSG_READY_FOR_ACCOUNT_DATA_TIMES;
    }
};
}

void Addmod_wrathidleScripts()
{
    new WrathIdle::IdleWorld();
    new WrathIdle::IdlePlayer();
    new WrathIdle::IdlePackets();
}
