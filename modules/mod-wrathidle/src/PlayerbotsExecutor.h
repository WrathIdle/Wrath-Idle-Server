#pragma once

#include <cstdint>
#include <string_view>

class Item;
class Player;
class Unit;
class GameObject;
struct Position;

namespace WrathIdle::PlayerbotsExecutor
{
bool Available();
bool Attach(Player* player);
bool Attached(Player* player);
void Detach(Player* player);
bool CanWalk(Player* player, Position const& destination);
bool Walk(Player* player, Position const& destination);
void StopMovement(Player* player);
bool Attack(Player* player, Unit* target);
bool Cast(Player* player, uint32_t spell, Unit* target);
uint32_t KnownRank(Player* player, uint32_t spell);
uint32_t KnownSpell(Player* player, std::string_view name);
bool CastNamed(Player* player, std::string_view name, Unit* target);
bool CastOnItem(Player* player, std::string_view name, Item* item);
bool OpenObject(Player* player, GameObject* object);
float CombatRange(Player* player);
bool ClassAction(Player* player, Unit* target, bool combat);
void StopPet(Player* player);
bool UseFood(Player* player, Item* item);
}
