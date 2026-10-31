#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace WrathIdle
{
constexpr uint32_t QuestExplorationEventFlag = 0x0002;

template <class Quest>
bool SupportedObjectiveFlags(Quest const& quest)
{
    // The core synthesizes KILL | CAST | SPEAKTO for every NPC/GO counter, including ordinary kills.
    return !quest.HasSpecialFlag(QuestExplorationEventFlag);
}

template <class Parent>
bool RelatedQuestAreas(uint32_t first, uint32_t second, Parent parent)
{
    if (!first || !second)
        return false;
    auto contains = [&parent](uint32_t ancestor, uint32_t child)
    {
        for (uint32_t depth = 0; child && depth < 16; ++depth)
        {
            if (ancestor == child)
                return true;
            child = parent(child);
        }
        return false;
    };
    return contains(first, second) || contains(second, first);
}

struct QuestPlanCandidate
{
    uint32_t id;
    bool accepted;
    float distance;
};

inline uint32_t AcceptedQuestFirst(std::vector<QuestPlanCandidate> const& candidates)
{
    uint32_t result = 0;
    bool accepted = false;
    float nearest = 0;
    for (auto const& candidate : candidates)
    {
        if (!candidate.id || !std::isfinite(candidate.distance) || candidate.distance < 0)
            continue;
        if (result && ((accepted && !candidate.accepted) ||
            (accepted == candidate.accepted && (candidate.distance > nearest ||
                (candidate.distance == nearest && candidate.id > result)))))
            continue;
        result = candidate.id;
        accepted = candidate.accepted;
        nearest = candidate.distance;
    }
    return result;
}

inline bool AutomaticQuestLevel(uint32_t character, uint32_t quest, bool classOrSpellReward)
{
    return classOrSpellReward || quest >= character || character - quest <= 5;
}

struct QuestWorkCandidate
{
    uint32_t id;
    bool incomplete;
    bool objective;
    bool allowed;
    float distance;
    uint32_t area = 0;
    bool sameArea = false;
    uint32_t requiredLevel = 0;
};

inline bool InQuestWorkCluster(QuestWorkCandidate const& candidate, uint32_t completedArea = 0)
{
    return std::isfinite(candidate.distance) && candidate.distance >= 0 && candidate.distance <= 600.0f &&
        (candidate.distance <= 120.0f || candidate.sameArea || (completedArea && candidate.area == completedArea));
}

inline bool NeedsLocalQuestExperience(std::vector<QuestWorkCandidate> const& candidates, uint32_t level)
{
    return std::any_of(candidates.begin(), candidates.end(), [level](auto const& candidate)
    {
        return candidate.id && candidate.incomplete && candidate.objective && !candidate.allowed &&
            candidate.requiredLevel > level && candidate.requiredLevel - level <= 3 && InQuestWorkCluster(candidate);
    });
}

enum class QuestFallback { NoQuest, Experience, Blocked };

inline QuestFallback AutomaticQuestFallback(bool needsExperience, bool blockedAcceptedQuest)
{
    // A level gate is an explicit reason to hunt. Failed interactions alone are not.
    return needsExperience ? QuestFallback::Experience :
        (blockedAcceptedQuest ? QuestFallback::Blocked : QuestFallback::NoQuest);
}

inline bool LocalQuestReward(float distance, bool sameArea)
{
    return std::isfinite(distance) && distance >= 0 && (distance <= 120 || (sameArea && distance <= 600));
}

inline uint32_t NearbyQuestWork(std::vector<QuestWorkCandidate> const& candidates, uint32_t completedArea = 0)
{
    uint32_t result = 0;
    float nearest = 600.0f;
    for (auto const& candidate : candidates)
    {
        if (!candidate.id || !candidate.incomplete || !candidate.objective || !candidate.allowed ||
            !InQuestWorkCluster(candidate, completedArea) || candidate.distance > nearest)
            continue;
        if (result && candidate.distance == nearest && candidate.id > result)
            continue;
        nearest = candidate.distance;
        result = candidate.id;
    }
    return result;
}

struct QuestPickupCandidate
{
    uint32_t id;
    bool available;
    bool allowed;
    float distance;
};

inline uint32_t NearbyQuestPickup(std::vector<QuestPickupCandidate> const& candidates)
{
    uint32_t result = 0;
    float nearest = 80.0f;
    for (auto const& candidate : candidates)
    {
        if (!candidate.id || !candidate.available || !candidate.allowed || !std::isfinite(candidate.distance) ||
            candidate.distance < 0 || candidate.distance > nearest)
            continue;
        if (result && candidate.distance == nearest && candidate.id > result)
            continue;
        nearest = candidate.distance;
        result = candidate.id;
    }
    return result;
}

inline bool HelpsQuestObjective(uint32_t entry, std::vector<uint32_t> const& kills,
    std::vector<uint32_t> const& neededItems, std::vector<uint32_t> const* drops)
{
    return std::find(kills.begin(), kills.end(), entry) != kills.end() || (drops &&
        std::any_of(neededItems.begin(), neededItems.end(), [drops](uint32_t item)
        {
            return std::find(drops->begin(), drops->end(), item) != drops->end();
        }));
}

// Time is active quest work only: pause, combat, recovery and service trips do not consume it.
struct QuestAttempt
{
    uint32_t quest = 0;
    uint32_t stalledSeconds = 0;
    uint32_t failures = 0;
    std::vector<uint32_t> progress;

    void Observe(uint32_t id, std::vector<uint32_t> const& current)
    {
        if (quest != id || progress != current)
        {
            quest = id;
            progress = current;
            stalledSeconds = 0;
            failures = 0;
        }
    }

    bool Failed() const { return failures >= 3 || stalledSeconds >= 600; }
};
}
