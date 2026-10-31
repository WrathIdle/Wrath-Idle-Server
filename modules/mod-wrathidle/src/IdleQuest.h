#pragma once

#include "Position.h"
#include <cstdint>
#include <string>
#include <vector>

class Player;
class Quest;

namespace WrathIdle
{
enum class QuestStep { Blocked, Travel, Fight, UseObject, Gather, Waiting, Interacted, FailedInteraction, Complete };
struct QuestIntent
{
    QuestStep step = QuestStep::Blocked;
    Position destination;
    uint32_t creature = 0;
    uint64_t spawn = 0;
    uint32_t lootCount = 0;
    float tolerance = 3.0f;
    std::string reason;
};
bool SupportedQuest(Quest const* quest);
std::vector<uint32_t> QuestProgress(Player* player, uint32_t questId);
QuestIntent PlanQuest(Player* player, uint32_t questId, bool interact);
struct QuestListing
{
    uint32_t id;
    uint32_t status;
    bool supported;
    std::string title;
};
std::vector<QuestListing> ListQuests(Player* player);
uint32_t ChooseQuest(Player* player, std::vector<uint32_t> const& avoided, uint32_t completedQuest = 0);
uint32_t ChooseQuestPickup(Player* player, std::vector<uint32_t> const& avoided,
    std::vector<uint32_t> const* queued = nullptr);
bool ExplainQuestHunting(Player* player, std::vector<uint32_t> const& avoided,
    std::vector<uint32_t> const& unavailable, std::string& reason);
uint32_t ChooseNearbyQuest(Player* player, std::vector<uint32_t> const& avoided,
    std::vector<uint32_t> const* queued = nullptr, uint32_t completedQuest = 0);
}
