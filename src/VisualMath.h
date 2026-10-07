#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace AQT
{
    struct RouteProgress
    {
        float distanceSquared{std::numeric_limits<float>::max()};
        float remaining{0.0f};
        float travelled{0.0f};
    };

    inline RouteProgress MeasureRoute(std::span<const std::array<float, 3>> points, const std::array<float, 3>& position)
    {
        RouteProgress result;
        float remaining{0.0f};
        for (std::size_t i = points.size(); i > 0; --i) {
            const auto& end = points[i - 1];
            const auto& start = points[i > 1 ? i - 2 : 0];
            const std::array direction{end[0] - start[0], end[1] - start[1], end[2] - start[2]};
            float lengthSquared{0.0f};
            float projection{0.0f};
            for (unsigned axis = 0; axis < 3; ++axis) {
                lengthSquared += direction[axis] * direction[axis];
                projection += (position[axis] - start[axis]) * direction[axis];
            }
            const float t = lengthSquared > 0.0f ? std::clamp(projection / lengthSquared, 0.0f, 1.0f) : 0.0f;
            float distanceSquared{0.0f};
            for (unsigned axis = 0; axis < 3; ++axis) {
                const float offset = position[axis] - start[axis] - t * direction[axis];
                distanceSquared += offset * offset;
            }
            const float length = std::sqrt(lengthSquared);
            if (distanceSquared <= result.distanceSquared) {
                result = {distanceSquared, remaining + (1.0f - t) * length};
            }
            remaining += length;
        }
        result.travelled = remaining - result.remaining;
        return result;
    }

    struct RouteSample
    {
        std::array<float, 3> position{};
        std::array<float, 3> direction{0.0f, 1.0f, 0.0f};
    };

    inline RouteSample SampleRoute(std::span<const std::array<float, 3>> points, float distance)
    {
        RouteSample result;
        if (points.empty()) {
            return result;
        }
        result.position = points.front();
        distance = std::max(0.0f, distance);
        for (std::size_t i = 1; i < points.size(); ++i) {
            const auto& start = points[i - 1];
            const auto& end = points[i];
            const std::array direction{end[0] - start[0], end[1] - start[1], end[2] - start[2]};
            const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
            if (length <= 0.001f) {
                continue;
            }
            const float t = std::min(distance / length, 1.0f);
            for (unsigned axis = 0; axis < 3; ++axis) {
                result.position[axis] = start[axis] + direction[axis] * t;
                result.direction[axis] = direction[axis] / length;
            }
            if (distance <= length) {
                break;
            }
            distance -= length;
        }
        return result;
    }

    class RouteProgressCache
    {
    public:
        const RouteProgress& Get(std::span<const std::array<float, 3>> points, const std::array<float, 3>& position)
        {
            if (!valid || position != previousPosition) {
                progress = MeasureRoute(points, position);
                previousPosition = position;
                valid = true;
            }
            return progress;
        }

        void Invalidate() { valid = false; }

    private:
        RouteProgress progress;
        std::array<float, 3> previousPosition{};
        bool valid{false};
    };

    inline bool NeedsAnchoredRoute(const RouteProgress& progress, float offRouteDistance, float extendDistance)
    {
        const float lead = std::min(extendDistance, (progress.remaining + progress.travelled) * 0.5f);
        return progress.distanceSquared >= offRouteDistance * offRouteDistance ||
            (progress.remaining <= lead && progress.travelled >= 128.0f);
    }

    inline float AdvanceFade(float alpha, float target, float delta, float seconds)
    {
        const float step = std::clamp(delta, 0.0f, 0.1f) / seconds;
        return alpha + std::clamp(std::clamp(target, 0.0f, 1.0f) - alpha, -step, step);
    }

    inline float FadeOpacity(float alpha)
    {
        return alpha * alpha * (3.0f - 2.0f * alpha);
    }

    inline float SmoothVisibility(float distance)
    {
        const float t = std::clamp(distance / 64.0f, 0.0f, 1.0f);
        return FadeOpacity(t);
    }

    inline float RouteVisibility(float sampleDistance, float travelled)
    {
        return SmoothVisibility(sampleDistance - travelled);
    }

    inline float PlayerClearance(float x, float y, float startDistance, float speed, float fadeDistance = 64.0f)
    {
        const float margin = startDistance + 88.0f + std::min(speed * 0.08f, 64.0f);
        float distance = std::hypot(x, y) - margin;
        return FadeOpacity(std::clamp(distance / fadeDistance, 0.0f, 1.0f));
    }
}
