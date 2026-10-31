#include "PlayerbotsExecutor.h"
#include "IdleNavigation.h"
#include "IdleClassPolicy.h"
#include "IdleValuePolicy.h"
#include "IdleTravel.h"
#include "IdleWorld.h"
#include "NavmeshRoute.h"
#include "AttackAction.h"
#include "LastMovementValue.h"
#include "Log.h"
#include "LootObjectStack.h"
#include "Map.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "Playerbots.h"
#include "UseItemAction.h"
#include "GenericSpellActions.h"
#include "Engine.h"
#include "Pet.h"
#include "CreatureAI.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "GameObject.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"

#include <algorithm>
#include <list>

namespace WrathIdle::PlayerbotsExecutor
{
namespace
{
constexpr char ClassStorage[] = "wrathidle.class-execution";

struct ClassExecution : DataMap::Base
{
    uint32 level = 0;
    size_t spells = 0;
    bool listeners = false;
};

void ClearTargets(PlayerbotAI* ai)
{
    auto context = ai->GetAiObjectContext();
    for (auto const& name : context->GetValues())
        if (auto value = context->GetUntypedValue(name))
            ClearBorrowedValues<Value, WorldObject*, Unit*, Creature*, Player*, Pet*, GameObject*, Item*, Aura*,
                std::vector<Item*>, std::vector<Unit*>, std::list<Unit*>>(value);
}

class ClassActionBoundary final : public ActionExecutionListener
{
public:
    explicit ClassActionBoundary(PlayerbotAI* controller) : ai(controller) { }

    bool Before(Action* action, Event) override
    {
        auto cast = dynamic_cast<CastSpellAction*>(action);
        if (!cast)
            return false;
        Player* player = ai->GetBot();
        if (player->getClass() == CLASS_DRUID && cast->getSpell() == "caster form")
            return true;
        uint32 spell = ai->GetAiObjectContext()->GetValue<uint32>("spell id", cast->getSpell())->Get();
        if (!spell || !player->HasSpell(spell))
            return false;
        auto info = sSpellMgr->GetSpellInfo(spell);
        if (!info || info->HasEffect(SPELL_EFFECT_TELEPORT_UNITS))
            return false;
        if (player->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
            (info->HasAttribute(SPELL_ATTR0_ON_NEXT_SWING) || info->HasAttribute(SPELL_ATTR0_ON_NEXT_SWING_NO_DAMAGE)))
            return false;
        Unit* recipient = action->GetTarget();
        Unit* selected = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        return recipient && recipient->IsInWorld() &&
            (recipient == player || recipient == player->GetPet() || recipient == selected);
    }

    bool AllowExecution(Action*, Event) override { return true; }
    void After(Action*, bool, Event) override { }
    bool OverrideResult(Action*, bool executed, Event) override { return executed; }

private:
    PlayerbotAI* ai;
};

PlayerbotAI* ManagedAI(Player* player)
{
    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    return ai && ai->IsExternallyControlled() ? ai : nullptr;
}

class DirectedAction final : public AttackAction
{
public:
    explicit DirectedAction(PlayerbotAI* ai) : AttackAction(ai, "wrathidle directed action") { }

    bool WalkTo(Position const& destination)
    {
        return MoveTo(bot->GetMapId(), destination.GetPositionX(), destination.GetPositionY(),
            destination.GetPositionZ(), false, false, true, true, MovementPriority::MOVEMENT_FORCED);
    }

    bool AttackTarget(Unit* target) { return Attack(target, false); }
};

class FoodAction final : public UseItemAction
{
public:
    explicit FoodAction(PlayerbotAI* ai) : UseItemAction(ai, "wrathidle food", true) { }
    bool Use(Item* item) { return UseItem(item, ObjectGuid::Empty, nullptr, bot); }
};

bool CompletePath(PathGenerator const& path)
{
    return (path.GetPathType() & PATHFIND_NORMAL) &&
        !(path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_SHORTCUT |
            PATHFIND_SHORT | PATHFIND_NOT_USING_PATH | PATHFIND_FARFROMPOLY));
}

bool PlanWalk(Player* player, Position const& destination, Position& step, bool report)
{
    if (!FinitePosition(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()) ||
        !FinitePosition(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()) ||
        !Acore::IsValidMapCoord(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()) ||
        player->GetExactDist2d(destination.GetPositionX(), destination.GetPositionY()) > 2000.0f)
    {
        if (report)
            LOG_WARN("module.wrathidle", "{}: destination outside local route budget: map={} from=({},{},{}) to=({},{},{})",
                player->GetName(), player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
                player->GetPositionZ(), destination.GetPositionX(), destination.GetPositionY(),
                destination.GetPositionZ());
        return false;
    }

    // Bound terrain loading to the requested local corridor; never load an entire continent.
    GridCoord first = Acore::ComputeGridCoord(player->GetPositionX(), player->GetPositionY());
    GridCoord last = Acore::ComputeGridCoord(destination.GetPositionX(), destination.GetPositionY());
    if ((std::max(first.x_coord, last.x_coord) - std::min(first.x_coord, last.x_coord) + 1) *
        (std::max(first.y_coord, last.y_coord) - std::min(first.y_coord, last.y_coord) + 1) > 25)
        return false;
    for (uint32 x = std::min(first.x_coord, last.x_coord); x <= std::max(first.x_coord, last.x_coord); ++x)
        for (uint32 y = std::min(first.y_coord, last.y_coord); y <= std::max(first.y_coord, last.y_coord); ++y)
            player->GetMap()->EnsureGridCreated(GridCoord(x, y));

    dtQueryFilterExt filter;
    // Match the core's normal-player terrain filter.
    filter.setIncludeFlags(NAV_GROUND | NAV_GROUND_STEEP | NAV_WATER | NAV_MAGMA);
    RoutePoint next{};
    RouteDiagnostic diagnostic;
    if (!PlanNavmeshLeg(player->GetMap()->GetMapCollisionData().GetMMapData().GetNavMesh(),
        {player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()},
        {destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()}, filter, next, diagnostic))
    {
        if (report)
            LOG_WARN("module.wrathidle",
                "{}: route rejected: map={} from=({},{},{}) to=({},{},{}) stage={} status={} polygons={} "
                "corners={} surfaceGap={}",
                player->GetName(), player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
                player->GetPositionZ(), destination.GetPositionX(), destination.GetPositionY(),
                destination.GetPositionZ(), diagnostic.stage, diagnostic.status,
                diagnostic.polygons, diagnostic.corners, diagnostic.surfaceGap);
        return false;
    }

    // Validate the executable leg with the same smooth-path settings used by MovePoint.
    // The optional climb check compares against a distant steer corner and rejects ordinary hills.
    PathGenerator leg(player);
    bool calculated = leg.CalculatePath(next.x, next.y, next.z);
    bool bounded = std::all_of(leg.GetPath().begin(), leg.GetPath().end(), [](auto const& point)
    {
        return FinitePosition(point.x, point.y, point.z);
    });
    if (!calculated || !CompletePath(leg) || !bounded || leg.GetPath().size() < 2)
    {
        if (report)
            LOG_WARN("module.wrathidle",
                "{}: walking leg rejected: map={} from=({},{},{}) to=({},{},{}) type={} points={}",
                player->GetName(), player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
                player->GetPositionZ(), next.x, next.y, next.z, uint32(leg.GetPathType()), leg.GetPath().size());
        return false;
    }
    step.Relocate(next.x, next.y, next.z);
    return true;
}
}

bool Available()
{
    return sPlayerbotAIConfig.Enabled;
}

bool Attached(Player* player)
{
    auto ai = ManagedAI(player);
    return ai && ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get();
}

bool Attach(Player* player)
{
    if (!Available())
        return false;
    if (PlayerbotAI* existing = GET_PLAYERBOT_AI(player))
        return existing->IsExternallyControlled();

    sPlayerbotsMgr.AddPlayerbotData(player, true, true);
    PlayerbotAI* ai = ManagedAI(player);
    if (!ai)
        return false;
    ai->SetMaster(player);
    for (uint8 state = 0; state < BOT_STATE_MAX; ++state)
        ai->ClearStrategies(static_cast<BotState>(state));
    player->CustomData.Erase(ClassStorage);
    LOG_INFO("module.wrathidle", "{}: Playerbots managed execution attached", player->GetName());
    return true;
}

void StopMovement(Player* player)
{
    player->GetMotionMaster()->Clear();
    player->StopMoving();
    if (PlayerbotAI* ai = ManagedAI(player))
        ai->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get().clear();
}

void Detach(Player* player)
{
    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    if (ai && !ai->IsExternallyControlled())
        return;
    StopMovement(player);
    player->AttackStop();
    player->InterruptNonMeleeSpells(false);
    StopPet(player);
    player->CustomData.Erase(ClassStorage);
    delete ai; // Playerbots unregisters the context in its destructor.
}

bool CanWalk(Player* player, Position const& destination)
{
    Position step;
    return ManagedAI(player) && PlanWalk(player, destination, step, false);
}

bool Walk(Player* player, Position const& destination)
{
    PlayerbotAI* ai = ManagedAI(player);
    if (!ai || !ai->CanMove())
        return false;

    Position step;
    if (!PlanWalk(player, destination, step, true))
        return false;

    // WrathIdle has already coalesced repeated destinations. A new intent must preempt an old move.
    ai->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get().clear();
    DirectedAction action(ai);
    bool moved = action.WalkTo(step);
    if (!moved)
        LOG_WARN("module.wrathidle", "{}: Playerbots refused validated walking leg to ({},{},{})",
            player->GetName(), step.GetPositionX(), step.GetPositionY(), step.GetPositionZ());
    return moved;
}

bool Attack(Player* player, Unit* target)
{
    PlayerbotAI* ai = ManagedAI(player);
    if (!ai || !target || !target->IsInWorld())
        return false;
    auto loot = ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get();
    if (!loot)
    {
        LOG_ERROR("module.wrathidle", "{}: combat context has no owned loot storage", player->GetName());
        return false;
    }
    DirectedAction action(ai);
    bool result = action.AttackTarget(target);
    // Upstream actions retain target pointers; this controller resolves GUIDs anew each tick.
    ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(nullptr);
    ai->GetAiObjectContext()->GetValue<Unit*>("old target")->Set(nullptr);
    if (auto available = ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get())
        available->Clear();
    else
        return false;
    return result;
}

bool Cast(Player* player, uint32_t spell, Unit* target)
{
    PlayerbotAI* ai = ManagedAI(player);
    spell = KnownRank(player, spell);
    auto info = sSpellMgr->GetSpellInfo(spell);
    if (!info || (player->GetCurrentSpell(CURRENT_MELEE_SPELL) &&
        info->HasAttribute(SPELL_ATTR0_ON_NEXT_SWING)))
        return false;
    return ai && spell && ai->CanCastSpell(spell, target) && ai->CastSpell(spell, target);
}

uint32_t KnownRank(Player* player, uint32_t spell)
{
    if (!spell)
        return 0;
    uint32 first = sSpellMgr->GetFirstSpellInChain(spell);
    uint32 selected = 0;
    uint32 rank = 0;
    for (auto const& [id, learned] : player->GetSpellMap())
    {
        if (learned->State == PLAYERSPELL_REMOVED || !learned->Active || !player->HasSpell(id) ||
            sSpellMgr->GetFirstSpellInChain(id) != first)
            continue;
        auto info = sSpellMgr->GetSpellInfo(id);
        if (info && (!selected || info->GetRank() > rank))
        {
            selected = id;
            rank = info->GetRank();
        }
    }
    return selected;
}

float CombatRange(Player* player)
{
    auto preset = PresetForClass(player->getClass());
    bool ranged = preset && preset->ranged;
    if (player->getClass() == CLASS_HUNTER)
    {
        uint32 ammo = player->GetUInt32Value(PLAYER_AMMO_ID);
        ranged = ammo && player->GetItemCount(ammo);
    }
    if (player->getClass() == CLASS_DRUID)
    {
        auto ai = ManagedAI(player);
        ranged = ai && !KnownSpell(player, "cat form") && !KnownSpell(player, "bear form");
    }
    return ranged ? 24.0f : 2.0f;
}

uint32_t KnownSpell(Player* player, std::string_view name)
{
    auto ai = ManagedAI(player);
    if (!ai)
        return 0;
    uint32 id = ai->GetAiObjectContext()->GetValue<uint32>("spell id", std::string(name))->Get();
    return KnownRank(player, id);
}

bool CastNamed(Player* player, std::string_view name, Unit* target)
{
    return Cast(player, KnownSpell(player, name), target);
}

bool CastOnItem(Player* player, std::string_view name, Item* item)
{
    auto info = sSpellMgr->GetSpellInfo(KnownSpell(player, name));
    if (!info || !item || player->GetItemByGuid(item->GetGUID()) != item || player->IsNonMeleeSpellCast(false))
        return false;
    SpellCastTargets targets;
    targets.SetItemTarget(item);
    auto cast = new Spell(player, info, TRIGGERED_NONE);
    return cast->prepare(&targets) == SPELL_CAST_OK;
}

bool OpenObject(Player* player, GameObject* object)
{
    auto ai = ManagedAI(player);
    if (!ai || !object || !object->isSpawned() ||
        !player->CanSeeOrDetect(object) || !object->IsAtInteractDistance(player) ||
        player->IsNonMeleeSpellCast(false))
        return false;
    // Use the same lock-specific spell selection accepted by the normal client cast handler.
    auto info = object->GetSpellForLock(player);
    if (!info || !info->HasEffect(SPELL_EFFECT_OPEN_LOCK))
    {
        LOG_INFO("module.wrathidle", "{}: container entry={} lock={} has no eligible opening spell",
            player->GetName(), object->GetEntry(), object->GetGOInfo()->GetLockId());
        return false;
    }
    StopMovement(player);
    SpellCastTargets targets;
    targets.SetGOTarget(object);
    // The ordinary cast owns object range/lock checks. A unit-style ray to a ground-level GO origin
    // is not an extra requirement of the native object-opening interaction.
    auto cast = new Spell(player, info, TRIGGERED_NONE);
    SpellCastResult result = cast->prepare(&targets);
    LOG_INFO("module.wrathidle", "{}: container entry={} lock={} opening-spell={} result={}",
        player->GetName(), object->GetEntry(), object->GetGOInfo()->GetLockId(), info->Id, uint32(result));
    return result == SPELL_CAST_OK;
}

void StopPet(Player* player)
{
    if (Pet* pet = player->GetPet())
    {
        pet->SetReactState(REACT_PASSIVE);
        pet->AttackStop();
        if (auto charm = pet->GetCharmInfo())
            charm->SetIsCommandAttack(false);
        pet->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
    }
}

bool ClassAction(Player* player, Unit* target, bool combat)
{
    auto ai = ManagedAI(player);
    auto preset = PresetForClass(player->getClass());
    if (!ai || !preset || !player->IsAlive() || player->IsNonMeleeSpellCast(false))
        return false;
    auto state = player->CustomData.GetDefault<ClassExecution>(ClassStorage);
    if (!state->listeners)
    {
        for (auto phase : {BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT})
        {
            ai->ChangeEngine(phase);
            ai->GetCurrentEngine()->AddActionExecutionListener(new ClassActionBoundary(ai));
        }
        state->listeners = true;
    }
    ClearTargets(ai);
    if (state->level != player->GetLevel() || state->spells != player->GetSpellMap().size())
    {
        for (auto phase : {BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT})
            ai->ClearStrategies(phase);
        std::string strategy(preset->strategy);
        if (player->getClass() == CLASS_DRUID && CombatRange(player) > 5.0f)
            strategy = "balance";
        else if (player->getClass() == CLASS_DRUID && !KnownSpell(player, "cat form"))
            strategy = "bear";
        ai->ChangeStrategy("+" + strategy, BOT_STATE_COMBAT);
        ai->ChangeStrategy("+nc", BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+racials", BOT_STATE_COMBAT);
        if (player->getClass() == CLASS_WARLOCK)
            ai->ChangeStrategy("+voidwalker", BOT_STATE_NON_COMBAT);
        if (player->getClass() == CLASS_HUNTER)
            ai->ChangeStrategy("+pet", BOT_STATE_NON_COMBAT);
        state->level = player->GetLevel();
        state->spells = player->GetSpellMap().size();
    }
    ai->ChangeEngine(combat ? BOT_STATE_COMBAT : BOT_STATE_NON_COMBAT);
    ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(target);
    ai->ResetActionDuration();
    bool result = false;
    if (combat && target && player->getClass() == CLASS_WARLOCK && target->GetHealthPct() < 20.0f)
        if (auto summon = sSpellMgr->GetSpellInfo(KnownSpell(player, "summon voidwalker")))
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (summon->Reagent[i] > 0 && player->GetItemCount(summon->Reagent[i]) < summon->ReagentCount[i])
                {
                    result = CastNamed(player, "drain soul", target);
                    break;
                }
    if (!result)
        result = ai->GetCurrentEngine()->DoNextAction(target);
    ClearTargets(ai);
    if (Pet* pet = player->GetPet())
    {
        pet->SetReactState(REACT_PASSIVE);
        if (combat && target && player->IsValidAttackTarget(target))
        {
            if (auto charm = pet->GetCharmInfo())
                charm->SetIsCommandAttack(true);
            pet->AI()->AttackStart(target);
        }
    }
    return result;
}

bool UseFood(Player* player, Item* item)
{
    PlayerbotAI* ai = ManagedAI(player);
    if (!ai || !item)
        return false;
    FoodAction action(ai);
    return action.Use(item);
}
}

namespace WrathIdle
{
bool ActivateWorldTransit(Player* player, TransitLink const& link, Profile const& profile, uint32_t& spent)
{
    if (!PlayerbotsExecutor::Attached(player) || !player->IsAlive() || player->IsInCombat() ||
        player->IsNonMeleeSpellCast(false) || player->GetMapId() != link.from.map)
        return false;
    WorldPoint before{player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()};
    if (link.kind == TransitKind::AreaPortal)
    {
        auto trigger = sObjectMgr->GetAreaTrigger(link.id);
        auto destination = sObjectMgr->GetAreaTriggerTeleport(link.id);
        if (!trigger || !destination || !player->IsInAreaTriggerRadius(trigger) ||
            WorldDistance(link.to, {destination->target_mapId, destination->target_X,
                destination->target_Y, destination->target_Z}) > 1)
            return false;
        WorldPacket packet(CMSG_AREATRIGGER);
        packet << link.id;
        player->GetSession()->HandleAreaTriggerOpcode(packet);
    }
    else if (link.kind == TransitKind::ObjectPortal)
    {
        GameObject* object = nullptr;
        auto bounds = player->GetMap()->GetGameObjectBySpawnIdStore().equal_range(link.source);
        for (auto it = bounds.first; it != bounds.second; ++it)
            if (it->second->GetEntry() == link.id && it->second->isSpawned() &&
                it->second->IsAtInteractDistance(player) && player->CanSeeOrDetect(it->second))
                object = it->second;
        if (!object || object->GetGoType() != GAMEOBJECT_TYPE_SPELLCASTER)
            return false;
        PlayerbotsExecutor::StopMovement(player);
        WorldPacket packet(CMSG_GAMEOBJ_USE);
        packet << object->GetGUID();
        player->GetSession()->HandleGameObjectUseOpcode(packet);
    }
    else if (link.kind == TransitKind::Flight)
    {
        Creature* master = nullptr;
        auto bounds = player->GetMap()->GetCreatureBySpawnIdStore().equal_range(link.id);
        for (auto it = bounds.first; it != bounds.second; ++it)
            if (player->GetNPCIfCanInteractWith(it->second->GetGUID(), UNIT_NPC_FLAG_FLIGHTMASTER))
                master = it->second;
        if (!master || sObjectMgr->GetNearestTaxiNode(*master, player->GetTeamId(true)) != link.source ||
            !player->m_taxi.IsTaximaskNodeKnown(link.source) || !player->m_taxi.IsTaximaskNodeKnown(link.destination))
            return false;
        uint32 path = 0, price = 0;
        sObjectMgr->GetTaxiPath(link.source, link.destination, path, price);
        price = uint32(std::ceil(price * player->GetReputationPriceDiscount(master)));
        if (!path || price > Spendable(player->GetMoney(), profile.values[GoldReserve], profile.values[VisitBudget], spent))
            return false;
        player->RemoveAurasByType(SPELL_AURA_MOUNTED);
        player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
        PlayerbotsExecutor::StopMovement(player);
        uint32 money = player->GetMoney();
        WorldPacket packet(CMSG_ACTIVATETAXI);
        packet << master->GetGUID() << link.source << link.destination;
        player->GetSession()->HandleActivateTaxiOpcode(packet);
        if (player->GetMoney() < money)
            spent += money - player->GetMoney();
    }
    else
        return false;
    return player->IsBeingTeleported() || player->IsInFlight() || player->IsNonMeleeSpellCast(false) ||
        WorldDistance(before, {player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
            player->GetPositionZ()}) > 5;
}

bool DiscoverNearbyFlight(Player* player)
{
    if (!player->IsAlive() || player->IsInCombat() || player->IsInFlight())
        return false;
    std::list<Creature*> candidates;
    auto check = [player](Creature* npc)
    {
        return npc->HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER) && player->IsWithinDistInMap(npc, INTERACTION_DISTANCE);
    };
    Acore::CreatureListSearcher<decltype(check)> searcher(player, candidates, check);
    Cell::VisitObjects(player, searcher, INTERACTION_DISTANCE);
    for (auto master : candidates)
    {
        if (!player->GetNPCIfCanInteractWith(master->GetGUID(), UNIT_NPC_FLAG_FLIGHTMASTER))
            continue;
        uint32 node = sObjectMgr->GetNearestTaxiNode(*master, player->GetTeamId(true));
        if (!node || player->m_taxi.IsTaximaskNodeKnown(node))
            continue;
        player->GetSession()->SendLearnNewTaxiNode(master);
        if (player->m_taxi.IsTaximaskNodeKnown(node))
            return true;
    }
    return false;
}

bool UseTravelMount(Player* player)
{
    if (player->IsMounted() || player->IsInCombat() || player->IsInWater() || player->IsNonMeleeSpellCast(false))
        return false;
    std::vector<std::pair<int32, uint32>> mounts;
    for (auto const& [id, learned] : player->GetSpellMap())
    {
        auto info = sSpellMgr->GetSpellInfo(id);
        if (learned->State == PLAYERSPELL_REMOVED || !learned->Active || !player->HasSpell(id) || !info ||
            !info->HasAura(SPELL_AURA_MOUNTED) || info->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
            continue;
        int32 speed = 0;
        for (auto const& effect : info->Effects)
            if (effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
                speed = std::max(speed, effect.CalcValue(player));
        if (speed > 0)
            mounts.emplace_back(speed, id);
    }
    std::sort(mounts.rbegin(), mounts.rend());
    uint32 attempts = 0;
    for (auto const& [speed, id] : mounts)
    {
        if (++attempts > 4)
            break;
        if (PlayerbotsExecutor::Cast(player, id, player))
            return true;
    }
    return false;
}
}
