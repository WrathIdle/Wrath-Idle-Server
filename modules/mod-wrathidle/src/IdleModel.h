#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// WRI2 wire protocol with versioned saved profiles. The native interface mirrors the wire fields.
namespace WrathIdle
{
constexpr uint32_t Version = 2;
constexpr uint32_t ProfileSchema = 3;
constexpr std::string_view Prefix = "WRI2\t";
constexpr size_t MaxGoals = 8;
constexpr size_t MaxRules = 8;
constexpr size_t MaxProtected = 32;
constexpr uint32_t MaxMoney = 2147483647;

enum Setting : size_t
{
    RecoverHealth, ResumeHealth, RetreatHealth, FreeBagSlots, RepairPercent,
    GoldReserve, VisitBudget, FoodStock, FoodItem, MinEnemyLevel, MaxEnemyLevel,
    SearchRadius, SellQuality, SettingCount
};

struct Rule
{
    uint32_t spell = 0;
    uint32_t minPower = 0;
    uint32_t maxHealth = 100;
    uint32_t self = 0;
    uint32_t missingAura = 0;
};

struct Goal
{
    uint32_t kind = 0; // 0 level, 1 stock, 2 quest, 3 retired travel, 4 training, 5 world-spawn destination
    uint32_t target = 80; // level or item entry
    uint32_t count = 1;
    uint32_t area = 1; // Reserved legacy wire field; geography comes from authoritative world records.
    uint32_t limitSeconds = 0;
    uint32_t elapsedSeconds = 0;
    uint32_t id = 0;
};

struct Profile
{
    std::array<uint32_t, SettingCount> values{60, 90, 20, 2, 30, 100, 100000000, 10, 0, 0, 0, 45, 0};
    std::array<Rule, MaxRules> rules{};
    std::vector<uint32_t> protectedItems;

    std::vector<uint32_t> avoidedQuests;
    uint32_t autoEquip = 1;
    uint32_t autoTrain = 1;
    uint32_t equipBoe = 0;
    uint32_t lockedSlots = 0;
    uint32_t autoTalents = 1;
    uint32_t classPreset = 1;
};

struct Model
{
    uint32_t revision = 1;
    uint32_t nextGoalId = 1;
    Profile profile;
    std::vector<Goal> goals;
};

inline bool Numbers(std::string_view text, std::vector<uint32_t>& values, size_t maxCount = 256)
{
    values.clear();
    while (!text.empty())
    {
        if (text.front() == ' ')
        {
            text.remove_prefix(1);
            continue;
        }
        size_t end = text.find(' ');
        std::string_view token = text.substr(0, end);
        uint32_t value = 0;
        auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || values.size() == maxCount)
            return false;
        values.push_back(value);
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return true;
}

inline std::string Join(std::vector<uint32_t> const& values)
{
    std::string result;
    for (uint32_t value : values)
    {
        if (!result.empty())
            result += ' ';
        result += std::to_string(value);
    }
    return result;
}

inline bool Valid(Profile const& profile)
{
    auto const& v = profile.values;
    if (v[RetreatHealth] < 1 || v[RetreatHealth] >= v[RecoverHealth] ||
        v[RecoverHealth] >= v[ResumeHealth] || v[ResumeHealth] > 100 ||
        v[FreeBagSlots] > 16 || v[RepairPercent] > 100 || v[GoldReserve] > MaxMoney ||
        v[VisitBudget] > MaxMoney || v[FoodStock] > 100 ||
        v[MinEnemyLevel] > 80 || v[MaxEnemyLevel] > 80 ||
        (v[MinEnemyLevel] && v[MaxEnemyLevel] && v[MinEnemyLevel] > v[MaxEnemyLevel]) ||
        v[SearchRadius] < 10 || v[SearchRadius] > 60 || v[SellQuality] > 2 ||
        profile.protectedItems.size() > MaxProtected || profile.avoidedQuests.size() > MaxProtected ||
        profile.autoEquip > 1 || profile.autoTrain > 1 ||
        profile.equipBoe > 1 || profile.lockedSlots >= (1u << 23) ||
        profile.autoTalents > 1 || profile.classPreset > 1)
        return false;
    for (auto const& rule : profile.rules)
        if (rule.maxHealth > 100 || rule.minPower > 100 || rule.self > 1 || rule.missingAura > 1)
            return false;
    return true;
}

inline bool Valid(Goal const& goal)
{
    return goal.kind <= 5 && goal.area == 1 && goal.count > 0 && goal.count <= 10000 &&
        goal.target > 0 && (goal.kind != 0 || (goal.target >= 2 && goal.target <= 80)) &&
        goal.limitSeconds <= 86400 && (goal.kind != 3 || (goal.target <= 2 && goal.count == 1)) &&
        (goal.kind != 4 || (goal.target == 1 && goal.count == 1)) && (goal.kind != 5 || goal.count == 1);
}

inline uint32_t Spendable(uint32_t money, uint32_t reserve, uint32_t budget, uint32_t spent)
{
    return std::min(money > reserve ? money - reserve : 0, budget > spent ? budget - spent : 0);
}

inline bool CanSell(uint32_t entry, uint32_t quality, bool intrinsicallyProtected,
    Profile const& profile, std::vector<Goal> const& goals)
{
    if (intrinsicallyProtected || entry == profile.values[FoodItem] || quality > profile.values[SellQuality] ||
        std::find(profile.protectedItems.begin(), profile.protectedItems.end(), entry) != profile.protectedItems.end())
        return false;
    return std::none_of(goals.begin(), goals.end(), [entry](Goal const& goal)
    {
        return goal.kind == 1 && goal.target == entry;
    });
}

inline std::vector<uint32_t> Encode(Model const& model)
{
    std::vector<uint32_t> values{ProfileSchema, model.revision};
    values.insert(values.end(), model.profile.values.begin(), model.profile.values.end());
    for (auto const& rule : model.profile.rules)
        values.insert(values.end(), {rule.spell, rule.minPower, rule.maxHealth, rule.self, rule.missingAura});
    values.push_back(static_cast<uint32_t>(model.profile.protectedItems.size()));
    values.insert(values.end(), model.profile.protectedItems.begin(), model.profile.protectedItems.end());
    values.push_back(static_cast<uint32_t>(model.goals.size()));
    for (auto const& goal : model.goals)
        values.insert(values.end(), {goal.kind, goal.target, goal.count, goal.area,
            goal.limitSeconds, goal.elapsedSeconds, goal.id});
    values.push_back(uint32_t(model.profile.avoidedQuests.size()));
    values.insert(values.end(), model.profile.avoidedQuests.begin(), model.profile.avoidedQuests.end());
    values.insert(values.end(), {model.profile.autoEquip, model.profile.autoTrain,
        model.profile.equipBoe, model.profile.lockedSlots, model.nextGoalId,
        model.profile.autoTalents, model.profile.classPreset});
    return values;
}

inline bool Decode(std::vector<uint32_t> const& values, Model& output)
{
    if (values.size() < 2 + SettingCount + MaxRules * 5 + 2 ||
        (values[0] != ProfileSchema && values[0] != 2 && values[0] != 1))
        return false;
    Model candidate;
    candidate.revision = values[1];
    if (!candidate.revision)
        return false;
    size_t offset = 2;
    for (auto& value : candidate.profile.values)
        value = values[offset++];
    for (auto& rule : candidate.profile.rules)
    {
        rule = {values[offset], values[offset + 1], values[offset + 2], values[offset + 3], values[offset + 4]};
        offset += 5;
    }
    uint32_t keepCount = values[offset++];
    if (keepCount > MaxProtected || offset + keepCount >= values.size())
        return false;
    candidate.profile.protectedItems.assign(values.begin() + offset, values.begin() + offset + keepCount);
    offset += keepCount;
    uint32_t count = values[offset++];
    bool legacy = values[0] == 1;
    bool classPolicy = values[0] == ProfileSchema;
    size_t tail = classPolicy ? 8 : 6;
    size_t width = legacy ? 6 : 7;
    if (count > MaxGoals || (legacy ? offset + count * width != values.size() :
        offset + count * width + tail > values.size()) || !Valid(candidate.profile))
        return false;
    for (uint32_t i = 0; i < count; ++i)
    {
        Goal goal{values[offset], values[offset + 1], values[offset + 2], values[offset + 3],
            values[offset + 4], values[offset + 5], legacy ? i + 1 : values[offset + 6]};
        if (!Valid(goal) || !goal.id || std::any_of(candidate.goals.begin(), candidate.goals.end(),
            [&goal](Goal const& other) { return other.id == goal.id; }))
            return false;
        candidate.goals.push_back(goal);
        offset += width;
    }
    if (!legacy)
    {
        uint32_t avoidCount = values[offset++];
        if (avoidCount > MaxProtected || offset + avoidCount + tail - 1 != values.size())
            return false;
        candidate.profile.avoidedQuests.assign(values.begin() + offset, values.begin() + offset + avoidCount);
        offset += avoidCount;
        candidate.profile.autoEquip = values[offset++];
        candidate.profile.autoTrain = values[offset++];
        candidate.profile.equipBoe = values[offset++];
        candidate.profile.lockedSlots = values[offset++];
        if (!Valid(candidate.profile))
            return false;
    }
    candidate.nextGoalId = legacy ? count + 1 : values[offset];
    if (classPolicy)
    {
        candidate.profile.autoTalents = values[offset + 1];
        candidate.profile.classPreset = values[offset + 2];
        if (!Valid(candidate.profile))
            return false;
    }
    else if (candidate.profile.values[MinEnemyLevel] == 1 && candidate.profile.values[MaxEnemyLevel] == 6)
    {
        candidate.profile.values[MinEnemyLevel] = 0;
        candidate.profile.values[MaxEnemyLevel] = 0;
    }
    if (!candidate.nextGoalId || std::any_of(candidate.goals.begin(), candidate.goals.end(),
        [&candidate](Goal const& goal) { return goal.id >= candidate.nextGoalId; }))
        return false;
    output = std::move(candidate);
    return true;
}
}
