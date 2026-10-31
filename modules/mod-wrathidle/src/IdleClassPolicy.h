#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace WrathIdle
{
struct ClassPreset
{
    uint32_t id;
    std::string_view name;
    std::string_view strategy;
    std::string_view talentName;
    uint32_t tree;
    bool ranged;
};

inline constexpr std::array<ClassPreset, 10> ClassPresets{{
    {1, "Arms Warrior", "arms", "arms pve", 0, false},
    {2, "Retribution Paladin", "ret", "ret pve", 2, false},
    {3, "Beast Mastery Hunter", "bm", "bm pve", 0, true},
    {4, "Combat Rogue", "combat", "combat pve", 1, false},
    {5, "Shadow Priest", "shadow", "shadow pve", 2, true},
    {6, "Blood Death Knight", "blood", "blood pve", 0, false},
    {7, "Enhancement Shaman", "enh", "enh pve", 1, false},
    {8, "Frost Mage", "frost", "frost pve", 2, true},
    {9, "Affliction Warlock", "affli", "affli pve", 0, true},
    {11, "Feral Druid", "cat", "cat pve", 1, false}
}};

inline ClassPreset const* PresetForClass(uint32_t id)
{
    for (auto const& preset : ClassPresets)
        if (preset.id == id)
            return &preset;
    return nullptr;
}

inline bool SupportedProgression(uint32_t race, uint32_t cls, uint32_t level)
{
    return race && PresetForClass(cls) && level >= 1 && level <= 80;
}

inline bool EnemyLevelAllowed(uint32_t character, uint32_t enemy, uint32_t minimum, uint32_t maximum, bool quest = false)
{
    uint32_t low = minimum ? minimum : (quest ? 1 : (character > 2 ? character - 2 : 1));
    uint32_t high = maximum ? std::min(maximum, character) : character;
    return enemy >= low && enemy <= high;
}

inline bool EnemyLevelRangeAllowed(uint32_t character, uint32_t lowest, uint32_t highest,
    uint32_t minimum, uint32_t maximum)
{
    uint32_t low = minimum ? minimum : (character > 2 ? character - 2 : 1);
    uint32_t high = maximum ? std::min(maximum, character) : character;
    return lowest <= highest && highest >= low && lowest <= high;
}

// A preset may add points to a compatible allocation; it never resets an existing build.
inline bool TalentAllocationCompatible(uint32_t currentRank, uint32_t plannedRank)
{
    return currentRank <= plannedRank;
}

inline bool NeedsRecovery(float health, float mana, bool usesMana, uint32_t threshold)
{
    return health < threshold || (usesMana && mana < 20.0f);
}

inline bool RecoveryComplete(float health, float mana, bool usesMana, uint32_t threshold)
{
    return health >= threshold && (!usesMana || mana >= 80.0f);
}
}
