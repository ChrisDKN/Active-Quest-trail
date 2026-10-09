#include "Settings.h"
#include "Keybind.h"
#include "Trail.h"
#include "Translations.h"
#include "Version.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
#ifdef AQT_RUNTIME_17104
    constexpr auto runtime = SKSE::RUNTIME_SSE_1_7_104;
    constexpr REL::Version minimumSKSE{2, 3, 1, 0};
#elif defined(AQT_RUNTIME_1597)
    constexpr auto runtime = SKSE::RUNTIME_SSE_1_5_97;
    constexpr REL::Version minimumSKSE{2, 0, 20, 0};
#else
    constexpr auto runtime = SKSE::RUNTIME_SSE_1_6_1170;
    constexpr REL::Version minimumSKSE{2, 2, 6, 0};
#endif

    void Message(SKSE::MessagingInterface::Message* message)
    {
        switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            AQT::Translations::Load();
            AQT::LoadSettings();
            AQT::RegisterMenu();
            AQT::RegisterTrailInput();
            if (AQT::InitializeTrail()) {
                AQT::InstallUpdateHook();
            }
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            AQT::OnLoadStart();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            AQT::OnLoadFinished(message->data != nullptr);
            break;
        case SKSE::MessagingInterface::kNewGame:
            AQT::OnLoadFinished(true);
            break;
        case SKSE::MessagingInterface::kSaveGame:
            AQT::OnSave();
            break;
        default:
            break;
        }
    }
}

SKSE_PLUGIN_VERSION = [] {
    SKSE::PluginVersionData version;
    version.PluginVersion(AQT::pluginVersion);
    version.PluginName("ActiveQuestTrail");
    version.AuthorName("Active Quest Trail contributors");
    version.UsesAddressLibrary();
    version.UsesUpdatedStructs();
    version.CompatibleVersions({runtime});
    version.MinimumRequiredXSEVersion(minimumSKSE);
    return version;
}();

#ifdef AQT_RUNTIME_1597
SKSE_PLUGIN_QUERY(const SKSE::QueryInterface* skse, SKSE::PluginInfo* info)
{
    info->infoVersion = SKSE::PluginInfo::kVersion;
    info->name = "ActiveQuestTrail";
    info->version = AQT::pluginVersion.pack();
    return !skse->IsEditor() && skse->RuntimeVersion() == runtime &&
           skse->SKSEVersion() >= minimumSKSE.pack();
}
#endif

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse)
{
    const auto directory = SKSE::log::log_directory();
    if (!directory) {
        return false;
    }
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*directory / "ActiveQuestTrail.log").string(), true);
    auto logger = std::make_shared<spdlog::logger>("ActiveQuestTrail", std::move(sink));
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%H:%M:%S] [%l] %v");
    spdlog::flush_on(spdlog::level::info);
    if (skse->RuntimeVersion() != runtime) {
        spdlog::error("This build requires Skyrim {}; found {}. Select the matching game version in the installer.", runtime.string(), skse->RuntimeVersion().string());
        return false;
    }
    SKSE::Init(skse);
    spdlog::info("Active Quest Trail {} loaded on {}", AQT::pluginVersionString, skse->RuntimeVersion().string());
    return SKSE::GetMessagingInterface()->RegisterListener(Message);
}
