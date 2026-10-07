#pragma once

namespace AQT
{
    struct Settings;

    void InitializeDestinationGlow(RE::TESDataHandler* data);
    RE::ObjectRefHandle FindRouteDestination(RE::PlayerCharacter* player, RE::GuideEffect* effect);
    void SetGlowDestination(RE::ObjectRefHandle target);
    void UpdateDestinationGlow(RE::PlayerCharacter* player, const Settings& settings, bool routeVisible);
    void ResetDestinationGlow(bool clearRestored = false);
}
