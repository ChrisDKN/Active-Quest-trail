#include "Translations.h"

#include <SKSE/Translation.h>
#include <map>
#include <string_view>

namespace AQT::Translations
{
    namespace
    {
        using Table = std::map<std::string, std::string, std::less<>>;
        const Table english{
            {"SaveNow", "Save now"},
            {"ReloadSettings", "Reload settings"},
            {"RestoreDefaults", "Restore defaults"},
            {"EnableQuestTrail", "Enable quest trail"},
            {"TrailStyle", "Trail style"},
            {"ShowTrailWithChicken", "Show trail with chicken"},
            {"ChickenLeadDistance", "Chicken lead distance"},
            {"HideIndoors", "Hide indoors"},
            {"HideInDungeons", "Hide in dungeons"},
            {"HideInCombat", "Hide in combat"},
            {"CustomColour", "Custom colour"},
            {"TrailColour", "Trail colour"},
            {"Brightness", "Brightness"},
            {"Opacity", "Opacity"},
            {"HeightAboveRoute", "Height above route"},
            {"ClearSpaceAroundPlayer", "Clear space around player"},
            {"GlowDestination", "Glow destination"},
            {"AnimateTrail", "Animate trail"},
            {"DriftingSparks", "Drifting sparks"},
            {"FlowSpeed", "Flow speed"},
            {"UpdateFadeDuration", "Update fade duration"},
            {"TrailLighting", "Trail lighting"},
            {"LightBrightness", "Light brightness"},
            {"LightRadius", "Light radius"},
            {"TrailLength", "Trail length"},
            {"AnchorTrailToTheRoute", "Anchor trail to the route"},
            {"RebuildWhenOffRoute", "Rebuild when off route"},
            {"ExtendWithDistanceRemaining", "Extend with distance remaining"},
            {"RefreshWhileMoving", "Refresh while moving"},
            {"RefreshInterval", "Refresh interval"},
            {"RebuildTrail", "Rebuild trail"},
            {"AutoSave", "Changes save automatically."},
            {"SavePending", "Saving after editing..."},
            {"SaveSucceeded", "Settings saved automatically."},
            {"SaveFailed", "Could not save settings. Check ActiveQuestTrail.log and use Save now to retry."},
            {"SettingsReloaded", "Settings reloaded. Changes save automatically."},
            {"ReloadFailed", "Could not reload settings. Check ActiveQuestTrail.log."},
            {"WispTrail", "Wisp trail"},
            {"FollowTheChicken", "Follow the chicken"},
            {"ChickenHelp", "Cosmetic guide; reappears ahead if you outrun it."},
            {"LightingHelp", "Requires Community Shaders with Light Limit Fix. Up to 12 nearby lights."},
            {"TrailLengthHelp", "Maximum length; routes may end sooner. Longer trails cost more performance."},
            {"AnchoringHelp", "For moving objectives, turn anchoring off or use Rebuild trail."},
            {"QuestHelp", "Track one quest for predictable guidance."},
            {"UnitsFormat", "%.0f units"},
            {"SecondsFormat", "%.2f s"},
            {"StatusLine", "{0} | Trail segments: {1}"},
            {"ModName", "Active Quest Trail"},
            {"Settings", "Settings"},
            {"Clear", "Clear"},
            {"HoldToShowTheTrail", "Hold to show the trail"},
            {"PressToShowTheTrailTemporarily", "Press to show the trail temporarily"},
            {"ShowDuration", "Show duration"},
            {"Cancel", "Cancel"},
            {"Unbound", "Unbound"},
            {"KeyPause", "Pause"},
            {"KeyNumLock", "Num Lock"},
            {"KeyPrintScreen", "Print Screen"},
            {"ModifierNone", "None"},
            {"ModifierCtrl", "Ctrl"},
            {"ModifierShift", "Shift"},
            {"ModifierAlt", "Alt"},
            {"ShowSecondsFormat", "%.1f seconds"},
            {"KeyNumber", "Key {0}"},
            {"Modifier", "Modifier"},
            {"ToggleKey", "Toggle key"},
            {"HoldKey", "Hold key"},
            {"TimedKey", "Timed key"},
            {"ChooseTrailKey", "Choose trail key"},
            {"VisibilityModeHelp", "Choose one mode, or leave both off for a continuous trail. Enable quest trail is the master switch. Pressing the timed key again restarts the countdown; paused menus pause it."},
            {"KeyConflictHelp", "The show key overlaps the toggle key. Choose different keys or modifiers so the toggle does not switch the trail off."},
            {"ChooseKeyHelp", "Press a keyboard key. Escape cancels."},
            {"StatusWaitingForGame", "Waiting for a loaded game"},
            {"StatusDisabled", "Disabled"},
            {"StatusHoldKey", "Hold the configured key to show the trail"},
            {"StatusSetHoldKey", "Set a hold key in Settings"},
            {"StatusTimedKey", "Press the configured key to show the trail"},
            {"StatusSetTimedKey", "Set a timed key in Settings"},
            {"StatusDead", "Hidden while dead"},
            {"StatusWaitingForArea", "Waiting for an area"},
            {"StatusIndoors", "Hidden indoors"},
            {"StatusDungeon", "Hidden in a dungeon"},
            {"StatusCombat", "Hidden in combat"},
            {"StatusNoObjective", "No tracked quest objective"},
            {"StatusWaitingForPlayer", "Waiting for the player"},
            {"StatusFollowingChicken", "Following the chicken"},
            {"StatusFollowingObjective", "Following the current objective"},
            {"StatusPlayerClearance", "Route ready; nearby trail is inside player clearance"},
            {"StatusWaitingForRoute", "Waiting for a route from Skyrim"},
            {"StatusMissingPlugin", "ActiveQuestTrail.esp is missing or outdated"},
            {"StatusLoading", "Loading game"},
            {"StatusLoadFailed", "Game load failed"},
        };
        Table strings = english;
        Table labels;

        bool ValidFloatFormat(std::string_view value, std::string_view conversion)
        {
            unsigned count = 0;
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (value[i] != '%') {
                    continue;
                }
                if (i + 1 < value.size() && value[i + 1] == '%') {
                    ++i;
                } else if (value.substr(i).starts_with(conversion)) {
                    ++count;
                    i += conversion.size() - 1;
                } else {
                    return false;
                }
            }
            return count == 1;
        }

        bool Valid(const std::string& key, const std::string& value)
        {
            if (value.find_first_not_of(" \t\r\n") == std::string::npos || value.find("##") != std::string::npos) {
                return false;
            }
            if ((key == "ModName" || key == "Settings") && value.find_first_of("/\r\n") != std::string::npos) {
                return false;
            }
            if (key == "UnitsFormat") {
                return ValidFloatFormat(value, "%.0f");
            }
            if (key == "SecondsFormat") {
                return ValidFloatFormat(value, "%.2f");
            }
            if (key == "ShowSecondsFormat") {
                return ValidFloatFormat(value, "%.1f");
            }
            try {
                if (key == "KeyNumber") {
                    int keyCode = 1;
                    (void)std::vformat(value, std::make_format_args(keyCode));
                } else if (key == "StatusLine") {
                    const char* status = "";
                    std::size_t count = 0;
                    (void)std::vformat(value, std::make_format_args(status, count));
                }
            } catch (const std::format_error&) {
                return false;
            }
            return true;
        }
    }

    void Load()
    {
        SKSE::Translation::ParseTranslation("ActiveQuestTrail");
        std::size_t translated = 0;
        for (const auto& [key, fallback] : english) {
            std::string value;
            const auto token = "$AQT_" + key;
            if (SKSE::Translation::Translate(token, value) && value != token) {
                if (Valid(key, value)) {
                    strings[key] = std::move(value);
                    ++translated;
                } else {
                    spdlog::warn("Invalid translation for {}; using English", token);
                }
            }
            labels[key] = strings.at(key) + "###AQT_" + key;
        }
        spdlog::info("Loaded {} of {} menu translations; missing entries use English", translated, english.size());
    }

    const char* Get(const char* key)
    {
        const auto found = strings.find(key);
        return found != strings.end() ? found->second.c_str() : key;
    }

    const char* Label(const char* key)
    {
        const auto found = labels.find(key);
        return found != labels.end() ? found->second.c_str() : Get(key);
    }

    const char* English(const char* key)
    {
        const auto found = english.find(key);
        return found != english.end() ? found->second.c_str() : key;
    }
}
