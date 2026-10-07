#include "Settings.h"
#include "ConfigFile.h"

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
        std::string status{"Waiting for a loaded game"};
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
            if (ImGuiMCP::Button("Save now")) {
                SaveSettings();
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Reload settings")) {
                LoadSettings();
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Restore defaults")) {
                SetSettings(Settings{});
                QueueSave();
            }
            const char* message = "Changes save automatically.";
            switch (saveState.load()) {
            case SaveState::Pending: message = "Saving after editing..."; break;
            case SaveState::Saved: message = "Settings saved automatically."; break;
            case SaveState::Failed: message = "Could not save settings. Check ActiveQuestTrail.log and use Save now to retry."; break;
            case SaveState::Reloaded: message = "Settings reloaded. Changes save automatically."; break;
            case SaveState::LoadFailed: message = "Could not reload settings. Check ActiveQuestTrail.log."; break;
            default: break;
            }
            ImGuiMCP::TextWrapped("%s", message);
            ImGuiMCP::Separator();
            auto settings = GetSettings();
            bool changed = ImGuiMCP::Checkbox("Enable quest trail", &settings.enabled);
            const char* styles[]{"Wisp trail", "Follow the chicken"};
            changed |= ImGuiMCP::Combo("Trail style", &settings.trailStyle, styles, 2);
            if (settings.trailStyle == 1) {
                changed |= ImGuiMCP::Checkbox("Show trail with chicken", &settings.chickenTrail);
                changed |= ImGuiMCP::SliderFloat("Chicken lead distance", &settings.chickenDistance, 300.0f, 1000.0f, "%.0f units");
                ImGuiMCP::TextWrapped("Cosmetic guide; reappears ahead if you outrun it.");
            }
            ImGuiMCP::Separator();
            changed |= ImGuiMCP::Checkbox("Hide indoors", &settings.hideIndoors);
            changed |= ImGuiMCP::Checkbox("Hide in dungeons", &settings.hideDungeons);
            changed |= ImGuiMCP::Checkbox("Hide in combat", &settings.hideInCombat);
            ImGuiMCP::Separator();
            changed |= ImGuiMCP::Checkbox("Custom colour", &settings.customColour);
            if (settings.customColour) {
                changed |= ImGuiMCP::ColorEdit3("Trail colour", settings.colour.data());
            }
            changed |= ImGuiMCP::SliderFloat("Brightness", &settings.brightness, 0.1f, 5.0f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Opacity", &settings.opacity, 0.05f, 1.0f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Height above route", &settings.height, 16.0f, 160.0f, "%.0f units");
            changed |= ImGuiMCP::SliderFloat("Clear space around player", &settings.startDistance, 32.0f, 256.0f, "%.0f units");
            changed |= ImGuiMCP::Checkbox("Glow destination", &settings.glowDoor);
            changed |= ImGuiMCP::Checkbox("Animate trail", &settings.animate);
            changed |= ImGuiMCP::Checkbox("Drifting sparks", &settings.particles);
            changed |= ImGuiMCP::SliderFloat("Flow speed", &settings.animationSpeed, 0.25f, 3.0f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Update fade duration", &settings.fadeSeconds, 0.1f, 1.5f, "%.2f s");
            ImGuiMCP::Separator();
            changed |= ImGuiMCP::Checkbox("Trail lighting", &settings.trailLights);
            if (settings.trailLights) {
                changed |= ImGuiMCP::SliderFloat("Light brightness", &settings.lightBrightness, 0.1f, 5.0f, "%.2f");
                changed |= ImGuiMCP::SliderFloat("Light radius", &settings.lightRadius, 128.0f, 384.0f, "%.0f units");
                ImGuiMCP::TextWrapped("Requires Community Shaders with Light Limit Fix. Up to 12 nearby lights.");
            }
            ImGuiMCP::Separator();
            changed |= ImGuiMCP::SliderFloat("Trail length", &settings.trailLength, 1500.0f, 12000.0f, "%.0f units");
            ImGuiMCP::TextWrapped("Maximum length; routes may end sooner. Longer trails cost more performance.");
            changed |= ImGuiMCP::Checkbox("Anchor trail to the route", &settings.anchorTrail);
            if (settings.anchorTrail) {
                changed |= ImGuiMCP::SliderFloat("Rebuild when off route", &settings.offRouteDistance, 64.0f, 1024.0f, "%.0f units");
                changed |= ImGuiMCP::SliderFloat("Extend with distance remaining", &settings.extendDistance, 256.0f, 6000.0f, "%.0f units");
                ImGuiMCP::TextWrapped("For moving objectives, turn anchoring off or use Rebuild trail.");
            } else {
                changed |= ImGuiMCP::Checkbox("Refresh while moving", &settings.refreshWhileMoving);
                changed |= ImGuiMCP::SliderFloat("Refresh interval", &settings.refreshSeconds, 0.25f, 2.0f, "%.2f s");
            }
            if (changed) {
                SetSettings(settings);
                QueueSave();
            }
            SavePendingSettings(!ImGuiMCP::IsAnyItemActive());
            if (ImGuiMCP::Button("Rebuild trail")) {
                RequestRefresh();
            }
            ImGuiMCP::Separator();
            std::string text;
            {
                std::scoped_lock lock(mutex);
                text = std::format("{} | Trail segments: {}", status, segmentCount);
            }
            ImGuiMCP::TextWrapped("%s", text.c_str());
            ImGuiMCP::TextWrapped("Track one quest for predictable guidance.");
        }
    }

    Settings GetSettings()
    {
        std::scoped_lock lock(mutex);
        return current;
    }

    void SetSettings(Settings settings)
    {
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
        current = settings;
        ++revision;
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
            spdlog::info("Trail: {}", text);
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
        SKSEMenuFramework::SetSection("Active Quest Trail");
        SKSEMenuFramework::AddSectionItem("Settings", RenderMenu);
        spdlog::info("Settings page registered");
    }
}
