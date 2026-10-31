#include "IdleUpkeep.h"
#include "IdleNavigation.h"
#include "IdleClassPolicy.h"
#include "IdleWorld.h"
#include "Bag.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include <list>
#include <limits>
#include "Creature.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StatsWeightCalculator.h"
#include "Trainer.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "PlayerbotsExecutor.h"
#include "Playerbots.h"
#include <map>
#include <cmath>
#include "Pet.h"

namespace WrathIdle
{
std::string MaintainClass(Player* player, Profile const& profile)
{
    if (!profile.classPreset || player->IsInCombat() || !player->IsAlive() || player->IsNonMeleeSpellCast(false))
        return {};
    if (player->getClass() == CLASS_MAGE && profile.values[FoodStock])
    {
        if (!CarriedDrinkEntry(player) && PlayerbotsExecutor::CastNamed(player, "conjure water", player))
            return "Conjuring water using a learned spell.";
        if (!player->GetItemCount(CarriedFoodEntry(player, profile)) &&
            PlayerbotsExecutor::CastNamed(player, "conjure food", player))
            return "Conjuring food using a learned spell.";
    }
    Pet* pet = player->GetPet();
    auto feeding = sSpellMgr->GetSpellInfo(PlayerbotsExecutor::KnownSpell(player, "feed pet"));
    if (!pet || !pet->IsAlive() || pet->getPetType() != HUNTER_PET || pet->GetHappinessState() == HAPPY || !feeding)
        return {};
    for (auto const& effect : feeding->Effects)
        if (effect.Effect == SPELL_EFFECT_FEED_PET && effect.TriggerSpell && pet->HasAura(effect.TriggerSpell))
            return {};
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag <= INVENTORY_SLOT_BAG_END; ++bag)
    {
        uint8 container = bag == INVENTORY_SLOT_BAG_END ? INVENTORY_SLOT_BAG_0 : bag;
        Bag* storage = container == INVENTORY_SLOT_BAG_0 ? nullptr : player->GetBagByPos(container);
        if (container != INVENTORY_SLOT_BAG_0 && !storage)
            continue;
        uint8 first = storage ? 0 : INVENTORY_SLOT_ITEM_START;
        uint8 last = storage ? storage->GetBagSize() : INVENTORY_SLOT_ITEM_END;
        for (uint8 slot = first; slot < last; ++slot)
            if (Item* item = player->GetItemByPos(container, slot))
                if (!player->HasQuestForItem(item->GetEntry()) &&
                    std::find(profile.protectedItems.begin(), profile.protectedItems.end(), item->GetEntry()) ==
                        profile.protectedItems.end() && pet->HaveInDiet(item->GetTemplate()) &&
                    pet->GetCurrentFoodBenefitLevel(item->GetTemplate()->ItemLevel) &&
                    PlayerbotsExecutor::CastOnItem(player, "feed pet", item))
                    return "Feeding the hunter pet from carried food through the normal feeding spell.";
    }
    return {};
}

TalentUpdate DevelopTalents(Player* player, Profile const& profile)
{
    if (!profile.autoTalents || !player->GetFreeTalentPoints() || player->IsInCombat() || !player->IsAlive())
        return {};
    auto preset = PresetForClass(player->getClass());
    if (!preset)
        return {false, "No class talent preset is available."};
    int selected = -1;
    for (uint32 i = 0; i < MAX_SPECNO; ++i)
        if (sPlayerbotAIConfig.PremadeSpecName[player->getClass()][i] == preset->talentName)
        {
            selected = int(i);
            break;
        }
    if (selected < 0)
        return {false, "The configured talent template is missing for " + std::string(preset->name) + "."};
    auto const& order = sPlayerbotAIConfig.ParsedSpecLinkOrder[player->getClass()][selected][80];
    if (order.empty())
        return {false, "The configured talent template contains no allocation data."};
    std::map<uint32, uint32> planned;
    std::vector<uint32> sequence;
    for (auto const& cell : order)
    {
        if (cell.size() != 4 || !cell[3] || cell[3] > MAX_TALENT_RANK)
            return {false, "The configured talent template contains an invalid rank."};
        for (uint32 id = 0; id < sTalentStore.GetNumRows(); ++id)
        {
            auto talent = sTalentStore.LookupEntry(id);
            auto tab = talent ? sTalentTabStore.LookupEntry(talent->TalentTab) : nullptr;
            if (!tab || !(tab->ClassMask & player->getClassMask()) || tab->tabpage != cell[0] ||
                talent->Row != cell[1] || talent->Col != cell[2])
                continue;
            if (!planned.count(talent->TalentID))
                sequence.push_back(talent->TalentID);
            planned[talent->TalentID] = std::max(planned[talent->TalentID], cell[3]);
        }
    }
    for (auto const& [spell, learned] : player->GetTalentMap())
    {
        if (learned->State == PLAYERSPELL_REMOVED || !learned->IsInSpec(player->GetActiveSpec()))
            continue;
        auto position = GetTalentSpellPos(spell);
        if (position && !TalentAllocationCompatible(position->rank + 1, planned[position->talent_id]))
            return {false, "Existing talents differ from " + std::string(preset->name) +
                ". Review the allocation; no reset was performed."};
    }
    for (uint32 id : sequence)
    {
        auto talent = sTalentStore.LookupEntry(id);
        if (!talent)
            continue;
        uint32 current = 0;
        for (uint32 rank = 0; rank < MAX_TALENT_RANK; ++rank)
            if (talent->RankID[rank] && player->HasTalent(talent->RankID[rank], player->GetActiveSpec()))
                current = rank + 1;
        if (current >= planned[id])
            continue;
        uint32 before = player->GetFreeTalentPoints();
        player->LearnTalent(id, current);
        if (player->GetFreeTalentPoints() < before)
        {
            player->SendTalentsInfoData(false);
            return {true, "Spent one earned talent point for " + std::string(preset->name) + "."};
        }
    }
    return {false, "No next talent fits the preset's prerequisites. Review the allocation."};
}

bool IsHealingFood(Player* player, uint32_t entry)
{
    auto item = sObjectMgr->GetItemTemplate(entry);
    if (!item || item->Class != ITEM_CLASS_CONSUMABLE || item->SubClass != ITEM_SUBCLASS_FOOD ||
        player->CanUseItem(item) != EQUIP_ERR_OK)
        return false;
    for (auto const& effect : item->Spells)
        if (auto spell = sSpellMgr->GetSpellInfo(effect.SpellId))
            if (spell->HasAura(SPELL_AURA_MOD_REGEN))
                return true;
    return false;
}

bool IsManaDrink(Player* player, uint32_t entry)
{
    auto item = sObjectMgr->GetItemTemplate(entry);
    if (!item || item->Class != ITEM_CLASS_CONSUMABLE || item->SubClass != ITEM_SUBCLASS_FOOD ||
        player->CanUseItem(item) != EQUIP_ERR_OK)
        return false;
    for (auto const& effect : item->Spells)
        if (auto spell = sSpellMgr->GetSpellInfo(effect.SpellId))
            for (auto const& aura : spell->Effects)
                if ((aura.ApplyAuraName == SPELL_AURA_MOD_POWER_REGEN ||
                    aura.ApplyAuraName == SPELL_AURA_PERIODIC_ENERGIZE) && aura.MiscValue == POWER_MANA)
                    return true;
    return false;
}

bool IsUsableAmmo(Player* player, uint32_t entry)
{
    auto item = sObjectMgr->GetItemTemplate(entry);
    auto weapon = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!item || item->Class != ITEM_CLASS_PROJECTILE || !weapon || player->CanUseAmmo(entry) != EQUIP_ERR_OK)
        return false;
    uint32 kind = weapon->GetTemplate()->SubClass;
    return (kind == ITEM_SUBCLASS_WEAPON_GUN && item->SubClass == ITEM_SUBCLASS_BULLET) ||
        ((kind == ITEM_SUBCLASS_WEAPON_BOW || kind == ITEM_SUBCLASS_WEAPON_CROSSBOW) &&
            item->SubClass == ITEM_SUBCLASS_ARROW);
}

namespace
{
uint32 CarriedSupply(Player* player, bool (*matches)(Player*, uint32))
{
    uint32 selected = 0;
    uint32 best = 0;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag <= INVENTORY_SLOT_BAG_END; ++bag)
    {
        uint8 container = bag == INVENTORY_SLOT_BAG_END ? INVENTORY_SLOT_BAG_0 : bag;
        Bag* storage = container == INVENTORY_SLOT_BAG_0 ? nullptr : player->GetBagByPos(container);
        if (container != INVENTORY_SLOT_BAG_0 && !storage)
            continue;
        uint8 first = storage ? 0 : INVENTORY_SLOT_ITEM_START;
        uint8 last = storage ? storage->GetBagSize() : INVENTORY_SLOT_ITEM_END;
        for (uint8 slot = first; slot < last; ++slot)
            if (auto item = player->GetItemByPos(container, slot))
                if (matches(player, item->GetEntry()) && player->GetItemCount(item->GetEntry()) > best)
                {
                    selected = item->GetEntry();
                    best = player->GetItemCount(selected);
                }
    }
    return selected;
}
}

uint32_t CarriedDrinkEntry(Player* player) { return CarriedSupply(player, IsManaDrink); }
uint32_t CarriedAmmoEntry(Player* player) { return CarriedSupply(player, IsUsableAmmo); }

uint32_t CarriedFoodEntry(Player* player, Profile const& profile)
{
    if (profile.values[FoodItem])
        return profile.values[FoodItem];
    uint32 selected = 0;
    uint32 bestCount = 0;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag <= INVENTORY_SLOT_BAG_END; ++bag)
    {
        uint8 container = bag == INVENTORY_SLOT_BAG_END ? INVENTORY_SLOT_BAG_0 : bag;
        Bag* storage = container == INVENTORY_SLOT_BAG_0 ? nullptr : player->GetBagByPos(container);
        if (container != INVENTORY_SLOT_BAG_0 && !storage)
            continue;
        uint8 first = storage ? 0 : INVENTORY_SLOT_ITEM_START;
        uint8 last = storage ? storage->GetBagSize() : INVENTORY_SLOT_ITEM_END;
        for (uint8 slot = first; slot < last; ++slot)
            if (Item* item = player->GetItemByPos(container, slot))
                if (IsHealingFood(player, item->GetEntry()))
                {
                    uint32 count = player->GetItemCount(item->GetEntry());
                    if (count > bestCount)
                    {
                        selected = item->GetEntry();
                        bestCount = count;
                    }
                }
    }
    return selected;
}

bool ResolveDestination(Player* player, uint32_t spawnId, ServiceDestination& destination)
{
    auto spawn = sObjectMgr->GetCreatureData(spawnId);
    if (!spawn || spawn->mapid != player->GetMapId() || !FriendlySpawn(player, *spawn))
        return false;
    auto creature = sObjectMgr->GetCreatureTemplate(spawn->id);
    if (!creature || !(creature->npcflag & (UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_REPAIR |
        UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_QUESTGIVER)))
        return false;
    destination.id = spawnId;
    destination.entry = spawn->id;
    destination.position.Relocate(spawn->posX, spawn->posY, spawn->posZ, spawn->orientation);
    destination.name = creature->Name;
    return true;
}

std::vector<ServiceDestination> ListDestinations(Player* player)
{
    std::vector<ServiceDestination> result;
    for (auto const& spawn : MapCreatures(player->GetMapId()))
    {
        if (player->GetExactDist2d(spawn.posX, spawn.posY) > 2000.0f)
            continue;
        ServiceDestination destination;
        if (ResolveDestination(player, spawn.spawnId, destination))
            result.push_back(std::move(destination));
    }
    std::sort(result.begin(), result.end(), [player](auto const& left, auto const& right)
    {
        float a = player->GetExactDistSq(left.position.GetPositionX(), left.position.GetPositionY(),
            left.position.GetPositionZ());
        float b = player->GetExactDistSq(right.position.GetPositionX(), right.position.GetPositionY(),
            right.position.GetPositionZ());
        return a == b ? left.id < right.id : a < b;
    });
    return result;
}

bool FindService(Player* player, uint32_t requiredFlags, uint32_t item, ServiceDestination& destination, SupplyKind supply)
{
    uint32 attempts = 0;
    for (auto const& candidate : ListDestinations(player))
    {
        auto creature = sObjectMgr->GetCreatureTemplate(candidate.entry);
        if (!creature || (creature->npcflag & requiredFlags) != requiredFlags)
            continue;
        bool stocked = !item && supply == SupplyKind::Any;
        uint32 selected = 0;
        if (auto stock = sObjectMgr->GetNpcVendorItemList(candidate.entry))
            for (uint32 slot = 0; slot < stock->GetItemCount(); ++slot)
                if (auto offer = stock->GetItem(slot))
                    if (!offer->ExtendedCost && (item ? offer->item == item :
                        ((supply == SupplyKind::Food && IsHealingFood(player, offer->item)) ||
                        (supply == SupplyKind::Drink && IsManaDrink(player, offer->item)) ||
                        (supply == SupplyKind::Ammo && IsUsableAmmo(player, offer->item)))))
                    {
                        stocked = true;
                        selected = offer->item;
                        break;
                    }
        if (!stocked)
            continue;
        if (++attempts > 4)
            return false;
        if (player->GetDistance(candidate.position) > INTERACTION_DISTANCE &&
            !PlayerbotsExecutor::CanWalk(player, candidate.position))
            continue;
        destination = candidate;
        destination.item = selected;
        return true;
    }
    return false;
}

std::vector<SupplyRequest> PlanClassSupplies(Player* player, Profile const& profile)
{
    std::vector<SupplyRequest> result;
    uint32 budget = Spendable(player->GetMoney(), profile.values[GoldReserve], profile.values[VisitBudget], 0);
    if (!budget)
        return result;
    auto add = [&](uint32 item, uint32 count, SupplyKind supply)
    {
        uint32 minimum = std::max(1u, count / 3);
        if (item && player->GetItemCount(item) >= minimum)
            return;
        ServiceDestination vendor;
        if (!FindService(player, UNIT_NPC_FLAG_VENDOR, item, vendor, supply))
            return;
        auto prototype = sObjectMgr->GetItemTemplate(vendor.item);
        if (prototype && prototype->BuyCount && prototype->BuyPrice <= budget &&
            player->GetItemCount(vendor.item) < minimum)
            result.push_back({vendor, count});
    };
    if (player->GetMaxPower(POWER_MANA) && profile.values[FoodStock])
        add(CarriedDrinkEntry(player), profile.values[FoodStock], SupplyKind::Drink);
    if (player->getClass() == CLASS_HUNTER)
        add(CarriedAmmoEntry(player), 200, SupplyKind::Ammo);
    std::map<uint32, uint32> reagents;
    for (auto const& [id, learned] : player->GetSpellMap())
    {
        if (learned->State == PLAYERSPELL_REMOVED || !learned->Active || !player->HasSpell(id))
            continue;
        if (auto info = sSpellMgr->GetSpellInfo(id))
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (info->Reagent[i] > 0 && info->ReagentCount[i])
                    reagents[info->Reagent[i]] = std::max(reagents[info->Reagent[i]], info->ReagentCount[i] * 3);
    }
    for (auto const& [entry, count] : reagents)
    {
        if (result.size() >= 6)
            break;
        add(entry, std::max(5u, count), SupplyKind::Any);
    }
    return result;
}

bool IsClassSupply(Player* player, uint32_t entry)
{
    if (IsManaDrink(player, entry) || IsUsableAmmo(player, entry))
        return true;
    for (auto const& [id, learned] : player->GetSpellMap())
        if (learned->State != PLAYERSPELL_REMOVED && learned->Active)
            if (auto spell = sSpellMgr->GetSpellInfo(id))
                for (int32 reagent : spell->Reagent)
                    if (reagent > 0 && uint32(reagent) == entry)
                        return true;
    return false;
}

bool BuySupply(Player* player, Profile const& profile, SupplyRequest const& request, uint32_t& spent)
{
    Creature* vendor = player->FindNearestCreature(request.vendor.entry, INTERACTION_DISTANCE);
    if (!vendor || !player->GetNPCIfCanInteractWith(vendor->GetGUID(), UNIT_NPC_FLAG_VENDOR))
        return false;
    auto stock = vendor->GetVendorItems();
    if (!stock)
        return false;
    for (uint32 slot = 0; slot < stock->GetItemCount(); ++slot)
    {
        auto offer = stock->GetItem(slot);
        if (!offer || offer->item != request.vendor.item || offer->ExtendedCost)
            continue;
        auto prototype = sObjectMgr->GetItemTemplate(offer->item);
        if (!prototype || !prototype->BuyCount)
            return false;
        uint32 price = uint32(std::floor(prototype->BuyPrice * player->GetReputationPriceDiscount(vendor)));
        uint32 initialCount = player->GetItemCount(offer->item);
        while (player->GetItemCount(offer->item) < request.count &&
            price <= Spendable(player->GetMoney(), profile.values[GoldReserve], profile.values[VisitBudget], spent))
        {
            uint32 before = player->GetMoney();
            uint32 count = player->GetItemCount(offer->item);
            player->BuyItemFromVendorSlot(vendor->GetGUID(), slot, offer->item, 1, NULL_BAG, NULL_SLOT);
            spent += before - player->GetMoney();
            if (count == player->GetItemCount(offer->item))
                return false;
        }
        return player->GetItemCount(offer->item) >= request.count ||
            player->GetItemCount(offer->item) > initialCount ||
            price > Spendable(player->GetMoney(), profile.values[GoldReserve], profile.values[VisitBudget], spent);
    }
    return false;
}

bool IsPendingUpgrade(Player* player, Item* item)
{
    if (!item || player->CanUseItem(item) != EQUIP_ERR_OK)
        return false;
    auto proto = item->GetTemplate();
    if (proto->Class == ITEM_CLASS_CONTAINER && !proto->BagFamily)
    {
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        {
            Bag* old = player->GetBagByPos(slot);
            if (!old || old->GetBagSize() < proto->ContainerSlots)
                return true;
        }
        return false;
    }
    uint8 slot = player->FindEquipSlot(proto, NULL_SLOT, true);
    if (slot >= EQUIPMENT_SLOT_END)
        return false;
    StatsWeightCalculator weights(player);
    weights.SetItemSetBonus(false);
    weights.SetOverflowPenalty(false);
    Item* old = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    float previous = old ? weights.CalculateItem(old->GetEntry(), old->GetItemRandomPropertyId(), slot) : 0;
    if (proto->InventoryType == INVTYPE_2HWEAPON)
        if (Item* offhand = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
            previous += weights.CalculateItem(offhand->GetEntry(), offhand->GetItemRandomPropertyId(),
                EQUIPMENT_SLOT_OFFHAND);
    float score = weights.CalculateItem(item->GetEntry(), item->GetItemRandomPropertyId(), slot);
    return score > 0 && score > previous;
}

std::string MaintainEquipment(Player* player, Profile const& profile)
{
    if (!profile.autoEquip || player->IsInCombat() || !player->IsAlive())
        return {};
    StatsWeightCalculator weights(player);
    weights.SetItemSetBonus(false);
    weights.SetOverflowPenalty(false);
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag <= INVENTORY_SLOT_BAG_END; ++bag)
    {
        uint8 container = bag == INVENTORY_SLOT_BAG_END ? INVENTORY_SLOT_BAG_0 : bag;
        Bag* storage = container == INVENTORY_SLOT_BAG_0 ? nullptr : player->GetBagByPos(container);
        uint8 first = storage ? 0 : INVENTORY_SLOT_ITEM_START;
        uint8 last = storage ? storage->GetBagSize() : INVENTORY_SLOT_ITEM_END;
        if (container != INVENTORY_SLOT_BAG_0 && !storage)
            continue;
        for (uint8 slot = first; slot < last; ++slot)
        {
            Item* item = player->GetItemByPos(container, slot);
            if (!item || item->IsWrapped() || item->IsRefundable() || player->IsItemInEquipmentSet(item->GetGUID()) ||
                std::find(profile.protectedItems.begin(), profile.protectedItems.end(), item->GetEntry()) !=
                    profile.protectedItems.end())
                continue;
            auto proto = item->GetTemplate();
            if ((!profile.equipBoe && proto->Bonding == BIND_WHEN_EQUIPPED && !item->IsSoulBound()) ||
                player->CanUseItem(item) != EQUIP_ERR_OK)
                continue;
            uint16 destination = 0;
            if (proto->Class == ITEM_CLASS_CONTAINER && !proto->BagFamily)
            {
                for (uint8 target = INVENTORY_SLOT_BAG_START; target < INVENTORY_SLOT_BAG_END; ++target)
                {
                    if (profile.lockedSlots & (1u << target))
                        continue;
                    Bag* old = player->GetBagByPos(target);
                    // SwapItem validates capacity and transfers occupied bag contents through the core.
                    if (old && old->GetBagSize() >= proto->ContainerSlots)
                        continue;
                    uint16 src = uint16(container) << 8 | slot;
                    uint16 dst = uint16(INVENTORY_SLOT_BAG_0) << 8 | target;
                    auto guid = item->GetGUID();
                    player->SwapItem(src, dst);
                    if (Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, target))
                        if (equipped->GetGUID() == guid)
                            return "Equipped a larger bag: " + proto->Name1;
                }
                continue;
            }
            if (player->CanEquipItem(NULL_SLOT, destination, item, true) != EQUIP_ERR_OK)
                continue;
            uint8 target = uint8(destination);
            if (target >= EQUIPMENT_SLOT_END || (profile.lockedSlots & (1u << target)))
                continue;
            // Do not displace an offhand implicitly when comparing a single weapon.
            if (proto->InventoryType == INVTYPE_2HWEAPON &&
                player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                continue;
            Item* old = player->GetItemByPos(INVENTORY_SLOT_BAG_0, target);
            float score = weights.CalculateItem(item->GetEntry(), item->GetItemRandomPropertyId(), target);
            float previous = old ? weights.CalculateItem(old->GetEntry(), old->GetItemRandomPropertyId(), target) : 0;
            if (score <= previous || score <= 0)
                continue;
            auto guid = item->GetGUID();
            player->SwapItem(uint16(container) << 8 | slot, destination);
            if (Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, target))
                if (equipped->GetGUID() == guid)
                    return "Equipped an upgrade: " + proto->Name1;
        }
    }
    return {};
}

namespace
{
bool DevelopmentSpell(Trainer::Type type, SpellInfo const* spell, uint32 depth = 0)
{
    if (!spell || depth > 2)
        return false;
    if (type == Trainer::Type::Class)
        return true;
    if (type == Trainer::Type::Pet)
        return false;
    for (auto const& effect : spell->Effects)
    {
        if (effect.Effect == SPELL_EFFECT_SKILL)
            if (auto skill = sSkillLineStore.LookupEntry(effect.MiscValue))
                if (skill->id == SKILL_RIDING || skill->categoryId == SKILL_CATEGORY_WEAPON)
                    return true;
        if (effect.Effect == SPELL_EFFECT_LEARN_SPELL &&
            DevelopmentSpell(type, sSpellMgr->GetSpellInfo(effect.TriggerSpell), depth + 1))
            return true;
    }
    return false;
}
}

std::string TrainNearby(Player* player, Profile const& profile, uint32_t& spent, bool explicitRequest)
{
    if ((!profile.autoTrain && !explicitRequest) || player->IsInCombat() || !player->IsAlive())
        return {};
    std::list<Creature*> nearby;
    auto check = [player](Creature* npc)
    {
        return npc->IsAlive() && npc->IsTrainer() && player->IsWithinDistInMap(npc, INTERACTION_DISTANCE);
    };
    Acore::CreatureListSearcher<decltype(check)> searcher(player, nearby, check);
    Cell::VisitObjects(player, searcher, INTERACTION_DISTANCE);
    for (Creature* npc : nearby)
    {
        if (!player->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_TRAINER))
            continue;
        auto trainer = sObjectMgr->GetTrainer(npc->GetEntry());
        if (!trainer || !trainer->IsTrainerValidForPlayer(player))
            continue;
        for (auto const& spell : trainer->GetSpells())
        {
            uint32 cost = uint32(spell.MoneyCost * player->GetReputationPriceDiscount(npc));
            if (!DevelopmentSpell(trainer->GetTrainerType(), sSpellMgr->GetSpellInfo(spell.SpellId)) ||
                !trainer->CanTeachSpell(player, &spell) ||
                cost > Spendable(player->GetMoney(), profile.values[GoldReserve], profile.values[VisitBudget], spent))
                continue;
            uint32 before = player->GetMoney();
            size_t known = player->GetSpellMap().size();
            trainer->TeachSpell(npc, player, spell.SpellId);
            spent += before - player->GetMoney();
            if (player->GetMoney() < before || player->GetSpellMap().size() != known ||
                !trainer->CanTeachSpell(player, &spell))
                return "Trained a permitted class, weapon, or riding ability: " + std::to_string(spell.SpellId);
        }
    }
    return {};
}

bool FindTraining(Player* player, Profile const& profile, uint32_t spent, Position& destination)
{
    float nearest = std::numeric_limits<float>::max();
    bool found = false;
    uint32 budget = Spendable(player->GetMoney(), profile.values[GoldReserve], profile.values[VisitBudget], spent);
    for (auto const& spawn : MapCreatures(player->GetMapId()))
    {
        auto trainer = sObjectMgr->GetTrainer(spawn.id);
        if (!trainer || !FriendlySpawn(player, spawn) ||
            !trainer->IsTrainerValidForPlayer(player))
            continue;
        bool available = false;
        for (auto const& spell : trainer->GetSpells())
            available = available || (DevelopmentSpell(trainer->GetTrainerType(), sSpellMgr->GetSpellInfo(spell.SpellId)) &&
                spell.MoneyCost <= budget && trainer->CanTeachSpell(player, &spell));
        float distance = player->GetExactDistSq(spawn.posX, spawn.posY, spawn.posZ);
        if (!available || distance >= nearest)
            continue;
        nearest = distance;
        destination.Relocate(spawn.posX, spawn.posY, spawn.posZ, spawn.orientation);
        found = true;
    }
    return found;
}
}
