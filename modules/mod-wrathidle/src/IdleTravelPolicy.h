#pragma once

#include "IdleNavigation.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <tuple>
#include <vector>

namespace WrathIdle
{
struct WorldPoint
{
    uint32_t map = 0;
    float x = 0, y = 0, z = 0;
};

inline bool ValidWorldPoint(WorldPoint const& point)
{
    return FinitePosition(point.x, point.y, point.z);
}

inline float WorldDistance(WorldPoint const& a, WorldPoint const& b)
{
    if (a.map != b.map || !ValidWorldPoint(a) || !ValidWorldPoint(b))
        return std::numeric_limits<float>::infinity();
    float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

enum class TransitKind { Walk, AreaPortal, ObjectPortal, Flight };

struct TransitLink
{
    TransitKind kind = TransitKind::Walk;
    WorldPoint from, to;
    uint32_t id = 0;
    uint32_t source = 0;
    uint32_t destination = 0;
    uint32_t price = 0;
    float seconds = 0;
};

struct BlockedWalk
{
    WorldPoint from, to;
};

// Walking edges are proposals. The ordinary complete navmesh planner validates every executed leg.
inline std::vector<TransitLink> PlanWorldJourney(WorldPoint start, WorldPoint goal,
    std::vector<TransitLink> const& links, std::vector<BlockedWalk> const& blocked, uint32_t budget, float speed)
{
    if (!ValidWorldPoint(start) || !ValidWorldPoint(goal) || !std::isfinite(speed) || speed <= 0 || links.size() > 4096)
        return {};
    std::vector<WorldPoint> nodes;
    std::map<std::tuple<uint32_t, float, float, float>, size_t> index;
    auto add = [&](WorldPoint point)
    {
        auto [it, inserted] = index.try_emplace({point.map, point.x, point.y, point.z}, nodes.size());
        if (inserted)
            nodes.push_back(point);
        return it->second;
    };
    size_t first = add(start), last = add(goal);
    if (first == last)
        return {TransitLink{TransitKind::Walk, start, goal}};
    struct Arc { size_t target; TransitLink link; };
    std::map<size_t, std::vector<Arc>> arcs;
    for (auto const& link : links)
    {
        if (link.kind == TransitKind::Walk || !ValidWorldPoint(link.from) || !ValidWorldPoint(link.to) ||
            !std::isfinite(link.seconds) || link.seconds < 0)
            continue;
        size_t from = add(link.from), to = add(link.to);
        arcs[from].push_back({to, link});
    }
    if (nodes.size() > 512)
        return {};
    struct Label
    {
        size_t node, parent;
        float seconds;
        uint32_t price;
        TransitLink hop;
        bool dominated = false;
    };
    size_t const none = std::numeric_limits<size_t>::max();
    std::vector<Label> labels{{first, none, 0, 0, {}}};
    std::vector<std::vector<size_t>> frontier(nodes.size());
    frontier[first].push_back(0);
    using QueueItem = std::pair<float, size_t>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
    queue.emplace(0, 0);
    while (!queue.empty() && labels.size() < 4096)
    {
        auto [seconds, current] = queue.top();
        queue.pop();
        Label base = labels[current];
        if (base.dominated)
            continue;
        if (base.node == last)
        {
            std::vector<TransitLink> result;
            for (size_t label = current; labels[label].parent != none; label = labels[label].parent)
                result.push_back(labels[label].hop);
            std::reverse(result.begin(), result.end());
            return result;
        }
        auto visit = [&](size_t to, TransitLink hop)
        {
            if (hop.price > budget - base.price)
                return;
            uint32_t price = base.price + hop.price;
            float total = seconds + hop.seconds;
            if (!std::isfinite(total))
                return;
            for (size_t other : frontier[to])
                if (!labels[other].dominated && labels[other].price <= price && labels[other].seconds <= total)
                    return;
            for (size_t other : frontier[to])
                if (price <= labels[other].price && total <= labels[other].seconds)
                    labels[other].dominated = true;
            if (labels.size() >= 4096)
                return;
            frontier[to].push_back(labels.size());
            queue.emplace(total, labels.size());
            labels.push_back({to, current, total, price, hop});
        };
        for (size_t to = 0; to < nodes.size(); ++to)
        {
            float distance = WorldDistance(nodes[base.node], nodes[to]);
            if (to == base.node || !std::isfinite(distance))
                continue;
            bool denied = std::any_of(blocked.begin(), blocked.end(), [&](auto const& edge)
            {
                return WorldDistance(edge.from, nodes[base.node]) < 3 && WorldDistance(edge.to, nodes[to]) < 3;
            });
            if (!denied)
                visit(to, {TransitKind::Walk, nodes[base.node], nodes[to], 0, 0, 0, 0, distance / speed});
        }
        for (auto const& arc : arcs[base.node])
            visit(arc.target, arc.link);
    }
    return {};
}
}
