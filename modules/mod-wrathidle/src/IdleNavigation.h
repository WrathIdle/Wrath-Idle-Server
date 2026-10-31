#pragma once

#include <cmath>

namespace WrathIdle
{
inline bool FinitePosition(float x, float y, float z)
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

// Plan against the entire route, then follow a short leg with a normal smooth path.
// Corner paths are planning data only; they must never be passed directly to movement.
template <class Points>
bool NextWalkingLeg(Points const& points, typename Points::value_type& destination)
{
    if (points.size() < 2)
        return false;
    for (auto const& point : points)
        if (!FinitePosition(point.x, point.y, point.z))
            return false;

    float remaining = 80.0f;
    auto previous = points.front();
    for (auto const& point : points)
    {
        float dx = point.x - previous.x;
        float dy = point.y - previous.y;
        float dz = point.z - previous.z;
        float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(length))
            return false;
        if (length > remaining)
        {
            float ratio = remaining / length;
            destination = previous;
            destination.x += dx * ratio;
            destination.y += dy * ratio;
            destination.z += dz * ratio;
            return true;
        }
        remaining -= length;
        previous = point;
    }
    destination = points.back();
    return remaining < 80.0f;
}
}
