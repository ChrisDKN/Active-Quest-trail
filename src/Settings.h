#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace AQT
{
    struct Settings
    {
        bool enabled{true};
        int toggleKey{0};
        int toggleModifier{0};
        int trailStyle{0};
        float chickenDistance{500.0f};
        bool chickenTrail{false};
        bool hideIndoors{false};
        bool hideDungeons{false};
        bool hideInCombat{false};
        bool customColour{false};
        std::array<float, 3> colour{1.0f, 1.0f, 1.0f};
        float brightness{1.0f};
        float opacity{1.0f};
        float height{20.0f};
        float startDistance{64.0f};
        bool glowDoor{true};
        bool animate{true};
        bool particles{true};
        float animationSpeed{1.0f};
        float fadeSeconds{0.5f};
        bool trailLights{true};
        float lightBrightness{2.0f};
        float lightRadius{280.0f};
        float trailLength{6000.0f};
        bool anchorTrail{true};
        float offRouteDistance{256.0f};
        float extendDistance{2000.0f};
        bool refreshWhileMoving{true};
        float refreshSeconds{2.0f};
    };

    Settings GetSettings();
    void SetSettings(Settings settings);
    void ToggleEnabled();
    void LoadSettings();
    bool SaveSettings();
    void SavePendingSettings(bool force = false);
    std::uint64_t SettingsRevision();
    void RegisterMenu();
    void SetStatus(std::string status, std::size_t segments = 0);
    void RequestRefresh();
}
