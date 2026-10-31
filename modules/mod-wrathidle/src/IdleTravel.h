#pragma once

#include "IdleModel.h"
#include "IdleTravelPolicy.h"

class Player;

namespace WrathIdle
{
std::vector<TransitLink> WorldTransitLinks(Player* player);
bool ActivateWorldTransit(Player* player, TransitLink const& link, Profile const& profile, uint32_t& spent);
bool DiscoverNearbyFlight(Player* player);
bool UseTravelMount(Player* player);
}
