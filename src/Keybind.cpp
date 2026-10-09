#include "Keybind.h"
#include "Settings.h"
#include "Translations.h"
#include "SKSEMenuFramework.h"

#include <atomic>

namespace AQT
{
    namespace
    {
        using Key = RE::BSKeyboardDevice::Key;
        std::atomic<bool> capturing{false};
        std::atomic<int> capturedKey{0};
        enum class Binding { Toggle, Hold, Timed };
        Binding captureBinding{Binding::Toggle};
        std::atomic<float> timedRemaining{0.0f};
        bool menuAvailable{false};

        bool IsModifier(int key)
        {
            return key == Key::kLeftControl || key == Key::kRightControl ||
                   key == Key::kLeftShift || key == Key::kRightShift ||
                   key == Key::kLeftAlt || key == Key::kRightAlt ||
                   key == Key::kLeftWin || key == Key::kRightWin;
        }

        bool MenuOpen()
        {
            if (!menuAvailable) {
                return false;
            }
            auto* window = SKSEMenuFramework::GetMainWindow();
            return window && window->IsOpen.load();
        }

        std::string KeyName(int key)
        {
            if (!key) {
                return Translations::Get("Unbound");
            }
            switch (key) {
            case Key::kPause: return Translations::Get("KeyPause");
            case Key::kNumLock: return Translations::Get("KeyNumLock");
            case Key::kPrintScreen: return Translations::Get("KeyPrintScreen");
            default: break;
            }
            wchar_t name[64]{};
            const auto scanCode = (key & 0x7f) << 16;
            const auto extended = (key & 0x80) ? 1 << 24 : 0;
            const auto length = GetKeyNameTextW(scanCode | extended, name, 64);
            char utf8[256]{};
            if (length > 0 && WideCharToMultiByte(CP_UTF8, 0, name, length, utf8, 255, nullptr, nullptr) > 0) {
                return utf8;
            }
            return Translations::Format("KeyNumber", key);
        }

        bool CaptureInput(RE::InputEvent* event)
        {
            if (!capturing.load()) {
                return false;
            }
            if (!MenuOpen()) {
                capturing = false;
                capturedKey = 0;
                return false;
            }
            auto* button = event ? event->AsButtonEvent() : nullptr;
            if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                return false;
            }
            const auto key = static_cast<int>(button->GetIDCode());
            if (button->IsDown()) {
                int empty = 0;
                if (key == Key::kEscape) {
                    capturedKey.compare_exchange_strong(empty, -1);
                } else if (IsBindableKey(key)) {
                    capturedKey.compare_exchange_strong(empty, key);
                }
            }
            return true;
        }

        bool CanUseKeybind()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            auto* controls = RE::ControlMap::GetSingleton();
            if (!player || !player->Is3DLoaded() || !ui || !controls || MenuOpen() ||
                (menuAvailable && SKSEMenuFramework::IsAnyBlockingWindowOpened()) ||
                ui->GameIsPaused() || ui->IsApplicationMenuOpen() || ui->IsItemMenuOpen() ||
                ui->IsModalMenuOpen() || ui->IsMenuOpen(RE::Console::MENU_NAME) ||
                ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
                return false;
            }
            const auto& data = controls->GetRuntimeData();
            return data.textEntryCount == 0 &&
                   (data.contextPriorityStack.empty() || data.contextPriorityStack.back() == RE::UserEvents::INPUT_CONTEXT_ID::kGameplay);
        }

        bool KeyHeld(int key)
        {
            auto* input = RE::BSInputDeviceManager::GetSingleton();
            auto* keyboard = input ? input->GetKeyboard() : nullptr;
            return key > 0 && key < 256 && keyboard && (keyboard->GetRuntimeData().curState[key] & 0x80) != 0;
        }

        bool ModifierHeld(int modifier)
        {
            if (!modifier) {
                return true;
            }
            auto* input = RE::BSInputDeviceManager::GetSingleton();
            auto* keyboard = input ? input->GetKeyboard() : nullptr;
            if (!keyboard) {
                return false;
            }
            const auto& state = keyboard->GetRuntimeData().curState;
            const auto held = [&state](Key key) { return (state[key] & 0x80) != 0; };
            switch (modifier) {
            case 1: return held(Key::kLeftControl) || held(Key::kRightControl);
            case 2: return held(Key::kLeftShift) || held(Key::kRightShift);
            case 3: return held(Key::kLeftAlt) || held(Key::kRightAlt);
            default: return false;
            }
        }

        bool RenderBinding(const char* name, int& key, int& modifier, Binding binding)
        {
            ImGuiMCP::PushID(name);
            const char* modifiers[]{Translations::Get("ModifierNone"), Translations::Get("ModifierCtrl"), Translations::Get("ModifierShift"), Translations::Get("ModifierAlt")};
            ImGuiMCP::SetNextItemWidth(ImGuiMCP::GetFontSize() * 6.0f);
            bool changed = ImGuiMCP::Combo("##Modifier", &modifier, modifiers, 4);
            if (ImGuiMCP::IsItemHovered()) {
                ImGuiMCP::SetTooltip("%s", Translations::Get("Modifier"));
            }
            ImGuiMCP::SameLine();
            const auto label = KeyName(key) + "###Key";
            const bool choose = ImGuiMCP::Button(label.c_str());
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button(Translations::Label("Clear"))) {
                key = 0;
                changed = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::TextUnformatted(Translations::Get(name));
            ImGuiMCP::PopID();
            if (choose) {
                capturedKey = 0;
                captureBinding = binding;
                capturing = true;
                ImGuiMCP::OpenPopup(Translations::Label("ChooseTrailKey"));
            }
            return changed;
        }

        class TrailInput final : public RE::BSTEventSink<RE::InputEvent*>
        {
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override
            {
                if (capturing.load() && !MenuOpen()) {
                    capturing = false;
                    capturedKey = 0;
                }
                if (!events) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                for (auto* event = *events; event; event = event->next) {
                    auto* button = event->AsButtonEvent();
                    if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !button->IsDown()) {
                        continue;
                    }
                    const auto settings = GetSettings();
                    if (capturing.load() || !CanUseKeybind()) {
                        continue;
                    }
                    const auto key = button->GetIDCode();
                    if (settings.toggleKey && key == static_cast<std::uint32_t>(settings.toggleKey) &&
                        ModifierHeld(settings.toggleModifier)) {
                        ToggleEnabled();
                    } else if (settings.enabled && settings.timedShow && settings.timedKey &&
                               key == static_cast<std::uint32_t>(settings.timedKey) && ModifierHeld(settings.timedModifier)) {
                        timedRemaining = settings.showSeconds;
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    bool IsBindableKey(int key)
    {
        return key > Key::kEscape && key < 256 && !IsModifier(key);
    }

    bool RenderKeybindSettings(Settings& settings)
    {
        bool changed = RenderBinding("ToggleKey", settings.toggleKey, settings.toggleModifier, Binding::Toggle);
        if (ImGuiMCP::Checkbox(Translations::Label("HoldToShowTheTrail"), &settings.holdToShow)) {
            if (settings.holdToShow) {
                settings.timedShow = false;
            }
            changed = true;
        }
        changed |= RenderBinding("HoldKey", settings.holdKey, settings.holdModifier, Binding::Hold);
        if (ImGuiMCP::Checkbox(Translations::Label("PressToShowTheTrailTemporarily"), &settings.timedShow)) {
            if (settings.timedShow) {
                settings.holdToShow = false;
            }
            changed = true;
        }
        changed |= RenderBinding("TimedKey", settings.timedKey, settings.timedModifier, Binding::Timed);
        changed |= ImGuiMCP::SliderFloat(Translations::Label("ShowDuration"), &settings.showSeconds, 1.0f, 120.0f, Translations::Get("ShowSecondsFormat"));
        ImGuiMCP::TextWrapped("%s", Translations::Get("VisibilityModeHelp"));
        const int showKey = settings.holdToShow ? settings.holdKey : settings.timedShow ? settings.timedKey : 0;
        const int showModifier = settings.holdToShow ? settings.holdModifier : settings.timedModifier;
        if (showKey && showKey == settings.toggleKey &&
            (showModifier == settings.toggleModifier || !showModifier || !settings.toggleModifier)) {
            ImGuiMCP::TextWrapped("%s", Translations::Get("KeyConflictHelp"));
        }
        if (ImGuiMCP::BeginPopupModal(Translations::Label("ChooseTrailKey"), nullptr, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
            const char* name = captureBinding == Binding::Toggle ? "ToggleKey" : captureBinding == Binding::Hold ? "HoldKey" : "TimedKey";
            ImGuiMCP::Text("%s", Translations::Get(name));
            ImGuiMCP::TextUnformatted(Translations::Get("ChooseKeyHelp"));
            const auto key = capturedKey.exchange(0);
            const bool cancel = ImGuiMCP::Button(Translations::Label("Cancel"));
            if (key != 0 || cancel || !capturing.load()) {
                if (key > 0 && !cancel) {
                    auto& binding = captureBinding == Binding::Toggle ? settings.toggleKey :
                        captureBinding == Binding::Hold ? settings.holdKey : settings.timedKey;
                    binding = key;
                    changed = true;
                }
                capturing = false;
                ImGuiMCP::CloseCurrentPopup();
            }
            ImGuiMCP::EndPopup();
        } else {
            capturing = false;
        }
        return changed;
    }

    void RegisterKeybindMenu()
    {
        menuAvailable = true;
        static auto* input = SKSEMenuFramework::AddInputEvent(CaptureInput);
        (void)input;
    }

    void RegisterTrailInput()
    {
        static TrailInput input;
        if (auto* manager = RE::BSInputDeviceManager::GetSingleton()) {
            manager->AddEventSink(&input);
        }
    }

    bool UpdateTrailVisibility(const Settings& settings, float delta)
    {
        if (!settings.enabled) {
            ResetTrailVisibility();
            return false;
        }
        if (settings.holdToShow) {
            return !capturing.load() && CanUseKeybind() && KeyHeld(settings.holdKey) && ModifierHeld(settings.holdModifier);
        }
        if (!settings.timedShow) {
            return true;
        }
        if (!settings.timedKey) {
            ResetTrailVisibility();
            return false;
        }
        auto remaining = timedRemaining.load();
        while (remaining > 0.0f) {
            const auto next = std::max(0.0f, remaining - std::max(0.0f, delta));
            if (timedRemaining.compare_exchange_weak(remaining, next)) {
                return next > 0.0f;
            }
        }
        return false;
    }

    void ResetTrailVisibility()
    {
        timedRemaining = 0.0f;
    }
}
