#include "Settings.h"
#include "ConfigFile.h"
#include "Keybind.h"
#include "Translations.h"

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include "SKSEMenuFramework.h"

namespace AQT
{
    namespace
    {
        std::mutex mutex;
        std::mutex fileMutex;
        Settings current;
        enum class SaveState { Ready, Pending, Saved, Failed, Reloaded, LoadFailed };
        std::atomic<SaveState> saveState{SaveState::Ready};
        std::atomic<bool> savePending{false};
        std::chrono::steady_clock::time_point saveAfter;
        std::uint64_t revision{0};
        std::string status{"StatusWaitingForGame"};
        std::size_t segmentCount{0};

        std::filesystem::path ConfigPath()
        {
            return std::filesystem::absolute("Data/SKSE/Plugins/ActiveQuestTrail.ini");
        }

        std::filesystem::path LightConfigPath()
        {
            return std::filesystem::absolute("Data/SKSE/Plugins/ActiveQuestTrail_Lights.ini");
        }

        std::filesystem::path LightingDefaultsPath()
        {
            return std::filesystem::absolute("Data/SKSE/Plugins/ActiveQuestTrail_LightingDefaults.ini");
        }

        void WriteConfig(const std::filesystem::path& path, const std::string& text)
        {
            std::filesystem::create_directories(path.parent_path());
            // Preserve mod managers' hardlinks to the stored user settings.
            std::ofstream output(path, std::ios::trunc);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output << text;
            output.close();
        }

        void QueueSave()
        {
            std::scoped_lock lock(mutex);
            saveAfter = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            savePending = true;
            saveState = SaveState::Pending;
        }

        void RenderMenu()
        {
            if (ImGuiMCP::Button(Translations::Label("SaveNow"))) {
                SaveSettings();
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button(Translations::Label("ReloadSettings"))) {
                LoadSettings();
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button(Translations::Label("RestoreDefaults"))) {
                SetSettings(Settings{});
                QueueSave();
            }
            const char* message = Translations::Get("AutoSave");
            switch (saveState.load()) {
            case SaveState::Pending: message = Translations::Get("SavePending"); break;
            case SaveState::Saved: message = Translations::Get("SaveSucceeded"); break;
            case SaveState::Failed: message = Translations::Get("SaveFailed"); break;
            case SaveState::Reloaded: message = Translations::Get("SettingsReloaded"); break;
            case SaveState::LoadFailed: message = Translations::Get("ReloadFailed"); break;
            default: break;
            }
            ImGuiMCP::TextWrapped("%s", message);
            ImGuiMCP::Separator();
            auto settings = GetSettings();
            bool changed = ImGuiMCP::Checkbox(Translations::Label("EnableQuestTrail"), &settings.enabled);
            changed |= RenderKeybindSettings(settings);
            const char* styles[]{Translations::Get("WispTrail"), Translations::Get("FollowTheChicken")};
            changed |= ImGuiMCP::Combo(Translations::Label("TrailStyle"), &settings.trailStyle, styles, 2);
            if (settings.trailStyle == 1) {
                changed |= ImGuiMCP::Checkbox(Translations::Label("ShowTrailWithChicken"), &settings.chickenTrail);
                changed |= ImGuiMCP::SliderFloat(Translations::Label("ChickenLeadDistance"), &settings.chickenDistance, 300.0f, 1000.0f, Translations::Get("UnitsFormat"));
                ImGuiMCP::TextWrapped("%s", Translations::Get("ChickenHelp"));
            }
            ImGuiMCP::Separator();
            changed |= ImGuiMCP::Checkbox(Translations::Label("HideIndoors"), &settings.hideIndoors);
            changed |= ImGuiMCP::Checkbox(Translations::Label("HideInDungeons"), &settings.hideDungeons);
            changed |= ImGuiMCP::Checkbox(Translations::Label("HideInCombat"), &settings.hideInCombat);
            ImGuiMCP::Separator();
            changed |= ImGuiMCP::Checkbox(Translations::Label("CustomColour"), &settings.customColour);
            if (settings.customColour) {
                changed |= ImGuiMCP::ColorEdit3(Translations::Label("TrailColour"), settings.colour.data());
            }
            changed |= ImGuiMCP::SliderFloat(Translations::Label("Brightness"), &settings.brightness, 0.1f, 5.0f, "%.2f");
            changed |= ImGuiMCP::SliderFloat(Translations::Label("Opacity"), &settings.opacity, 0.05f, 1.0f, "%.2f");
            changed |= ImGuiMCP::SliderFloat(Translations::Label("HeightAboveRoute"), &settings.height, 16.0f, 160.0f, Translations::Get("UnitsFormat"));
            changed |= ImGuiMCP::SliderFloat(Translations::Label("ClearSpaceAroundPlayer"), &settings.startDistance, 32.0f, 256.0f, Translations::Get("UnitsFormat"));
            changed |= ImGuiMCP::Checkbox(Translations::Label("GlowDestination"), &settings.glowDoor);
            changed |= ImGuiMCP::Checkbox(Translations::Label("AnimateTrail"), &settings.animate);
            changed |= ImGuiMCP::Checkbox(Translations::Label("DriftingSparks"), &settings.particles);
            changed |= ImGuiMCP::SliderFloat(Translations::Label("FlowSpeed"), &settings.animationSpeed, 0.25f, 3.0f, "%.2f");
            changed |= ImGuiMCP::SliderFloat(Translations::Label("UpdateFadeDuration"), &settings.fadeSeconds, 0.1f, 1.5f, Translations::Get("SecondsFormat"));
            ImGuiMCP::Separator();
            changed |= ImGuiMCP::Checkbox(Translations::Label("TrailLighting"), &settings.trailLights);
            if (settings.trailLights) {
                changed |= ImGuiMCP::SliderFloat(Translations::Label("LightBrightness"), &settings.lightBrightness, 0.1f, 5.0f, "%.2f");
                changed |= ImGuiMCP::SliderFloat(Translations::Label("LightRadius"), &settings.lightRadius, 128.0f, 384.0f, Translations::Get("UnitsFormat"));
                ImGuiMCP::TextWrapped("%s", Translations::Get("LightingHelp"));
            }
            ImGuiMCP::Separator();
            changed |= ImGuiMCP::SliderFloat(Translations::Label("TrailLength"), &settings.trailLength, 1500.0f, 12000.0f, Translations::Get("UnitsFormat"));
            ImGuiMCP::TextWrapped("%s", Translations::Get("TrailLengthHelp"));
            changed |= ImGuiMCP::Checkbox(Translations::Label("AnchorTrailToTheRoute"), &settings.anchorTrail);
            if (settings.anchorTrail) {
                changed |= ImGuiMCP::SliderFloat(Translations::Label("RebuildWhenOffRoute"), &settings.offRouteDistance, 64.0f, 1024.0f, Translations::Get("UnitsFormat"));
                changed |= ImGuiMCP::SliderFloat(Translations::Label("ExtendWithDistanceRemaining"), &settings.extendDistance, 256.0f, 6000.0f, Translations::Get("UnitsFormat"));
                ImGuiMCP::TextWrapped("%s", Translations::Get("AnchoringHelp"));
            } else {
                changed |= ImGuiMCP::Checkbox(Translations::Label("RefreshWhileMoving"), &settings.refreshWhileMoving);
                changed |= ImGuiMCP::SliderFloat(Translations::Label("RefreshInterval"), &settings.refreshSeconds, 0.25f, 2.0f, Translations::Get("SecondsFormat"));
            }
            if (changed) {
                SetSettings(settings);
                QueueSave();
            }
            SavePendingSettings(!ImGuiMCP::IsAnyItemActive());
            if (ImGuiMCP::Button(Translations::Label("RebuildTrail"))) {
                RequestRefresh();
            }
            ImGuiMCP::Separator();
            std::string text;
            {
                std::scoped_lock lock(mutex);
                text = Translations::Format("StatusLine", Translations::Get(status.c_str()), segmentCount);
            }
            ImGuiMCP::TextWrapped("%s", text.c_str());
            ImGuiMCP::TextWrapped("%s", Translations::Get("QuestHelp"));
        }
    }

    Settings GetSettings()
    {
        std::scoped_lock lock(mutex);
        return current;
    }

    void SetSettings(Settings settings)
    {
        for (auto* key : {&settings.toggleKey, &settings.holdKey, &settings.timedKey}) {
            if (!IsBindableKey(*key)) {
                *key = 0;
            }
        }
        settings.toggleModifier = std::clamp(settings.toggleModifier, 0, 3);
        settings.holdModifier = std::clamp(settings.holdModifier, 0, 3);
        settings.timedModifier = std::clamp(settings.timedModifier, 0, 3);
        if (settings.holdToShow) {
            settings.timedShow = false;
        }
        settings.showSeconds = std::isfinite(settings.showSeconds) ? std::clamp(settings.showSeconds, 1.0f, 120.0f) : Settings{}.showSeconds;
        settings.trailStyle = std::clamp(settings.trailStyle, 0, 1);
        settings.chickenDistance = std::clamp(settings.chickenDistance, 300.0f, 1000.0f);
        settings.brightness = std::clamp(settings.brightness, 0.1f, 5.0f);
        settings.opacity = std::clamp(settings.opacity, 0.05f, 1.0f);
        settings.height = std::clamp(settings.height, 16.0f, 160.0f);
        settings.startDistance = std::clamp(settings.startDistance, 32.0f, 256.0f);
        settings.animationSpeed = std::clamp(settings.animationSpeed, 0.25f, 3.0f);
        settings.fadeSeconds = std::clamp(settings.fadeSeconds, 0.1f, 1.5f);
        settings.lightBrightness = std::clamp(settings.lightBrightness, 0.1f, 5.0f);
        settings.lightRadius = std::clamp(settings.lightRadius, 128.0f, 384.0f);
        settings.trailLength = std::clamp(settings.trailLength, 1500.0f, 12000.0f);
        settings.offRouteDistance = std::clamp(settings.offRouteDistance, 64.0f, 1024.0f);
        settings.extendDistance = std::clamp(settings.extendDistance, 256.0f, 6000.0f);
        settings.refreshSeconds = std::clamp(settings.refreshSeconds, 0.25f, 2.0f);
        for (auto& component : settings.colour) {
            component = std::clamp(component, 0.0f, 1.0f);
        }
        std::scoped_lock lock(mutex);
        if (settings.enabled != current.enabled || settings.holdToShow != current.holdToShow ||
            settings.timedShow != current.timedShow || settings.holdKey != current.holdKey ||
            settings.holdModifier != current.holdModifier || settings.timedKey != current.timedKey ||
            settings.timedModifier != current.timedModifier || settings.showSeconds != current.showSeconds) {
            ResetTrailVisibility();
        }
        current = settings;
        ++revision;
    }

    void ToggleEnabled()
    {
        {
            std::scoped_lock lock(mutex);
            current.enabled = !current.enabled;
            ResetTrailVisibility();
            ++revision;
        }
        QueueSave();
    }

    std::uint64_t SettingsRevision()
    {
        std::scoped_lock lock(mutex);
        return revision;
    }

    void LoadSettings()
    {
        std::scoped_lock fileLock(fileMutex);
        try {
            ConfigFile config;
            config.Load(ConfigPath());
            ConfigFile lights;
            lights.Load(LightConfigPath());
            lights.Load(LightingDefaultsPath());
            Settings settings;
            settings.enabled = config.Number("General", "Enabled", 1) != 0;
            settings.toggleKey = static_cast<int>(std::clamp(config.Number("Controls", "ToggleKey", 0), 0.0f, 255.0f));
            settings.toggleModifier = static_cast<int>(std::clamp(config.Number("Controls", "ToggleModifier", 0), 0.0f, 3.0f));
            settings.holdToShow = config.Number("Controls", "HoldToShow", 0) != 0;
            settings.holdKey = static_cast<int>(std::clamp(config.Number("Controls", "HoldKey", 0), 0.0f, 255.0f));
            settings.holdModifier = static_cast<int>(std::clamp(config.Number("Controls", "HoldModifier", 0), 0.0f, 3.0f));
            settings.timedShow = config.Number("Controls", "TimedShow", 0) != 0;
            settings.timedKey = static_cast<int>(std::clamp(config.Number("Controls", "TimedKey", 0), 0.0f, 255.0f));
            settings.timedModifier = static_cast<int>(std::clamp(config.Number("Controls", "TimedModifier", 0), 0.0f, 3.0f));
            settings.showSeconds = config.Number("Controls", "ShowSeconds", settings.showSeconds);
            settings.trailStyle = static_cast<int>(std::clamp(config.Number("General", "TrailStyle", 0), 0.0f, 1.0f));
            settings.chickenDistance = config.Number("General", "ChickenDistance", settings.chickenDistance);
            settings.chickenTrail = config.Number("General", "ChickenTrail", 0) != 0;
            settings.hideIndoors = config.Number("Visibility", "HideIndoors", 0) != 0;
            settings.hideDungeons = config.Number("Visibility", "HideDungeons", 0) != 0;
            settings.hideInCombat = config.Number("Visibility", "HideInCombat", 0) != 0;
            settings.customColour = config.Number("Appearance", "CustomColour", 0) != 0;
            settings.colour[0] = config.Number("Appearance", "Red", settings.colour[0]);
            settings.colour[1] = config.Number("Appearance", "Green", settings.colour[1]);
            settings.colour[2] = config.Number("Appearance", "Blue", settings.colour[2]);
            settings.brightness = config.Number("Appearance", "Brightness", 1);
            settings.opacity = config.Number("Appearance", "Opacity", 1);
            settings.height = config.Number("Appearance", "Height", settings.height);
            settings.startDistance = config.Number("Appearance", "StartDistance", settings.startDistance);
            settings.glowDoor = config.Number("Appearance", "GlowDoor", 1) != 0;
            settings.animate = config.Number("Appearance", "Animate", 1) != 0;
            settings.particles = config.Number("Appearance", "Particles", 1) != 0;
            settings.animationSpeed = config.Number("Appearance", "AnimationSpeed", settings.animationSpeed);
            settings.fadeSeconds = config.Number("Appearance", "FadeSeconds", settings.fadeSeconds);
            settings.trailLength = config.Number("General", "TrailLength", settings.trailLength);
            settings.anchorTrail = config.Number("General", "AnchorTrail", 1) != 0;
            settings.offRouteDistance = config.Number("General", "OffRouteDistance", settings.offRouteDistance);
            settings.extendDistance = config.Number("General", "ExtendDistance", settings.extendDistance);
            settings.refreshWhileMoving = config.Number("General", "RefreshWhileMoving", 1) != 0;
            settings.refreshSeconds = config.Number("General", "RefreshSeconds", settings.refreshSeconds);
            settings.trailLights = config.Number("Lighting", "Enabled", lights.Number("Lighting", "Enabled", settings.trailLights)) != 0;
            settings.lightBrightness = config.Number("Lighting", "Brightness", lights.Number("Lighting", "Brightness", settings.lightBrightness));
            settings.lightRadius = config.Number("Lighting", "Radius", lights.Number("Lighting", "Radius", settings.lightRadius));
            SetSettings(settings);
            savePending = false;
            saveState = SaveState::Reloaded;
            spdlog::info("Settings read from {} (if present); lighting defaults from {}", ConfigPath().string(), LightingDefaultsPath().string());
            spdlog::info("Trail lighting: enabled={}, brightness={}, radius={}", settings.trailLights, settings.lightBrightness, settings.lightRadius);
            spdlog::info("Settings loaded: enabled={}, hideIndoors={}, hideDungeons={}", settings.enabled, settings.hideIndoors, settings.hideDungeons);
        } catch (const std::exception& error) {
            savePending = false;
            saveState = SaveState::LoadFailed;
            spdlog::error("Cannot load settings: {}", error.what());
        }
    }

    bool SaveSettings()
    {
        std::scoped_lock fileLock(fileMutex);
        Settings settings;
        std::uint64_t savedRevision;
        {
            std::scoped_lock lock(mutex);
            settings = current;
            savedRevision = revision;
        }
        try {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << "[General]\nEnabled=" << settings.enabled
                   << "\nTrailStyle=" << settings.trailStyle
                   << "\nChickenDistance=" << settings.chickenDistance
                   << "\nChickenTrail=" << settings.chickenTrail
                   << "\nTrailLength=" << settings.trailLength
                   << "\nAnchorTrail=" << settings.anchorTrail
                   << "\nOffRouteDistance=" << settings.offRouteDistance
                   << "\nExtendDistance=" << settings.extendDistance
                   << "\nRefreshSeconds=" << settings.refreshSeconds
                   << "\nRefreshWhileMoving=" << settings.refreshWhileMoving
                   << "\n\n[Visibility]\nHideIndoors=" << settings.hideIndoors
                   << "\nHideDungeons=" << settings.hideDungeons
                   << "\nHideInCombat=" << settings.hideInCombat
                   << "\n\n[Appearance]\nCustomColour=" << settings.customColour
                   << "\nRed=" << settings.colour[0] << "\nGreen=" << settings.colour[1]
                   << "\nBlue=" << settings.colour[2] << "\nBrightness=" << settings.brightness
                   << "\nOpacity=" << settings.opacity << "\nHeight=" << settings.height
                   << "\nStartDistance=" << settings.startDistance << "\nGlowDoor=" << settings.glowDoor
                   << "\nAnimate=" << settings.animate << "\nParticles=" << settings.particles
                   << "\nAnimationSpeed=" << settings.animationSpeed
                   << "\nFadeSeconds=" << settings.fadeSeconds << '\n';
            output << "\n[Controls]\nToggleKey=" << settings.toggleKey
                   << "\nToggleModifier=" << settings.toggleModifier
                   << "\nHoldToShow=" << settings.holdToShow
                   << "\nHoldKey=" << settings.holdKey
                   << "\nHoldModifier=" << settings.holdModifier
                   << "\nTimedShow=" << settings.timedShow
                   << "\nTimedKey=" << settings.timedKey
                   << "\nTimedModifier=" << settings.timedModifier
                   << "\nShowSeconds=" << settings.showSeconds << '\n';
            output << "\n[Lighting]\nEnabled=" << settings.trailLights
                   << "\nBrightness=" << settings.lightBrightness
                   << "\nRadius=" << settings.lightRadius << '\n';
            WriteConfig(ConfigPath(), output.str());
            {
                std::scoped_lock lock(mutex);
                if (revision == savedRevision) {
                    savePending = false;
                    saveState = SaveState::Saved;
                }
            }
            spdlog::info("Settings saved to {}", ConfigPath().string());
            return true;
        } catch (const std::exception& error) {
            {
                std::scoped_lock lock(mutex);
                if (revision == savedRevision) {
                    savePending = false;
                    saveState = SaveState::Failed;
                }
            }
            spdlog::error("Cannot save settings to {}: {}", ConfigPath().string(), error.what());
            return false;
        }
    }

    void SavePendingSettings(bool force)
    {
        if (!savePending.load(std::memory_order_relaxed)) {
            return;
        }
        {
            std::scoped_lock lock(mutex);
            if (!savePending || (!force && std::chrono::steady_clock::now() < saveAfter)) {
                return;
            }
            savePending = false;
        }
        SaveSettings();
    }

    void SetStatus(std::string text, std::size_t segments)
    {
        std::scoped_lock lock(mutex);
        if (status != text) {
            spdlog::info("Trail: {}", Translations::English(text.c_str()));
        }
        status = std::move(text);
        segmentCount = segments;
    }

    void RegisterMenu()
    {
        if (!GetModuleHandleW(L"SKSEMenuFramework.dll") || SKSEMenuFramework::GetMenuFrameworkVersion() < 3.0f) {
            spdlog::info("SKSE Menu Framework 3 unavailable; INI configuration remains available");
            return;
        }
        RegisterKeybindMenu();
        SKSEMenuFramework::SetSection(Translations::Get("ModName"));
        SKSEMenuFramework::AddSectionItem(Translations::Get("Settings"), RenderMenu);
        spdlog::info("Settings page registered");
    }
}
