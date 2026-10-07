#pragma once

#include <array>
#include <span>

namespace AQT
{
    struct Settings;

    float UpdateChickenGuide(RE::NiNode* parent, std::span<const std::array<float, 3>> route,
        float playerDistance, float delta, const Settings& settings, bool visible);
    void ResetChickenGuide(bool releaseModel = false);
}
