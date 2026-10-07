#include "Keybind.h"
#include "Settings.h"
#include "SKSEMenuFramework.h"

#include <atomic>

namespace AQT
{
    namespace
    {
        using Key = RE::BSKeyboardDevice::Key;
        std::atomic<bool> capturing{false};
        std::atomic<int> capturedKey{0};
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
                return "Unbound";
            }
            switch (key) {
            case Key::kPause: return "Pause";
            case Key::kNumLock: return "Num Lock";
            case Key::kPrintScreen: return "Print Screen";
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
            return std::format("Key {}", key);
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

        bool CanToggle()
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

        class ToggleInput final : public RE::BSTEventSink<RE::InputEvent*>
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
                    if (settings.toggleKey && button->GetIDCode() == static_cast<std::uint32_t>(settings.toggleKey) &&
                        !capturing.load() && CanToggle() && ModifierHeld(settings.toggleModifier)) {
                        ToggleEnabled();
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
        const char* modifiers[]{"None", "Ctrl", "Shift", "Alt"};
        ImGuiMCP::SetNextItemWidth(ImGuiMCP::GetFontSize() * 6.0f);
        bool changed = ImGuiMCP::Combo("##AQTToggleModifier", &settings.toggleModifier, modifiers, 4);
        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip("Modifier");
        }
        ImGuiMCP::SameLine();
        const auto label = KeyName(settings.toggleKey) + "###AQTToggleKey";
        if (ImGuiMCP::Button(label.c_str())) {
            capturedKey = 0;
            capturing = true;
            ImGuiMCP::OpenPopup("Choose toggle key");
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Clear##AQTToggleKey")) {
            settings.toggleKey = 0;
            changed = true;
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::TextUnformatted("Toggle key");
        if (ImGuiMCP::BeginPopupModal("Choose toggle key", nullptr, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGuiMCP::TextUnformatted("Press a keyboard key. Escape cancels.");
            const auto key = capturedKey.exchange(0);
            const bool cancel = ImGuiMCP::Button("Cancel");
            if (key != 0 || cancel || !capturing.load()) {
                if (key > 0 && !cancel) {
                    settings.toggleKey = key;
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

    void RegisterToggleInput()
    {
        static ToggleInput input;
        if (auto* manager = RE::BSInputDeviceManager::GetSingleton()) {
            manager->AddEventSink(&input);
        }
    }
}
