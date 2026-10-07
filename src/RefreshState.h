#pragma once

#include <algorithm>

namespace AQT
{
    struct RefreshState
    {
        static constexpr float minimumInterval = 0.2f;
        static constexpr float movementDistanceSquared = 32.0f * 32.0f;
        static constexpr float timeout = 2.0f;

        int active{-1};
        int pending{-1};
        float sinceRequest{0.0f};
        float pendingAge{0.0f};
        float retryDelay{0.0f};

        void Advance(float delta)
        {
            sinceRequest += delta;
            if (pending >= 0) {
                pendingAge += delta;
            }
            retryDelay = std::max(0.0f, retryDelay - delta);
        }

        bool Due(bool needsRoute) const
        {
            return pending < 0 && retryDelay <= 0.0f &&
                (active < 0 || (needsRoute && sinceRequest >= minimumInterval));
        }

        bool Due(float distanceSquared, bool movingRefresh, float idleInterval) const
        {
            return Due(sinceRequest >= idleInterval || (movingRefresh && distanceSquared >= movementDistanceSquared));
        }

        int Begin()
        {
            pending = active == 0 ? 1 : 0;
            sinceRequest = 0.0f;
            pendingAge = 0.0f;
            return pending;
        }

        int Commit()
        {
            const int previous = active;
            active = pending;
            pending = -1;
            return previous;
        }

        bool TimedOut() const
        {
            return pending >= 0 && pendingAge >= timeout;
        }

        void Reset()
        {
            *this = {};
        }
    };
}
