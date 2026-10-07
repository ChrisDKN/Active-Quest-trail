#include "DestinationGlow.h"
#include "Settings.h"

#include <chrono>

namespace AQT
{
    namespace
    {
        RE::TESEffectShader* shader{nullptr};
        RE::ObjectRefHandle destination;
        RE::NiPointer<RE::ShaderReferenceEffect> active;
        RE::NiPointer<RE::NiAVObject> activeRoot;
        bool failureLogged{false};
        std::chrono::steady_clock::time_point nextAttempt;

        bool SameSpace(RE::TESObjectREFR* target, RE::PlayerCharacter* player)
        {
            auto* cell = target->GetParentCell();
            auto* playerCell = player->GetParentCell();
            return cell && playerCell && (cell == playerCell ||
                (!cell->IsInteriorCell() && !playerCell->IsInteriorCell() &&
                    cell->GetRuntimeData().worldSpace &&
                    cell->GetRuntimeData().worldSpace == playerCell->GetRuntimeData().worldSpace));
        }

        bool AcquireGlow(RE::TESObjectREFR* target)
        {
            auto* effects = RE::ProcessLists::GetSingleton();
            if (!effects) {
                return false;
            }
            const auto handle = target->GetHandle();
            const auto find = [&] {
                effects->ForEachShaderEffect([&](RE::ShaderReferenceEffect* effect) {
                    if (effect->effectData == shader && effect->target == handle && !effect->finished &&
                        (effect->lifetime < 0.0f || effect->age < effect->lifetime)) {
                        active.reset(effect);
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            };
            find();
            const auto now = std::chrono::steady_clock::now();
            if (!active && now >= nextAttempt) {
                nextAttempt = now + std::chrono::milliseconds(500);
                // AE 1.6.1170 can return 1 here, despite CommonLib's pointer return type.
                (void)target->ApplyEffectShader(shader, 2.0f);
                find();
                if (!active && !failureLogged) {
                    spdlog::warn("Waiting for Skyrim to register the destination shader");
                    failureLogged = true;
                }
            }
            if (active) {
                activeRoot.reset(target->Get3D());
            }
            return active != nullptr;
        }

        void StopGlow(bool immediate)
        {
            nextAttempt = {};
            if (active) {
                if (immediate) {
                    active->finished = true;
                } else {
                    active->lifetime = active->age;
                }
                active.reset();
                activeRoot.reset();
            }
        }
    }

    void InitializeDestinationGlow(RE::TESDataHandler* data)
    {
        shader = data->LookupForm<RE::TESEffectShader>(0x806, "ActiveQuestTrail.esp");
        if (!shader) {
            spdlog::warn("Destination glow shader missing; install the complete Active Quest Trail package");
        }
    }

    RE::ObjectRefHandle FindRouteDestination(RE::PlayerCharacter* player, RE::GuideEffect* effect, const RE::NiPoint3& endpoint)
    {
        RE::ObjectRefHandle result;
        float nearest = 256.0f * 256.0f;
        if (!effect->questTarget || !effect->quest) {
            return result;
        }
        const auto consider = [&](RE::TESObjectREFR* ref) {
            if (!ref || ref == player || !ref->GetBaseObject() ||
                ref->IsDisabled() || ref->IsDeleted() || !ref->Get3D() || !SameSpace(ref, player)) {
                return;
            }
            const auto offset = ref->GetPosition() - endpoint;
            const float distance = offset.x * offset.x + offset.y * offset.y;
            if (std::abs(offset.z) <= 192.0f && distance < nearest) {
                nearest = distance;
                result = ref->GetHandle();
            }
        };
        {
            RE::BSSpinLockGuard guard(player->GetQuestTargetsLock());
            for (const auto& link : effect->questTarget->teleportPath.teleportRefs) {
                consider(link.ref);
                if (link.ref) {
                    auto otherSide = link.ref->extraList.GetTeleportLinkedDoor().get();
                    consider(otherSide.get());
                }
            }
        }
        RE::ObjectRefHandle tracking;
        effect->questTarget->GetTrackingRef(tracking, effect->quest);
        auto tracked = tracking.get();
        consider(tracked.get());
        return result;
    }

    void SetGlowDestination(RE::ObjectRefHandle target)
    {
        if (destination != target) {
            StopGlow(false);
            destination = target;
            if (auto ref = target.get()) {
                spdlog::info("Trail ends at reference {:08X}", ref->GetFormID());
            }
        }
    }

    void UpdateDestinationGlow(RE::PlayerCharacter* player, const Settings& settings, bool routeVisible)
    {
        auto target = destination.get();
        if (!shader || !settings.glowDoor || !routeVisible || !target || target->IsDisabled() ||
            target->IsDeleted() || !SameSpace(target.get(), player) || !target->Get3D()) {
            StopGlow(false);
            return;
        }
        const auto rgb = settings.customColour ? settings.colour : Settings{}.colour;
        const RE::Color colour{static_cast<std::uint8_t>(rgb[0] * 255.0f),
            static_cast<std::uint8_t>(rgb[1] * 255.0f), static_cast<std::uint8_t>(rgb[2] * 255.0f), 255};
        auto& data = shader->data;
        data.fillTextureEffectColorKey1 = colour;
        data.fillTextureEffectColorKey2 = colour;
        data.fillTextureEffectColorKey3 = colour;
        data.edgeEffectColor = colour;
        data.colorScale = settings.brightness * 2.0f;
        data.fillTextureEffectFullAlphaRatio = settings.opacity * 0.2f;
        data.edgeEffectFullAlphaRatio = settings.opacity * 0.7f;
        data.fillTextureEffectAlphaFadeInTime = settings.fadeSeconds;
        data.fillTextureEffectAlphaFadeOutTime = settings.fadeSeconds;
        data.edgeEffectAlphaFadeInTime = settings.fadeSeconds;
        data.edgeEffectAlphaFadeOutTime = settings.fadeSeconds;
        if (active && (active->finished || activeRoot.get() != target->Get3D())) {
            StopGlow(true);
        }
        if (!active && !AcquireGlow(target.get())) {
            return;
        }
        active->lifetime = active->age + 2.0f;
    }

    void ResetDestinationGlow(bool clearRestored)
    {
        StopGlow(true);
        destination = {};
        if (clearRestored && shader) {
            if (auto* effects = RE::ProcessLists::GetSingleton()) {
                effects->ForEachShaderEffect([](RE::ShaderReferenceEffect* effect) {
                    if (effect->effectData == shader) {
                        effect->finished = true;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
        }
    }
}
