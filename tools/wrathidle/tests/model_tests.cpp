#include "IdleModel.h"
#include "IdleClassPolicy.h"
#include "IdleQuestPolicy.h"
#include "IdleNavigation.h"
#include "IdleValuePolicy.h"
#include "IdleTravelPolicy.h"
#include <cstdlib>
#include <iostream>
#include <limits>

void Require(bool value, char const* message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

namespace
{
struct BorrowedUnit { };
struct BorrowedItem { };
struct ContextValue
{
    virtual ~ContextValue() = default;
    virtual void Reset() = 0;
    uint32_t resets = 0;
};
template <class T>
struct TypedContextValue
{
    virtual ~TypedContextValue() = default;
    virtual void Set(T) = 0;
};
// Match the upstream ManualSetValue contract: Reset restores the constructor default.
template <class T>
struct ContextFixture : ContextValue, TypedContextValue<T>
{
    ContextFixture(T initial, T current) : initial(initial), value(current) { }
    void Set(T current) override { value = current; }
    void Reset() override { ++resets; value = initial; }
    T initial, value;
};
struct OwnedLoot
{
    explicit OwnedLoot(uint32_t& destructions) : destructions(destructions) { }
    ~OwnedLoot() { ++destructions; }
    uint32_t& destructions;
    uint32_t attacks = 0;
};
struct OwnedLootFixture : ContextFixture<OwnedLoot*>
{
    explicit OwnedLootFixture(uint32_t& destructions) : ContextFixture(nullptr, new OwnedLoot(destructions)) { }
    ~OwnedLootFixture() override { delete value; }
};
}

int main()
{
    using namespace WrathIdle;
    uint32_t destructions = 0;
    {
        BorrowedUnit unit;
        BorrowedItem item;
        ContextFixture<BorrowedUnit*> target(&unit, &unit);
        ContextFixture<std::vector<BorrowedItem*>> items({}, {&item});
        ContextFixture<uint32_t> cooldown(0, 42);
        OwnedLootFixture loot(destructions);
        auto owned = loot.value;
        for (uint32_t attack = 0; attack < 32; ++attack)
        {
            target.Set(&unit);
            items.Set({&item});
            for (ContextValue* value : {static_cast<ContextValue*>(&target), static_cast<ContextValue*>(&items),
                static_cast<ContextValue*>(&cooldown), static_cast<ContextValue*>(&loot)})
                ClearBorrowedValues<TypedContextValue, BorrowedUnit*, std::vector<BorrowedItem*>>(value);
            Require(!target.value && items.value.empty(), "Each combat pulse releases borrowed entity targets");
            Require(loot.value == owned && !loot.resets, "Combat cleanup must retain the owned loot container");
            Require(cooldown.value == 42 && !cooldown.resets, "Cleanup must preserve unrelated execution state");
            ++loot.value->attacks;
        }
        Require(loot.value->attacks == 32, "Repeated attacks retain one usable owned context");
    }
    Require(destructions == 1, "The owned context must be released exactly once when its owner is destroyed");
    WorldPoint journeyStart{17, 0, 0, 0}, journeyEnd{29, 0, 0, 0};
    Require(PlanWorldJourney(journeyStart, journeyEnd, {}, {}, 100, 7).empty(),
        "Different maps cannot be joined by a fabricated walking edge");
    std::vector<TransitLink> transits{
        {TransitKind::Flight, journeyStart, {23, 0, 0, 0}, 1, 1, 2, 8, 1},
        {TransitKind::AreaPortal, journeyStart, {23, 0, 0, 0}, 2, 0, 0, 0, 5},
        {TransitKind::Flight, {23, 0, 0, 0}, journeyEnd, 3, 2, 3, 5, 1}};
    auto journey = PlanWorldJourney(journeyStart, journeyEnd, transits, {}, 10, 7);
    Require(journey.size() == 2 && journey[0].kind == TransitKind::AreaPortal && journey[1].price == 5,
        "A slower affordable arrival must survive when the fastest arrival cannot afford the next flight");
    journey = PlanWorldJourney(journeyStart, journeyEnd, transits, {}, 13, 7);
    Require(journey.size() == 2 && journey[0].kind == TransitKind::Flight,
        "An affordable faster route can use the actual transit graph");
    WorldPoint sameMapEnd{17, 100, 0, 0};
    Require(PlanWorldJourney(journeyStart, sameMapEnd, {}, {{journeyStart, sameMapEnd}}, 0, 7).empty(),
        "A rejected walking connection must not be silently reinstated");
    Require(PlanWorldJourney(journeyStart, journeyEnd, transits, {}, 0, 7).empty(),
        "World travel cannot spend more than the available fare budget");
    struct QuestFlagsFixture
    {
        uint32_t flags;
        bool HasSpecialFlag(uint32_t mask) const { return (flags & mask) != 0; }
    };
    // ObjectMgr adds KILL(0x800), CAST(0x20), SPEAKTO(0x400) to the ordinary Cutting Teeth counters.
    Require(SupportedObjectiveFlags(QuestFlagsFixture{0x4 | 0x800 | 0x20 | 0x400}),
        "Synthesized cast-credit flags must not reject an ordinary accepted kill quest");
    Require(!SupportedObjectiveFlags(QuestFlagsFixture{0x2 | 0x800 | 0x20 | 0x400}),
        "Exploration and scripted event requirements still need their own handler");
    Require(AcceptedQuestFirst({{1, false, 80}, {2, true, 300}}) == 2,
        "Accepted field work beats traveling to a new quest even beyond the former half-distance score");
    Require(AcceptedQuestFirst({{2, true, 300}, {1, false, 80}}) == 2,
        "Quest enumeration order must not replace an accepted objective with a new errand");
    Require(AcceptedQuestFirst({{2, true, 300}, {3, true, 200}}) == 3,
        "Within one priority choose the nearest accepted destination");
    auto parentArea = [](uint32_t area) { return area == 363 || area == 362 ? 14u : 0u; };
    Require(RelatedQuestAreas(363, 14, parentArea) && RelatedQuestAreas(14, 363, parentArea),
        "Quest subareas and their enclosing zone must form the same bounded work cluster");
    Require(!RelatedQuestAreas(363, 362, parentArea) && !RelatedQuestAreas(0, 14, parentArea),
        "Unrelated subareas and missing area metadata must not imply the same work cluster");
    std::vector<QuestWorkCandidate> localLeveling{{790, true, true, false, 350, 363, true, 4}};
    Require(NeedsLocalQuestExperience(localLeveling, 1),
        "A nearby accepted level-four objective should prompt local experience at level one before outbound delivery");
    Require(!NeedsLocalQuestExperience(localLeveling, 4), "Reaching the objective's level releases local leveling");
    Require(AutomaticQuestFallback(false, true) == QuestFallback::Blocked,
        "Rejected quest interaction must not silently turn into general grinding");
    Require(AutomaticQuestFallback(true, true) == QuestFallback::Experience,
        "A genuine accepted objective level gate still permits explained experience hunting");
    Require(AutomaticQuestFallback(false, false) == QuestFallback::NoQuest,
        "An exhausted quest catalog permits explained leveling without pretending a quest is incomplete");
    localLeveling[0].distance = 601;
    Require(!NeedsLocalQuestExperience(localLeveling, 1), "Distant hard quests must not trap the current work cluster");
    localLeveling[0].distance = 350;
    localLeveling[0].requiredLevel = 10;
    Require(!NeedsLocalQuestExperience(localLeveling, 1), "Much higher level quests must not force prolonged local grinding");
    Require(LocalQuestReward(300, true) && !LocalQuestReward(1300, true) && !LocalQuestReward(300, false),
        "Nearby in-area rewards can unlock the next local quest before grinding; outbound rewards stay deferred");
    Require(EnemyLevelRangeAllowed(1, 1, 2, 0, 0) && !EnemyLevelAllowed(1, 2, 0, 0),
        "Mixed-level starter spawns can provide local experience without allowing higher-level live pulls");
    Require(EnemyLevelRangeAllowed(7, 4, 6, 0, 0) && !EnemyLevelRangeAllowed(7, 8, 10, 0, 0),
        "Static source ranges must intersect the character's allowed live-target band");
    for (auto const& preset : ClassPresets)
        for (uint32_t race : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 11u})
            Require(SupportedProgression(race, preset.id, 80), "Every playable race/class policy reaches level 80");
    Require(!SupportedProgression(1, 10, 80) && !SupportedProgression(1, 1, 81),
        "Unknown classes and levels outside Wrath must fail closed");
    Require(EnemyLevelAllowed(80, 79, 0, 0) && !EnemyLevelAllowed(80, 6, 0, 0),
        "Automatic hunting bands must follow character level instead of starter mobs");
    Require(!EnemyLevelAllowed(40, 41, 0, 0) && !EnemyLevelAllowed(40, 38, 39, 40),
        "Character safety and explicit level policies must constrain targets");
    Require(TalentAllocationCompatible(2, 3) && !TalentAllocationCompatible(3, 2),
        "Talent development must not erase an incompatible existing allocation");
    Require(EnemyLevelAllowed(40, 5, 0, 0, true) && !EnemyLevelAllowed(40, 5, 30, 0, true),
        "Gray quest objectives are allowed by automatic policy but respect an explicit minimum");
    Require(NeedsRecovery(100, 19, true, 60) && !NeedsRecovery(100, 0, false, 60),
        "Mana classes recover before a pull without treating rage and energy as mana");
    Require(!RecoveryComplete(95, 79, true, 90) && RecoveryComplete(95, 80, true, 90),
        "Mana recovery has a separate resume threshold to prevent repeated short rests");
    Require(!AutomaticQuestLevel(60, 5, false) && AutomaticQuestLevel(60, 5, true),
        "Leveling skips new gray errands while retaining class quests and spell rewards");
    Require(AutomaticQuestLevel(60, 55, false) && !AutomaticQuestLevel(60, 54, false),
        "Automatic quest pickup uses a character-relative level band");
    std::vector<QuestWorkCandidate> work{{7, false, false, true, 400.0f},
        {21, true, true, true, 20.0f}, {33, true, true, true, 40.0f}};
    Require(NearbyQuestWork(work) == 21, "Finish nearby accepted work before returning a completed quest");
    work[1].incomplete = false;
    Require(NearbyQuestWork(work) == 33, "Recheck other nearby objectives after finishing the first quest");
    work[2].allowed = false;
    Require(!NearbyQuestWork(work), "Avoided or unqueued quests must not delay an explicit turn-in");
    work[2].allowed = true;
    work[2].objective = false;
    Require(!NearbyQuestWork(work), "Another ready turn-in must not be mistaken for unfinished field work");
    work[2].objective = true;
    work[2].distance = 121;
    Require(!NearbyQuestWork(work), "Distant unfinished quests must not cause an unrelated detour");
    work[2].distance = std::numeric_limits<float>::quiet_NaN();
    Require(!NearbyQuestWork(work), "Invalid objective positions must not change quest intent");
    work = {{8, true, true, true, 120}, {7, true, true, true, 120}};
    Require(NearbyQuestWork(work) == 7, "Boundary objectives are included with deterministic ties");
    work = {{7, true, true, true, 185.72f, 9}};
    Require(NearbyQuestWork(work, 9) == 7,
        "Finish the kobold quest in the completed wolf quest's data-defined area beyond 120 yards");
    Require(!NearbyQuestWork(work, 12), "A different quest area must not extend the nearby-work limit");
    work[0].sameArea = true;
    Require(NearbyQuestWork(work) == 7, "Current subarea work stays preferred without a previous completed quest");
    work[0].distance = 601;
    Require(!NearbyQuestWork(work, 9), "Area grouping must still bound the detour");
    std::vector<QuestPickupCandidate> pickups{{33, false, true, 0}, {7, true, true, 33.38f}};
    Require(NearbyQuestPickup(pickups) == 7,
        "Pick up the second available quest before leaving the nearby quest givers for the wolves");
    pickups[1].allowed = false;
    Require(!NearbyQuestPickup(pickups), "Avoided and unqueued pickups must not override user intent");
    pickups[1].allowed = true;
    pickups[1].distance = 81;
    Require(!NearbyQuestPickup(pickups), "Distant quest givers do not interrupt the current objective");
    std::vector<uint32_t> drops{100};
    Require(HelpsQuestObjective(5, {5}, {200}, &drops),
        "An unfinished item objective must not hide an unfinished kill objective");
    Require(HelpsQuestObjective(6, {5}, {200, 100}, &drops),
        "Every unfinished item objective can provide useful hunting work");
    Require(!HelpsQuestObjective(6, {5}, {200}, &drops), "Unrelated drops do not satisfy quest planning");
    Require(!HelpsQuestObjective(6, {5}, {100}, nullptr), "Missing drop metadata must fail closed");
    QuestAttempt attempt;
    attempt.Observe(7, {1, 0});
    attempt.stalledSeconds = 600;
    Require(attempt.Failed(), "Waiting at an empty quest spawn must be bounded");
    attempt.Observe(7, {1, 1});
    Require(!attempt.Failed() && !attempt.stalledSeconds, "Actual objective progress resets the stall limit");
    attempt.failures = 3;
    Require(attempt.Failed(), "Repeated rejected interactions must be bounded");
    attempt.Observe(7, {1, 1});
    Require(attempt.Failed(), "Travel and retries without progress must not clear interaction failures");
    attempt.Observe(21, {1, 0});
    Require(!attempt.Failed(), "A different quest starts with its own retry allowance");
    Model model;
    Require(model.profile.values[FoodItem] == 0, "New profiles discover suitable food instead of requiring one vendor item");
    model.profile.values[FoodItem] = 2070;
    model.profile.protectedItems = {6948, 117};
    model.goals = {{0, 6, 1, 1, 3600, 120, 1}, {1, 2589, 20, 1, 0, 0, 2}};
    model.nextGoalId = 3;
    Require(Valid(model.profile), "Preset must validate");
    auto highLevelBudget = model.profile;
    highLevelBudget.values[VisitBudget] = 50000000;
    Require(Valid(highLevelBudget), "Wrath riding costs must fit the configurable service budget");
    highLevelBudget.values[VisitBudget] = MaxMoney + 1;
    Require(!Valid(highLevelBudget), "Service policies must remain within the core money limit");
    Require(!CanSell(6948, 0, false, model.profile, model.goals), "Explicit protection beats selling");
    Require(!CanSell(2589, 0, false, model.profile, model.goals), "Goal materials must not be sold");
    Require(!CanSell(2070, 0, false, model.profile, model.goals), "Reserved food must not be sold");
    Require(!CanSell(25, 0, true, model.profile, model.goals), "Equipment/quest protection beats quality");
    Require(CanSell(7073, 0, false, model.profile, model.goals), "Ordinary grey loot can be sold");
    Require(!CanSell(25, 1, false, model.profile, model.goals), "Default must not sell white equipment");
    Require(Spendable(50, 100, 500, 0) == 0, "Reserve cannot underflow");
    Require(Spendable(1000, 100, 500, 490) == 10, "Visit budget must constrain spending");
    Require(Spendable(1000, 100, 500, 501) == 0, "Spent budget cannot underflow");
    Model restored;
    auto encoded = Encode(model);
    Require(Decode(encoded, restored) && Encode(restored) == encoded, "Persistence must round trip goals and rules");
    encoded.pop_back();
    Require(!Decode(encoded, restored), "Truncated saves must be rejected atomically");
    Require(Encode(restored) == Encode(model), "Bad saves must not replace the current profile");
    encoded = Encode(model);
    encoded[0] = ProfileSchema + 1;
    Require(!Decode(encoded, restored), "Unknown save versions must fail closed");
    auto legacy = Encode(model);
    legacy[0] = 1;
    legacy.resize(legacy.size() - 8);
    legacy.erase(legacy.end() - 1);
    legacy.erase(legacy.end() - 7);
    Require(Decode(legacy, restored), "Valid v1 profiles must migrate");
    Require(restored.goals[0].id == 1 && restored.goals[1].id == 2 && restored.nextGoalId == 3,
        "Migration assigns distinct persistent goal identities");
    Require(restored.profile.protectedItems == model.profile.protectedItems,
        "Migration preserves item protection");
    auto duplicate = Encode(model);
    size_t goalStart = 2 + SettingCount + MaxRules * 5 + 1 + model.profile.protectedItems.size() + 1;
    duplicate[goalStart + 7 + 6] = 1;
    Require(!Decode(duplicate, restored), "Duplicate goal identities must be rejected");
    auto regressed = Encode(model);
    regressed[regressed.size() - 3] = 2;
    Require(!Decode(regressed, restored), "Next identity must exceed every saved identity");
    Model preferences = model;
    preferences.profile.avoidedQuests = {7, 21};
    preferences.profile.autoTrain = 0;
    preferences.profile.autoTalents = 0;
    preferences.profile.classPreset = 0;
    preferences.profile.lockedSlots = (1u << 15) | (1u << 20);
    Require(Decode(Encode(preferences), restored) && Encode(preferences) == Encode(restored),
        "Quest avoidance, training policy, and equipment locks must persist");
    Require(Valid(Goal{2, 7, 1, 1, 0, 0, 3}), "Selected quest goals validate");
    Require(Valid(Goal{3, 2, 1, 1, 0, 0, 3}), "Named travel goals validate");
    Require(!Valid(Goal{3, 3, 1, 1, 0, 0, 3}), "Unknown travel destinations must fail closed");
    Require(Valid(Goal{4, 1, 1, 1, 0, 0, 3}), "Training goals validate");
    Require(Valid(Goal{5, 12345, 1, 1, 0, 0, 3}), "Travel accepts a world-spawn destination identity");
    Require(!Valid(Goal{5, 12345, 2, 1, 0, 0, 3}), "Travel destinations do not accept item quantities");
    Profile invalid = model.profile;
    invalid.values[ResumeHealth] = invalid.values[RecoverHealth];
    Require(!Valid(invalid), "Recovery thresholds need hysteresis");
    Require(Valid(Goal{0, 80, 1, 1, 0, 0}), "Progression goals must support level 80");
    Require(!Valid(Goal{0, 81, 1, 1, 0, 0}), "Goals outside Wrath progression must fail closed");
    auto versionTwo = Encode(model);
    versionTwo[0] = 2;
    versionTwo.resize(versionTwo.size() - 2);
    Require(Decode(versionTwo, restored) && restored.profile.autoTalents && restored.profile.classPreset,
        "V2 profiles migrate with class development enabled while preserving their policies");
    Require(restored.profile.protectedItems == model.profile.protectedItems,
        "V2 migration must preserve protected items");
    versionTwo[2 + VisitBudget] = 500;
    Require(Decode(versionTwo, restored) && restored.profile.values[VisitBudget] == 500,
        "Existing spending limits must survive the progression update");
    versionTwo[2 + MinEnemyLevel] = 1;
    versionTwo[2 + MaxEnemyLevel] = 6;
    Require(Decode(versionTwo, restored) && !restored.profile.values[MinEnemyLevel] &&
        !restored.profile.values[MaxEnemyLevel], "Legacy starter target defaults migrate to character-level targeting");
    versionTwo[2 + MinEnemyLevel] = 20;
    versionTwo[2 + MaxEnemyLevel] = 25;
    Require(Decode(versionTwo, restored) && restored.profile.values[MinEnemyLevel] == 20 &&
        restored.profile.values[MaxEnemyLevel] == 25, "Explicit non-default target limits survive migration");
    Require(!Valid(Goal{1, 2589, 0, 1, 0, 0}), "Zero item stock target is invalid");
    std::vector<uint32_t> numbers;
    Require(!Numbers("1 -1", numbers), "Negative arguments must not wrap");
    Require(!Numbers("4294967296", numbers), "Overflow must be rejected");
    Require(!Numbers("1 2x", numbers), "Trailing garbage must be rejected");
    Require(!Numbers("1 2 3", numbers, 2), "Input must be bounded");
    Require(Numbers("0 4294967295", numbers) && numbers.size() == 2, "Full uint32 range must parse");

    struct Point
    {
        float x, y, z;
    };
    Point destination{};
    std::vector<Point> route{{-8900.0f, -100.0f, 80.0f}, {-8900.0f, -50.0f, 80.0f},
        {-8800.0f, -50.0f, 80.0f}};
    Require(NextWalkingLeg(route, destination) && destination.x == -8870.0f && destination.y == -50.0f,
        "Walking legs must follow route corners before interpolating the remaining distance");
    route = {{-8900.0f, -100.0f, 80.0f}, {-8880.0f, -100.0f, 80.0f}};
    Require(NextWalkingLeg(route, destination) && destination.x == -8880.0f,
        "Short walking legs must retain their exact destination");
    route = {{-8900.0f, -100.0f, 80.0f}, {-8900.0f, -100.0f, 180.0f}};
    Require(NextWalkingLeg(route, destination) && destination.z == 160.0f,
        "Walking leg length must include elevation changes");
    route = {{-8900.0f, -100.0f, 80.0f}, {-8800.0f, -100.0f, 80.0f}, {-8600.0f, -100.0f, 80.0f}};
    Require(NextWalkingLeg(route, destination), "Finite routes must not depend on an authored area rectangle");
    route = {{1600.0f, 900.0f, 40.0f}, {1620.0f, 920.0f, 40.0f}};
    Require(NextWalkingLeg(route, destination) && destination.x == 1620.0f,
        "The same walking policy must support unrelated world coordinates");
    route = {{-8900.0f, -100.0f, 80.0f}, {-8900.0f, -100.0f, 80.0f}};
    Require(!NextWalkingLeg(route, destination), "A stationary route must not launch movement");
    route.back().z = std::numeric_limits<float>::quiet_NaN();
    Require(!NextWalkingLeg(route, destination), "Non-finite navigation coordinates must fail closed");
    Require(!NextWalkingLeg(std::vector<Point>{}, destination), "Empty routes must fail closed");
    Require(!FinitePosition(std::numeric_limits<float>::infinity(), 0, 0),
        "Non-finite world coordinates must still fail closed");
    std::cout << "WrathIdle policy checks passed\n";
}
