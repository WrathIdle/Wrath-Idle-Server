#pragma once

#include "IdleModel.h"
#include "Position.h"
#include <string>

class Player;
class Item;
struct Position;

namespace WrathIdle
{
// Performs at most one successful inventory or trainer transaction per call.
std::string MaintainEquipment(Player* player, Profile const& profile);
std::string TrainNearby(Player* player, Profile const& profile, uint32_t& spent, bool explicitRequest = false);
bool FindTraining(Player* player, Profile const& profile, uint32_t spent, Position& destination);
bool IsPendingUpgrade(Player* player, Item* item);
bool IsClassSupply(Player* player, uint32_t entry);
struct TalentUpdate
{
    bool changed = false;
    std::string reason;
};
TalentUpdate DevelopTalents(Player* player, Profile const& profile);
std::string MaintainClass(Player* player, Profile const& profile);
struct ServiceDestination
{
    uint32_t id = 0;
    uint32_t entry = 0;
    uint32_t item = 0;
    Position position;
    std::string name;
};
bool IsHealingFood(Player* player, uint32_t entry);
bool IsManaDrink(Player* player, uint32_t entry);
uint32_t CarriedDrinkEntry(Player* player);
bool IsUsableAmmo(Player* player, uint32_t entry);
uint32_t CarriedAmmoEntry(Player* player);
uint32_t CarriedFoodEntry(Player* player, Profile const& profile);
enum class SupplyKind { Any, Food, Drink, Ammo };
bool FindService(Player* player, uint32_t requiredFlags, uint32_t item, ServiceDestination& destination,
    SupplyKind supply = SupplyKind::Any);
struct SupplyRequest
{
    ServiceDestination vendor;
    uint32_t count = 0;
};
std::vector<SupplyRequest> PlanClassSupplies(Player* player, Profile const& profile);
bool BuySupply(Player* player, Profile const& profile, SupplyRequest const& request, uint32_t& spent);
std::vector<ServiceDestination> ListDestinations(Player* player);
bool ResolveDestination(Player* player, uint32_t spawnId, ServiceDestination& destination);
}
