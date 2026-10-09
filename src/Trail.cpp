#include "Trail.h"
#include "ChickenGuide.h"
#include "DestinationGlow.h"
#include "Keybind.h"
#include "RefreshState.h"
#include "Settings.h"
#include "VisualMath.h"

#include <RE/H/Hazard.h>
#include <RE/B/BSModelDB.h>
#include <RE/N/NiPSysData.h>
#include <Windows.h>
#include <chrono>
#include <bit>

namespace AQT
{
    namespace
    {
        struct Marker
        {
            RE::NiPoint3 position;
            RE::NiPoint3 forward;
        };

        struct Wisp
        {
            RE::NiPointer<RE::NiNode> root;
            RE::NiPointer<RE::BSGeometry> geometry;
            std::array<RE::NiPointer<RE::BSGeometry>, 2> sparks;
            RE::NiPointer<RE::NiTimeController> particleControllers;
            float particleTime{0.0f};
            RE::NiPoint3 position;
            RE::NiPoint3 target;
            RE::NiPoint3 forward;
            RE::NiPoint3 targetForward;
            float alpha{0.0f};
            float visibleAlpha{0.0f};
            float phase{0.0f};
            bool wanted{false};
            bool relocating{false};
            RE::NiPointer<RE::NiPointLight> light;
            RE::NiPointer<RE::BSLight> sceneLight;
            float lightAlpha{0.0f};
            float routeDistance{0.0f};
            float routeVisibility{0.0f};
        };

        struct Route
        {
            RE::SpellItem* spell{nullptr};
            RE::BGSHazard* hazard{nullptr};
            RE::NiPoint3 origin;
            std::vector<Marker> markers;
            RE::NiPointer<RE::NiAVObject> model;
            RE::ObjectRefHandle destination;
        };

        struct RouteTimings
        {
            float seconds{0.0f};
            unsigned requests{0};
            unsigned completed{0};
            unsigned timeouts{0};
            double submitMilliseconds{0.0};
            double longestSubmit{0.0};
            float readySeconds{0.0f};
            float longestReady{0.0f};
            unsigned frames{0};
            double visualMilliseconds{0.0};
            double longestVisual{0.0};
            double longestPresent{0.0};
        };

        std::array<Route, 2> routes;
        RefreshState refresh;
        RouteTimings timings;
        RE::BGSKeyword* dungeonKeyword{nullptr};
        RE::Setting* guideWaypoints{nullptr};
        RE::Setting* guideSpacing{nullptr};
        bool loaded{false};
        bool cleanupPending{true};
        float checkElapsed{0.0f};
        float targetElapsed{0.0f};
        bool hasTargets{false};
        RE::FormID lastCell{0};
        RE::FormID lastWorldspace{0};
        std::uint64_t lastRevision{0};
        std::uint64_t lastTargets{0};
        std::atomic<bool> requestedRefresh{false};
        RE::NiPoint3 requestOrigin;
        bool appearanceLogged{false};
        bool missingMeshLogged{false};
        std::vector<Wisp> wisps;
        RE::NiPointer<RE::NiNode> visualParent;
        RE::NiPointer<RE::NiNode> mistModel;
        bool missingMistLogged{false};
        Settings appearance;
        std::vector<std::array<float, 3>> displayedRoute;
        RouteProgressCache progressCache;
        bool routeNeedsRefresh{true};
        float routeAge{0.0f};
        float height{20.0f};
        double animationTime{0.0};
        RE::NiPoint3 previousPlayerPosition;
        bool havePlayerPosition{false};
        RE::NiPointer<RE::ShadowSceneNode> lightScene;
        bool communityShaders{false};
        bool lightFailureLogged{false};
        float lightCheckElapsed{0.0f};
        std::vector<std::pair<float, std::size_t>> lightCandidates;

        void RemoveLight(Wisp& wisp)
        {
            if (wisp.light) {
                wisp.light->GetLightRuntimeData().fade = 0.0f;
                wisp.light->CullNode(true);
                if (lightScene && wisp.sceneLight) {
                    lightScene->RemoveLight(wisp.sceneLight);
                }
                if (auto* parent = wisp.light->parent) {
                    parent->DetachChild(wisp.light.get());
                }
            }
            wisp.sceneLight.reset();
            wisp.light.reset();
            wisp.lightAlpha = 0.0f;
        }

        void UpdateLights(const RE::NiPoint3& playerPosition, float speed, float delta)
        {
            auto* scene = communityShaders && appearance.trailLights ?
                RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0] : nullptr;
            if (scene != lightScene.get()) {
                for (auto& wisp : wisps) {
                    RemoveLight(wisp);
                }
                lightScene.reset(scene);
                lightCheckElapsed = 0.0f;
            }
            if (!scene) {
                return;
            }
            constexpr unsigned maxLights = 12;
            constexpr float maxDistance = 3000.0f;
            const auto inverse = visualParent->world.Invert();
            const float fadeDistance = std::max({192.0f, appearance.lightRadius,
                std::min(speed * appearance.fadeSeconds, 768.0f)});
            const auto colour = appearance.customColour ? appearance.colour : Settings{}.colour;
            unsigned count{0};
            for (auto& wisp : wisps) {
                if (!wisp.light) {
                    continue;
                }
                const auto relative = wisp.position - playerPosition;
                const float distance = relative.Length();
                const float clearance = PlayerClearance(relative.x, relative.y, appearance.startDistance, speed, fadeDistance);
                const float rangeFade = FadeOpacity(std::clamp((maxDistance - distance) / 400.0f, 0.0f, 1.0f));
                const float target = wisp.relocating ? 0.0f : FadeOpacity(wisp.alpha) * wisp.routeVisibility * clearance * rangeFade;
                wisp.lightAlpha = AdvanceFade(wisp.lightAlpha, target, delta, appearance.fadeSeconds);
                if (wisp.lightAlpha <= 0.0f && target <= 0.0f) {
                    RemoveLight(wisp);
                    continue;
                }
                ++count;
                auto& data = wisp.light->GetLightRuntimeData();
                data.diffuse = {colour[0], colour[1], colour[2]};
                if (data.radius.x != appearance.lightRadius) {
                    data.radius = {appearance.lightRadius, appearance.lightRadius, 1.0f};
                    wisp.light->SetLightAttenuation(appearance.lightRadius);
                }
                data.fade = appearance.lightBrightness * appearance.brightness * appearance.opacity *
                    FadeOpacity(wisp.lightAlpha);
                wisp.light->CullNode(data.fade <= 0.0f);
                RE::NiTransform transform;
                transform.translate = wisp.position + RE::NiPoint3{0.0f, 0.0f, height};
                wisp.light->local = inverse * transform;
                RE::NiUpdateData update{};
                wisp.light->Update(update);
            }
            lightCheckElapsed += delta;
            if (count >= maxLights || lightCheckElapsed < 0.1f) {
                return;
            }
            lightCheckElapsed = 0.0f;
            auto& candidates = lightCandidates;
            candidates.clear();
            candidates.reserve(wisps.size());
            for (std::size_t i = 0; i < wisps.size(); ++i) {
                const auto& wisp = wisps[i];
                const float distance = (wisp.position - playerPosition).SqrLength();
                if (!wisp.light && wisp.wanted && !wisp.relocating && wisp.visibleAlpha > 0.05f &&
                    distance < (maxDistance - 400.0f) * (maxDistance - 400.0f)) {
                    candidates.emplace_back(distance, i);
                }
            }
            std::ranges::sort(candidates);
            const float spacing = std::max(192.0f, appearance.lightRadius);
            for (const auto& [distance, index] : candidates) {
                if (count >= maxLights) {
                    break;
                }
                auto& wisp = wisps[index];
                const bool crowded = std::ranges::any_of(wisps, [&](const auto& other) {
                    return other.light && (other.position - wisp.position).SqrLength() < spacing * spacing;
                });
                if (crowded) {
                    continue;
                }
                wisp.light.reset(RE::NiPointLight::Create());
                if (!wisp.light) {
                    continue;
                }
                wisp.light->name = "AQT_TrailLight";
                auto& data = wisp.light->GetLightRuntimeData();
                // Community Shaders stores its Initialised flag in ambient.red.
                data.ambient = {std::bit_cast<float>(std::uint32_t{1 << 8}), 1.0f, 0.0f};
                data.diffuse = {colour[0], colour[1], colour[2]};
                data.radius = {appearance.lightRadius, appearance.lightRadius, 1.0f};
                data.fade = 0.0f;
                wisp.light->SetLightAttenuation(appearance.lightRadius);
                wisp.light->CullNode(true);
                RE::NiTransform transform;
                transform.translate = wisp.position + RE::NiPoint3{0.0f, 0.0f, height};
                wisp.light->local = inverse * transform;
                visualParent->AttachChild(wisp.light.get());
                RE::NiUpdateData update{};
                wisp.light->Update(update);
                RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
                params.dynamic = true;
                params.affectLand = true;
                params.affectWater = true;
                params.neverFades = true;
                params.fov = 6.2831853f;
                params.falloff = 1.0f;
                params.nearDistance = 5.0f;
                wisp.sceneLight.reset(scene->AddLight(wisp.light.get(), params));
                if (!wisp.sceneLight) {
                    RemoveLight(wisp);
                    if (!lightFailureLogged) {
                        spdlog::warn("Could not register a trail light with Skyrim");
                        lightFailureLogged = true;
                    }
                    continue;
                }
                ++count;
            }
        }

        void ClearVisuals()
        {
            ResetChickenGuide();
            ResetDestinationGlow();
            for (auto& wisp : wisps) {
                RemoveLight(wisp);
                wisp.root->CullNode(true);
                if (visualParent && wisp.root->parent == visualParent.get()) {
                    visualParent->DetachChild(wisp.root.get());
                }
            }
            wisps.clear();
            lightScene.reset();
            lightCheckElapsed = 0.0f;
            displayedRoute.clear();
            progressCache.Invalidate();
            routeNeedsRefresh = true;
            routeAge = 0.0f;
            visualParent.reset();
            havePlayerPosition = false;
        }

        std::vector<RE::GuideEffect*> OwnedEffects(RE::PlayerCharacter* player, const Route& route)
        {
            std::vector<RE::GuideEffect*> result;
            if (player && route.spell) {
                auto* target = player->GetMagicTarget();
                if (auto* effects = target ? target->GetActiveEffectList() : nullptr) {
                    for (auto* effect : *effects) {
                        if (effect && effect->spell == route.spell && effect->GetBaseObject() &&
                            effect->GetBaseObject()->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kGuide) {
                            result.push_back(static_cast<RE::GuideEffect*>(effect));
                        }
                    }
                }
            }
            return result;
        }

        void ClearRoute(Route& route)
        {
            for (auto* effect : OwnedEffects(RE::PlayerCharacter::GetSingleton(), route)) {
                if (effect->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
                    continue;
                }
                for (auto handle : effect->hazards) {
                    if (auto reference = handle.get(); reference && reference->GetBaseObject() == route.hazard) {
                        if (auto* hazard = reference->As<RE::Hazard>()) {
                            hazard->GetHazardRuntimeData().lifetime = 0.01f;
                        }
                        if (auto* root = reference->Get3D()) {
                            root->CullNode(true);
                        }
                    }
                }
                effect->Dispel(true);
            }
            route.destination = {};
            route.markers.clear();
            route.model.reset();
        }

        void InvalidateRoute(bool retireVisuals = false)
        {
            if (cleanupPending || refresh.active >= 0 || refresh.pending >= 0) {
                for (auto& route : routes) {
                    ClearRoute(route);
                }
            }
            cleanupPending = false;
            refresh.Reset();
            routeNeedsRefresh = true;
            routeAge = 0.0f;
            if (retireVisuals) {
                SetGlowDestination({});
                for (auto& wisp : wisps) {
                    wisp.wanted = false;
                    wisp.relocating = false;
                }
            }
        }

        void ClearTrail()
        {
            ClearVisuals();
            InvalidateRoute();
        }

        RE::NiNode* GetVisualParent(RE::TESObjectCELL* cell)
        {
            auto* data = cell->GetRuntimeData().loadedData;
            if (!data || !data->cell3D) {
                return nullptr;
            }
            if (cell->IsInteriorCell()) {
                // DynamicNode participates in the interior portal graph's render traversal.
                auto* dynamic = data->cell3D->GetObjectByName("DynamicNode");
                return dynamic ? dynamic->AsNode() : nullptr;
            }
            return data->cell3D.get();
        }

        bool MoveVisualsToCell(RE::TESObjectCELL* cell)
        {
            auto* destination = GetVisualParent(cell);
            if (!destination) {
                return false;
            }
            if (visualParent.get() == destination) {
                return true;
            }
            const auto inverse = destination->world.Invert();
            RE::NiUpdateData update{};
            const auto moveNode = [&](RE::NiAVObject* node) {
                const auto world = node->world;
                if (auto* parent = node->parent) {
                    parent->DetachChild(node);
                }
                node->local = inverse * world;
                destination->AttachChild(node);
                node->Update(update);
            };
            for (auto& wisp : wisps) {
                moveNode(wisp.root.get());
                if (wisp.light) {
                    moveNode(wisp.light.get());
                }
            }
            visualParent.reset(destination);
            visualParent->UpdateUpwardPass(update);
            return true;
        }

        bool InDungeon(RE::PlayerCharacter* player)
        {
            auto* location = player->GetCurrentLocation();
            for (unsigned depth = 0; location && depth < 32; ++depth, location = location->parentLoc) {
                if (dungeonKeyword && location->HasKeyword(dungeonKeyword)) {
                    return true;
                }
            }
            return false;
        }

        std::pair<bool, std::uint64_t> TargetSignature(RE::PlayerCharacter* player)
        {
            bool found{false};
            std::uint64_t signature{0};
            RE::BSSpinLockGuard guard(player->GetQuestTargetsLock());
            for (const auto& [quest, targets] : player->GetQuestTargets()) {
                if (!quest || !quest->IsActive() || !targets || targets->empty()) {
                    continue;
                }
                found = true;
                signature ^= (static_cast<std::uint64_t>(quest->GetFormID()) << 32) ^ targets->size();
                for (const auto* target : *targets) {
                    signature ^= reinterpret_cast<std::uintptr_t>(target);
                }
            }
            return {found, signature};
        }

        RE::BSGeometry* StyleRoot(RE::NiAVObject* root, const Settings& settings, const char* name = "AQT_Wisp")
        {
            auto* object = root->GetObjectByName(name);
            auto* geometry = object ? object->AsGeometry() : nullptr;
            if (!geometry) {
                if (!missingMeshLogged) {
                    spdlog::warn("A guide marker has no {} geometry; install the complete Active Quest Trail package.", name);
                    missingMeshLogged = true;
                }
                return nullptr;
            }
            auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get();
            if (!property || !property->material || property->material->GetType() != RE::BSShaderMaterial::Type::kEffect) {
                return nullptr;
            }
            auto* material = static_cast<RE::BSEffectShaderMaterial*>(property->material->Create());
            if (!material) {
                return nullptr;
            }
            material->CopyMembers(property->material);
            const auto colour = settings.customColour ? settings.colour : Settings{}.colour;
            material->baseColor = {colour[0], colour[1], colour[2], 0.0f};
            material->baseColorScale = settings.brightness * 3.0f;
            property->SetMaterial(material, true);
            material->~BSEffectShaderMaterial();
            RE::free(material);
            property->SetupGeometry(geometry);
            property->FinishSetupGeometry(geometry);
            if (!appearanceLogged) {
                spdlog::info("Custom trail material applied: RGB=({:.3f}, {:.3f}, {:.3f}), brightness={:.3f}, opacity={:.3f}",
                    colour[0], colour[1], colour[2], settings.brightness, settings.opacity);
                appearanceLogged = true;
            }
            return geometry;
        }

        RE::NiMatrix3 Orientation(RE::NiPoint3 forward)
        {
            RE::NiPoint3 right{forward.y, -forward.x, 0.0f};
            if (right.SqrLength() < 0.01f) {
                return RE::NiMatrix3{};
            }
            forward.Unitize();
            right.Unitize();
            const auto up = right.Cross(forward);
            RE::NiMatrix3 rotation;
            for (unsigned i = 0; i < 3; ++i) {
                rotation.entry[i][0] = right[i];
                rotation.entry[i][1] = forward[i];
                rotation.entry[i][2] = up[i];
            }
            return rotation;
        }

        void AdvanceParticles(Wisp& wisp, float delta)
        {
            float advance = appearance.animate ? std::clamp(delta * appearance.animationSpeed, 0.0f, 0.1f) : 0.0f;
            if (wisp.particleTime < 0.65f) {
                advance = std::max(advance, 1.0f / 30.0f);
            }
            const unsigned steps = static_cast<unsigned>(std::ceil(advance * 30.0f));
            if (!steps) {
                return;
            }
            wisp.geometry->controllers = wisp.particleControllers;
            for (unsigned i = 0; i < steps; ++i) {
                wisp.particleTime += advance / steps;
                RE::NiUpdateData update{};
                update.time = wisp.particleTime;
                update.flags.set(RE::NiUpdateData::Flag::kDirty);
                wisp.root->Update(update);
            }
            wisp.geometry->controllers.reset();
        }

        bool PrepareRoute(RE::PlayerCharacter* player, Route& route)
        {
            std::vector<RE::NiPointer<RE::TESObjectREFR>> references;
            std::size_t expected{0};
            for (auto* effect : OwnedEffects(player, route)) {
                if (effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
                    continue;
                }
                expected += effect->hazards.size();
                for (auto handle : effect->hazards) {
                    if (auto reference = handle.get(); reference && !reference->IsDisabled() &&
                        reference->GetBaseObject() == route.hazard && reference->Get3D()) {
                        reference->Get3D()->CullNode(true);
                        references.push_back(std::move(reference));
                    }
                }
            }
            if (!expected || references.size() != expected) {
                return false;
            }
            if (references.size() > 1 && (references.back()->GetPosition() - route.origin).SqrLength() <
                                             (references.front()->GetPosition() - route.origin).SqrLength()) {
                std::ranges::reverse(references);
            }
            std::vector<Marker> prepared;
            prepared.reserve(references.size());
            for (std::size_t i = 0; i < references.size(); ++i) {
                const auto position = references[i]->GetPosition();
                auto forward = position - (i ? references[i - 1]->GetPosition() : route.origin);
                for (std::size_t j = i + 1; j < references.size(); ++j) {
                    const auto next = references[j]->GetPosition() - position;
                    if (next.SqrLength() > 0.01f) {
                        forward = next;
                        break;
                    }
                }
                forward.Unitize();
                prepared.push_back({position, forward});
            }
            route.destination = {};
            for (auto* effect : OwnedEffects(player, route)) {
                if (!effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive)) {
                    route.destination = FindRouteDestination(player, effect);
                    if (route.destination) {
                        break;
                    }
                }
            }
            route.markers = std::move(prepared);
            route.model.reset(references.front()->Get3D());
            return true;
        }

        void UpdateWisps(const RE::NiPoint3& playerPosition, float delta)
        {
            if (!visualParent) {
                return;
            }
            const float blend = 1.0f - std::exp(-12.0f * delta);
            const auto movement = playerPosition - previousPlayerPosition;
            const float speed = havePlayerPosition && delta > 0.001f ? std::hypot(movement.x, movement.y) / delta : 0.0f;
            previousPlayerPosition = playerPosition;
            havePlayerPosition = true;
            animationTime += static_cast<double>(delta) * appearance.animationSpeed;
            unsigned sparkPairs{0};
            const auto progress = progressCache.Get(displayedRoute, {playerPosition.x, playerPosition.y, playerPosition.z});
            const float chickenEnd = UpdateChickenGuide(visualParent.get(), displayedRoute, progress.travelled,
                delta, appearance, routeAge <= 5.0f);
            if (appearance.trailStyle == 1 && !appearance.chickenTrail &&
                chickenEnd != std::numeric_limits<float>::max()) {
                for (auto& wisp : wisps) {
                    wisp.root->CullNode(true);
                    wisp.visibleAlpha = 0.0f;
                    if (wisp.light) {
                        RemoveLight(wisp);
                    }
                }
                RE::NiUpdateData update{};
                visualParent->UpdateUpwardPass(update);
                return;
            }
            height += (appearance.height - height) * blend;
            const auto inverse = visualParent->world.Invert();
            for (auto& wisp : wisps) {
                wisp.visibleAlpha = 0.0f;
                if (wisp.wanted && !wisp.relocating) {
                    wisp.routeVisibility = RouteVisibility(wisp.routeDistance, progress.travelled) *
                        SmoothVisibility(chickenEnd - wisp.routeDistance);
                }
                const auto offset = wisp.target - playerPosition;
                if (routeAge > 5.0f || (!appearance.anchorTrail && offset.SqrLength() < 96.0f * 96.0f &&
                                          offset.Dot(wisp.targetForward) < -24.0f)) {
                    wisp.wanted = false;
                }
                wisp.alpha = AdvanceFade(wisp.alpha, wisp.wanted && !wisp.relocating, delta, appearance.fadeSeconds);
                if (wisp.alpha <= 0.0f) {
                    if (wisp.wanted && wisp.relocating && wisp.lightAlpha <= 0.0f) {
                        wisp.position = wisp.target;
                        wisp.forward = wisp.targetForward;
                        wisp.relocating = false;
                    }
                    wisp.root->CullNode(true);
                    continue;
                }
                if (!wisp.relocating) {
                    wisp.position += (wisp.target - wisp.position) * blend;
                    wisp.forward += (wisp.targetForward - wisp.forward) * blend;
                    wisp.forward.Unitize();
                }
                const auto relative = wisp.position - playerPosition;
                const float clearance = PlayerClearance(relative.x, relative.y, appearance.startDistance, speed);
                wisp.visibleAlpha = appearance.opacity * FadeOpacity(wisp.alpha) * wisp.routeVisibility * clearance;
                if (wisp.visibleAlpha <= 0.0f) {
                    wisp.root->CullNode(true);
                    continue;
                }
                RE::NiTransform transform;
                transform.translate = wisp.position + RE::NiPoint3{0.0f, 0.0f, height};
                transform.rotate = Orientation(wisp.forward);
                wisp.root->local = inverse * transform;
                wisp.geometry->GetGeometryRuntimeData().shaderProperty->SetMaterialAlpha(wisp.visibleAlpha);
                wisp.root->CullNode(false);
                const bool showSparks = appearance.particles && sparkPairs < 12 && relative.SqrLength() < 1200.0f * 1200.0f;
                if (showSparks) {
                    ++sparkPairs;
                }
                for (std::size_t i = 0; i < wisp.sparks.size(); ++i) {
                    auto* spark = wisp.sparks[i].get();
                    if (!spark) {
                        continue;
                    }
                    spark->CullNode(!showSparks);
                    if (showSparks) {
                        const float life = static_cast<float>(std::fmod(animationTime * 0.65 + wisp.phase + i * 0.5, 1.0));
                        const float angle = life * 6.28318530718f + wisp.phase;
                        spark->local.translate = {14.0f * std::sin(angle), -64.0f + life * 128.0f, 4.0f + life * 24.0f};
                        spark->local.scale = 0.8f + 0.6f * std::sin(life * 3.14159265f);
                        spark->GetGeometryRuntimeData().shaderProperty->SetMaterialAlpha(
                            wisp.visibleAlpha * std::sin(life * 3.14159265f) * SmoothVisibility(1200.0f - relative.Length()));
                    }
                }
                RE::NiUpdateData update{};
                update.time = wisp.particleTime;
                wisp.root->Update(update);
                AdvanceParticles(wisp, delta);
            }
            const auto poolLimit = static_cast<std::size_t>(appearance.trailLength / 48.0f) + 9;
            for (auto it = wisps.begin(); wisps.size() > poolLimit && it != wisps.end();) {
                if (!it->wanted && it->alpha <= 0.0f && it->lightAlpha <= 0.0f) {
                    RemoveLight(*it);
                    visualParent->DetachChild(it->root.get());
                    it = wisps.erase(it);
                } else {
                    ++it;
                }
            }
            UpdateLights(playerPosition, speed, delta);
            RE::NiUpdateData update{};
            visualParent->UpdateUpwardPass(update);
        }

        void PresentRoute(RE::TESObjectCELL* cell, const Route& route)
        {
            const auto start = std::chrono::steady_clock::now();
            auto* destination = GetVisualParent(cell);
            if (!destination || !route.model) {
                return;
            }
            if (!mistModel && RE::BSModelDB::Demand("ActiveQuestTrail\\TrailMist.nif", mistModel, {}) != RE::BSResource::ErrorCode::kNone) {
                if (!missingMistLogged) {
                    spdlog::error("Cannot load TrailMist.nif; install the complete Active Quest Trail package");
                    missingMistLogged = true;
                }
                return;
            }
            if (!mistModel) {
                return;
            }
            if (visualParent.get() != destination) {
                ClearVisuals();
                visualParent.reset(destination);
                height = appearance.height;
            }
            for (auto& wisp : wisps) {
                wisp.wanted = false;
            }
            const auto sampleLimit = static_cast<std::size_t>(appearance.trailLength / 48.0f) + 1;
            const auto poolLimit = sampleLimit + 8;
            std::vector<Marker> samples;
            samples.reserve(sampleLimit);
            float distanceToNext{0.0f};
            for (std::size_t i = 1; i < route.markers.size() && samples.size() < sampleLimit; ++i) {
                auto direction = route.markers[i].position - route.markers[i - 1].position;
                const float length = direction.Length();
                if (length < 0.01f) {
                    continue;
                }
                direction /= length;
                while (distanceToNext <= length && samples.size() < sampleLimit) {
                    samples.push_back({route.markers[i - 1].position + direction * distanceToNext, direction});
                    distanceToNext += 48.0f;
                }
                distanceToNext -= length;
            }
            if (samples.empty() && !route.markers.empty()) {
                samples.push_back(route.markers.front());
            }
            displayedRoute.clear();
            progressCache.Invalidate();
            displayedRoute.push_back({route.origin.x, route.origin.y, route.origin.z});
            float sampleDistance{0.0f};
            for (const auto& sample : samples) {
                Wisp* chosen{nullptr};
                float nearest = 120.0f * 120.0f;
                for (auto& wisp : wisps) {
                    const float distance = (wisp.position - sample.position).SqrLength();
                    if (!wisp.wanted && wisp.alpha > 0.0f && distance < nearest && wisp.forward.Dot(sample.forward) > 0.0f) {
                        chosen = &wisp;
                        nearest = distance;
                    }
                }
                if (!chosen) {
                    for (auto& wisp : wisps) {
                        if (!wisp.wanted && wisp.alpha <= 0.0f && wisp.lightAlpha <= 0.0f) {
                            chosen = &wisp;
                            break;
                        }
                    }
                }
                if (!chosen && wisps.size() < poolLimit) {
                    RE::NiPointer<RE::NiObject> copy;
                    mistModel->CreateDeepCopy(copy);
                    auto* root = copy ? copy->AsNode() : nullptr;
                    auto* geometry = root ? StyleRoot(root, appearance) : nullptr;
                    if (!geometry || !geometry->AsParticlesGeom() || !geometry->GetControllers()) {
                        if (!missingMistLogged) {
                            spdlog::error("TrailMist.nif is missing its particle system or controllers; install the complete Active Quest Trail package");
                            missingMistLogged = true;
                        }
                        break;
                    }
                    root->CullNode(true);
                    geometry->CullNode(false);
                    visualParent->AttachChild(root);
                    Wisp wisp;
                    wisp.root.reset(root);
                    wisp.geometry.reset(geometry);
                    RE::NiTimeController::StartAnimations(geometry);
                    wisp.particleControllers = std::move(geometry->controllers);
                    wisp.sparks[0].reset(StyleRoot(root, appearance, "AQT_Spark0"));
                    wisp.sparks[1].reset(StyleRoot(root, appearance, "AQT_Spark1"));
                    wisp.phase = static_cast<float>(wisps.size()) * 2.39996323f;
                    wisps.push_back(std::move(wisp));
                    chosen = &wisps.back();
                }
                if (!chosen) {
                    float distance = std::numeric_limits<float>::max();
                    for (auto& wisp : wisps) {
                        const float candidate = (wisp.position - sample.position).SqrLength();
                        if (!wisp.wanted && candidate < distance) {
                            chosen = &wisp;
                            distance = candidate;
                        }
                    }
                }
                if (!chosen) {
                    break;
                }
                chosen->relocating = (chosen->alpha > 0.0f || chosen->lightAlpha > 0.0f) &&
                    (chosen->position - sample.position).SqrLength() > 120.0f * 120.0f;
                if (chosen->alpha <= 0.0f && chosen->lightAlpha <= 0.0f) {
                    chosen->position = sample.position;
                    chosen->forward = sample.forward;
                }
                const auto& previous = displayedRoute.back();
                sampleDistance += (sample.position - RE::NiPoint3{previous[0], previous[1], previous[2]}).Length();
                chosen->routeDistance = sampleDistance;
                chosen->target = sample.position;
                chosen->targetForward = sample.forward;
                chosen->wanted = true;
                displayedRoute.push_back({sample.position.x, sample.position.y, sample.position.z});
            }
            if (displayedRoute.size() == 1) {
                displayedRoute.clear();
                progressCache.Invalidate();
            }
            SetGlowDestination(route.destination);
            routeAge = 0.0f;
            timings.longestPresent = std::max(timings.longestPresent,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }

        void ReportTimings()
        {
            if (timings.seconds < 30.0f) {
                return;
            }
            if (timings.requests || !wisps.empty()) {
                unsigned liveParticles{0};
                for (auto& wisp : wisps) {
                    auto* particles = wisp.geometry->AsParticlesGeom();
                    if (particles && wisp.visibleAlpha > 0.0f) {
                        const auto& data = particles->GetParticlesRuntimeData().particleData;
                        if (data) {
                            liveParticles += data->GetParticlesRuntimeData().numVertices;
                        }
                    }
                }
                spdlog::info("Routes in {:.1f}s: {} requests, {} ready, {} timeouts; cast submission avg {:.3f}ms/max {:.3f}ms; readiness avg {:.0f}ms/max {:.0f}ms",
                    timings.seconds, timings.requests, timings.completed, timings.timeouts,
                    timings.requests ? timings.submitMilliseconds / timings.requests : 0.0, timings.longestSubmit,
                    timings.completed ? timings.readySeconds * 1000.0f / timings.completed : 0.0f, timings.longestReady * 1000.0f);
                spdlog::info("Mist: {} emitters pooled, {} live particles in visible emitters; visual update avg {:.3f}ms/max {:.3f}ms; route presentation max {:.3f}ms",
                    wisps.size(), liveParticles, timings.frames ? timings.visualMilliseconds / timings.frames : 0.0,
                    timings.longestVisual, timings.longestPresent);
                if (appearance.trailLights) {
                    const auto lightCount = std::ranges::count_if(wisps, [](const auto& wisp) { return wisp.sceneLight != nullptr; });
                    spdlog::info("Trail lights: {} registered (limit 12), Community Shaders {}", lightCount, communityShaders ? "loaded" : "unavailable");
                }
                if (refresh.active >= 0 && !displayedRoute.empty()) {
                    const auto& markers = routes[refresh.active].markers;
                    float suppliedLength{0.0f};
                    for (std::size_t i = 1; i < markers.size(); ++i) {
                        suppliedLength += (markers[i].position - markers[i - 1].position).Length();
                    }
                    const auto displayed = MeasureRoute(displayedRoute, displayedRoute.front());
                    spdlog::info("Route length: requested {:.0f}, supplied {:.0f} ({} probes), displayed {:.0f} game units",
                        appearance.trailLength, suppliedLength, markers.size(), displayed.remaining);
                }
            }
            timings = {};
        }

        void Tick(RE::PlayerCharacter* player, float delta)
        {
            SavePendingSettings();
            if (!loaded || !routes[0].spell || !player || !player->Is3DLoaded()) {
                return;
            }
            auto* ui = RE::UI::GetSingleton();
            if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
                return;
            }
            const auto settings = GetSettings();
            if (!UpdateTrailVisibility(settings, delta)) {
                ClearTrail();
                SetStatus(!settings.enabled ? "StatusDisabled" : settings.holdToShow ?
                    settings.holdKey ? "StatusHoldKey" : "StatusSetHoldKey" :
                    settings.timedKey ? "StatusTimedKey" : "StatusSetTimedKey");
                return;
            }
            refresh.Advance(delta);
            checkElapsed += delta;
            targetElapsed += delta;
            timings.seconds += delta;
            routeAge = !appearance.anchorTrail || routeNeedsRefresh ? routeAge + delta : 0.0f;
            const auto visualStart = std::chrono::steady_clock::now();
            UpdateWisps(player->GetPosition(), delta);
            const double visualTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - visualStart).count();
            ++timings.frames;
            timings.visualMilliseconds += visualTime;
            timings.longestVisual = std::max(timings.longestVisual, visualTime);
            if (checkElapsed < 0.05f) {
                return;
            }
            checkElapsed = 0.0f;
            ReportTimings();
            if (cleanupPending) {
                ClearTrail();
            }
            const bool anchoringChanged = settings.anchorTrail != appearance.anchorTrail;
            const bool lengthChanged = settings.trailLength != appearance.trailLength;
            appearance = settings;
            auto* cell = player->GetParentCell();
            const char* hidden = player->IsDead() ? "StatusDead" :
                                 !cell ? "StatusWaitingForArea" :
                                 settings.hideIndoors && cell->IsInteriorCell() ? "StatusIndoors" :
                                 settings.hideDungeons && InDungeon(player) ? "StatusDungeon" :
                                 settings.hideInCombat && player->IsInCombat() ? "StatusCombat" : nullptr;
            if (hidden) {
                ClearTrail();
                SetStatus(hidden);
                return;
            }
            const bool cellChanged = cell->GetFormID() != lastCell;
            auto* worldspace = cell->GetRuntimeData().worldSpace;
            const auto worldspaceID = !cell->IsInteriorCell() && worldspace ? worldspace->GetFormID() : 0;
            bool targetChanged{false};
            if (targetElapsed >= 0.1f || cellChanged) {
                const auto [found, targets] = TargetSignature(player);
                hasTargets = found;
                targetChanged = targets != lastTargets;
                lastTargets = targets;
                targetElapsed = 0.0f;
            }
            const bool manualRefresh = requestedRefresh.exchange(false);
            if (cellChanged || targetChanged || manualRefresh || anchoringChanged || lengthChanged) {
                const bool sameExterior = lastWorldspace && worldspaceID == lastWorldspace;
                if (cellChanged && (!sameExterior || !MoveVisualsToCell(cell))) {
                    ClearTrail();
                } else {
                    InvalidateRoute(targetChanged);
                }
                lastCell = cell->GetFormID();
                lastWorldspace = worldspaceID;
            }
            if (!hasTargets) {
                ClearTrail();
                SetStatus("StatusNoObjective");
                return;
            }
            UpdateDestinationGlow(player, settings, routeAge <= 5.0f);
            const auto revision = SettingsRevision();
            if (revision != lastRevision) {
                appearanceLogged = false;
                for (auto& wisp : wisps) {
                    StyleRoot(wisp.root.get(), settings);
                    wisp.geometry->GetGeometryRuntimeData().shaderProperty->SetMaterialAlpha(wisp.visibleAlpha);
                    for (const auto* name : {"AQT_Spark0", "AQT_Spark1"}) {
                        StyleRoot(wisp.root.get(), settings, name);
                    }
                }
                lastRevision = revision;
            }
            if (refresh.pending >= 0) {
                if (PrepareRoute(player, routes[refresh.pending])) {
                    ++timings.completed;
                    timings.readySeconds += refresh.pendingAge;
                    timings.longestReady = std::max(timings.longestReady, refresh.pendingAge);
                    const int previous = refresh.Commit();
                    PresentRoute(cell, routes[refresh.active]);
                    if (previous >= 0) {
                        ClearRoute(routes[previous]);
                    }
                } else if (refresh.TimedOut()) {
                    ++timings.timeouts;
                    ClearRoute(routes[refresh.pending]);
                    refresh.pending = -1;
                    refresh.retryDelay = 0.5f;
                }
            }
            const float moved = (player->GetPosition() - requestOrigin).SqrLength();
            const auto position = player->GetPosition();
            const auto progress = progressCache.Get(displayedRoute, {position.x, position.y, position.z});
            routeNeedsRefresh = refresh.active < 0 || routeAge > 5.0f || displayedRoute.empty() ||
                NeedsAnchoredRoute(progress, settings.offRouteDistance, settings.extendDistance);
            const bool due = settings.anchorTrail ? refresh.Due(routeNeedsRefresh) :
                refresh.Due(moved, settings.refreshWhileMoving, settings.refreshSeconds);
            if (due) {
                auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
                if (!caster) {
                    SetStatus("StatusWaitingForPlayer");
                    return;
                }
                auto& route = routes[refresh.Begin()];
                ClearRoute(route);
                requestOrigin = player->GetPosition();
                route.origin = requestOrigin;
                const auto start = std::chrono::steady_clock::now();
                caster->CastSpellImmediate(route.spell, true, player, 1.0f, false, 0.0f, player);
                const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                ++timings.requests;
                timings.submitMilliseconds += milliseconds;
                timings.longestSubmit = std::max(timings.longestSubmit, milliseconds);
            }
            const auto count = std::ranges::count_if(wisps, [](const auto& wisp) { return wisp.visibleAlpha > 0.0f; });
            SetStatus(settings.trailStyle == 1 && !settings.chickenTrail && !displayedRoute.empty() && routeAge <= 5.0f ?
                "StatusFollowingChicken" : count ? "StatusFollowingObjective" :
                !wisps.empty() && routeAge <= 5.0f ? "StatusPlayerClearance" : "StatusWaitingForRoute", count);
        }

        void ResetRuntime()
        {
            ResetTrailVisibility();
            ResetChickenGuide(true);
            ResetDestinationGlow(true);
            ClearVisuals();
            mistModel.reset();
            missingMistLogged = false;
            for (auto& route : routes) {
                route.destination = {};
                route.markers.clear();
                route.model.reset();
            }
            refresh.Reset();
            timings = {};
            cleanupPending = true;
            checkElapsed = 0.0f;
            targetElapsed = 0.0f;
            hasTargets = false;
            lastCell = 0;
            lastWorldspace = 0;
            lastTargets = 0;
            appearanceLogged = false;
            missingMeshLogged = false;
        }

        struct GuideStart
        {
            static void Hook(RE::GuideEffect* effect)
            {
                const bool owned = std::ranges::any_of(routes, [effect](const auto& route) {
                    return route.spell && effect->spell == route.spell;
                });
                if (!owned || !guideWaypoints || !guideSpacing ||
                    !std::isfinite(guideSpacing->data.f) || guideSpacing->data.f <= 0.0f) {
                    original(effect);
                    return;
                }
                struct RestoreLimit
                {
                    std::int32_t value{guideWaypoints->data.i};
                    ~RestoreLimit() { guideWaypoints->data.i = value; }
                } restore;
                // 1.6.1170 reads the waypoint limit synchronously inside GuideEffect::Start.
                guideWaypoints->data.i = static_cast<std::int32_t>(std::clamp(
                    std::ceil(GetSettings().trailLength / guideSpacing->data.f) + 2.0f, 2.0f, 512.0f));
                original(effect);
            }
            static inline REL::Relocation<decltype(Hook)> original;
        };

        struct PlayerUpdate
        {
            static void Hook(RE::PlayerCharacter* player, float delta)
            {
                original(player, delta);
                Tick(player, delta);
            }
            static inline REL::Relocation<decltype(Hook)> original;
        };
    }

    void RequestRefresh()
    {
        requestedRefresh = true;
    }

    bool InitializeTrail()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            return false;
        }
        for (unsigned i = 0; i < routes.size(); ++i) {
            auto& route = routes[i];
            route.spell = data->LookupForm<RE::SpellItem>(0x801 + i * 3, "ActiveQuestTrail.esp");
            route.hazard = data->LookupForm<RE::BGSHazard>(0x802 + i * 3, "ActiveQuestTrail.esp");
            if (!route.spell || !route.hazard || route.spell->effects.size() != 1 ||
                !route.spell->effects[0] || !route.spell->effects[0]->baseEffect ||
                route.spell->effects[0]->baseEffect->GetArchetype() != RE::EffectArchetypes::ArchetypeID::kGuide) {
                spdlog::error("ActiveQuestTrail.esp is missing or outdated; install the complete Active Quest Trail package");
                SetStatus("StatusMissingPlugin");
                return false;
            }
            spdlog::info("Trail route {}: spell={:08X}, hazard={:08X}", i, route.spell->GetFormID(), route.hazard->GetFormID());
        }
        InitializeDestinationGlow(data);
        dungeonKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x130DB);
        communityShaders = GetModuleHandleW(L"CommunityShaders.dll") != nullptr;
        spdlog::info("Optional trail lighting: Community Shaders {}", communityShaders ? "loaded" : "unavailable");
        auto* gameSettings = RE::GameSettingCollection::GetSingleton();
        guideWaypoints = gameSettings ? gameSettings->GetSetting("iMagicGuideWaypoints") : nullptr;
        guideSpacing = gameSettings ? gameSettings->GetSetting("fMagicGuideSpacing") : nullptr;
        if (!guideWaypoints || !guideSpacing || guideWaypoints->GetType() != RE::Setting::Type::kInteger ||
            guideSpacing->GetType() != RE::Setting::Type::kFloat) {
            guideWaypoints = nullptr;
            guideSpacing = nullptr;
            spdlog::warn("Clairvoyance path-length settings unavailable; using Skyrim's existing route limit");
        }
        return true;
    }

    void InstallUpdateHook()
    {
        REL::Relocation<std::uintptr_t> table{RE::VTABLE_PlayerCharacter[0]};
        PlayerUpdate::original = table.write_vfunc(0xAD, PlayerUpdate::Hook);
        spdlog::info("Player update hook installed");
        if (guideWaypoints && guideSpacing) {
            REL::Relocation<std::uintptr_t> guideTable{RE::VTABLE_GuideEffect[0]};
            GuideStart::original = guideTable.write_vfunc(0x14, GuideStart::Hook);
            spdlog::info("Private guide length hook installed; original waypoint limit={}, spacing={:.1f}",
                guideWaypoints->data.i, guideSpacing->data.f);
        }
    }

    void OnLoadStart()
    {
        loaded = false;
        ResetRuntime();
        SetStatus("StatusLoading");
    }

    void OnLoadFinished(bool success)
    {
        ResetRuntime();
        loaded = success;
        SetStatus(success ? "StatusWaitingForPlayer" : "StatusLoadFailed");
    }

    void OnSave()
    {
        if (loaded) {
            ResetDestinationGlow();
            InvalidateRoute();
        }
    }
}
