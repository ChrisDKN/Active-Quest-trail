#include "Settings.h"
#include "Keybind.h"
#include "Trail.h"
#include "Translations.h"
#include "Version.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    constexpr std::initializer_list<REL::Version> supportedRuntimes{
        SKSE::RUNTIME_SSE_1_5_97,
        SKSE::RUNTIME_SSE_1_6_317,
        SKSE::RUNTIME_SSE_1_6_318,
        SKSE::RUNTIME_SSE_1_6_323,
        SKSE::RUNTIME_SSE_1_6_342,
        SKSE::RUNTIME_SSE_1_6_353,
        SKSE::RUNTIME_SSE_1_6_629,
        SKSE::RUNTIME_SSE_1_6_640,
        SKSE::RUNTIME_SSE_1_6_659,
        SKSE::RUNTIME_SSE_1_6_1130,
        SKSE::RUNTIME_SSE_1_6_1170,
        SKSE::RUNTIME_SSE_1_6_1179,
        SKSE::RUNTIME_SSE_1_7_99,
        SKSE::RUNTIME_SSE_1_7_104,
    };
    constexpr REL::Version minimumSKSE{2, 0, 20, 0};

    bool SupportsRuntime(REL::Version runtime)
    {
        return std::ranges::find(supportedRuntimes, runtime) != supportedRuntimes.end();
    }

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
    version.UsesNoStructs();  // CommonLib selects pre/post-1.6.629 layouts at runtime.
    version.CompatibleVersions(supportedRuntimes);
    version.MinimumRequiredXSEVersion(minimumSKSE);
    return version;
}();

SKSE_PLUGIN_QUERY(const SKSE::QueryInterface* skse, SKSE::PluginInfo* info)
{
    info->infoVersion = SKSE::PluginInfo::kVersion;
    info->name = "ActiveQuestTrail";
    info->version = AQT::pluginVersion.pack();
    return !skse->IsEditor() && SupportsRuntime(skse->RuntimeVersion()) &&
           skse->SKSEVersion() >= minimumSKSE.pack();
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse)
{
    if (skse->IsEditor()) {
        return false;
    }
    const auto directory = SKSE::log::log_directory();
    if (!directory) {
        return false;
    }
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*directory / "ActiveQuestTrail.log").string(), true);
    auto logger = std::make_shared<spdlog::logger>("ActiveQuestTrail", std::move(sink));
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%H:%M:%S] [%l] %v");
    spdlog::flush_on(spdlog::level::info);
    if (!SupportsRuntime(skse->RuntimeVersion())) {
        spdlog::error("Skyrim {} is not supported by this build of Active Quest Trail.", skse->RuntimeVersion().string());
        return false;
    }
    if (skse->SKSEVersion() < minimumSKSE.pack()) {
        spdlog::error("Active Quest Trail requires SKSE {} or newer.", minimumSKSE.string());
        return false;
    }
    SKSE::Init(skse);
    spdlog::info("Active Quest Trail {} loaded on {}", AQT::pluginVersionString, skse->RuntimeVersion().string());
    return SKSE::GetMessagingInterface()->RegisterListener(Message);
}
