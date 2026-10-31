#pragma once

#include "IdleNavigation.h"
#include "DetourNavMeshQuery.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

namespace WrathIdle
{
struct RoutePoint
{
    float x, y, z;
};

struct RouteDiagnostic
{
    char const* stage = "initialize";
    dtStatus status = DT_FAILURE;
    int polygons = 0;
    int corners = 0;
    float surfaceGap = 0.0f;
};

inline bool CompleteQuery(dtStatus status)
{
    return dtStatusSucceed(status) &&
        !dtStatusDetail(status, DT_PARTIAL_RESULT | DT_BUFFER_TOO_SMALL | DT_OUT_OF_NODES);
}

inline bool ProjectCorridorSurface(dtNavMeshQuery const& query, dtPolyRef const* corridor, int count,
    RoutePoint const& point, RoutePoint& projected, float& surfaceGap)
{
    if (!FinitePosition(point.x, point.y, point.z))
        return false;
    float legPoint[]{point.y, point.z, point.x};
    float closest = std::numeric_limits<float>::infinity();
    float gap = std::numeric_limits<float>::infinity();
    for (int i = 0; i < count; ++i)
    {
        float surface[3];
        if (!CompleteQuery(query.closestPointOnPoly(corridor[i], legPoint, surface, nullptr)) ||
            !FinitePosition(surface[0], surface[1], surface[2]))
            continue;
        float dx = surface[0] - legPoint[0], dy = surface[2] - legPoint[2];
        float lateral = dx * dx + dy * dy;
        gap = std::min(gap, lateral);
        if (lateral > 0.01f * 0.01f)
            continue;
        float height = surface[1] - legPoint[1];
        float difference = lateral + height * height;
        if (difference < closest)
        {
            closest = difference;
            projected = {surface[2], surface[0], surface[1]};
        }
    }
    surfaceGap = std::sqrt(gap);
    return std::isfinite(closest);
}

inline bool PlanNavmeshLeg(dtNavMesh const* mesh, RoutePoint const& start, RoutePoint const& goal,
    dtQueryFilter const& filter, RoutePoint& next, RouteDiagnostic& diagnostic, int searchNodes = 8192)
{
    diagnostic = {};
    if (!mesh || !FinitePosition(start.x, start.y, start.z) || !FinitePosition(goal.x, goal.y, goal.z))
        return false;

    // Keep the long-route search budget local to WrathIdle. The shared map query is tuned for short moves.
    std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query(dtAllocNavMeshQuery(), dtFreeNavMeshQuery);
    if (!query)
        return false;
    diagnostic.status = query->init(mesh, searchNodes);
    if (!CompleteQuery(diagnostic.status))
        return false;

    float from[]{start.y, start.z, start.x};
    float to[]{goal.y, goal.z, goal.x};
    float extents[]{3.0f, 5.0f, 3.0f};
    float projectedStart[3], projectedEnd[3];
    dtPolyRef startPoly = 0, endPoly = 0;
    diagnostic.stage = "start polygon";
    diagnostic.status = query->findNearestPoly(from, extents, &filter, &startPoly, projectedStart);
    if (!CompleteQuery(diagnostic.status) || !startPoly)
        return false;
    diagnostic.stage = "end polygon";
    diagnostic.status = query->findNearestPoly(to, extents, &filter, &endPoly, projectedEnd);
    if (!CompleteQuery(diagnostic.status) || !endPoly)
        return false;

    std::array<dtPolyRef, 4096> corridor;
    diagnostic.stage = "corridor";
    diagnostic.status = query->findPath(startPoly, endPoly, projectedStart, projectedEnd, &filter,
        corridor.data(), &diagnostic.polygons, static_cast<int>(corridor.size()));
    if (!CompleteQuery(diagnostic.status) || !diagnostic.polygons || corridor[diagnostic.polygons - 1] != endPoly)
        return false;

    std::array<float, 1024 * 3> corners;
    diagnostic.stage = "corners";
    diagnostic.status = query->findStraightPath(projectedStart, projectedEnd, corridor.data(), diagnostic.polygons,
        corners.data(), nullptr, nullptr, &diagnostic.corners, static_cast<int>(corners.size() / 3));
    if (!CompleteQuery(diagnostic.status))
        return false;
    std::vector<RoutePoint> points;
    for (int i = 0; i < diagnostic.corners; ++i)
        points.push_back({corners[i * 3 + 2], corners[i * 3], corners[i * 3 + 1]});
    diagnostic.stage = "bounded leg";
    if (!NextWalkingLeg(points, next))
        return false;

    // Corners can lie exactly on polygon edges. Interpolation roundoff can put a step just outside them.
    // Use the corridor's actual detail surface, admitting at most a tiny lateral correction to its boundary.
    diagnostic.stage = "leg surface";
    RoutePoint proposed = next;
    return ProjectCorridorSurface(*query, corridor.data(), diagnostic.polygons, proposed, next, diagnostic.surfaceGap);
}
}
