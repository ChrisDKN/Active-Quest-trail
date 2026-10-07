#pragma once

namespace AQT
{
    struct Settings;

    bool IsBindableKey(int key);
    bool RenderKeybindSettings(Settings& settings);
    void RegisterKeybindMenu();
    void RegisterToggleInput();
}
