#include "ChickenGuide.h"
#include "Settings.h"
#include "VisualMath.h"

#include <RE/B/BSModelDB.h>
#include <numbers>

namespace AQT
{
    namespace
    {
        struct Bone
        {
            RE::NiPointer<RE::NiAVObject> node;
            RE::NiTransform rest;
            int leg{0};
        };

        RE::NiPointer<RE::NiNode> model;
        RE::NiPointer<RE::NiNode> root;
        std::vector<Bone> bones;
        std::vector<RE::NiPointer<RE::BSGeometry>> geometry;
        RE::NiPoint3 position;
        float heading{0.0f};
        float alpha{0.0f};
        float gait{0.0f};
        float stride{0.0f};
        bool placed{false};
        bool relocating{false};
        bool unavailable{false};
        constexpr float tau = std::numbers::pi_v<float> * 2.0f;
        constexpr float normalSpeed = 220.0f;

        void PrepareGeometry(RE::NiAVObject* object, unsigned depth = 0)
        {
            if (depth > 64) {
                return;
            }
            if (auto* shape = object->AsGeometry()) {
                auto& data = shape->GetGeometryRuntimeData();
                auto* property = data.shaderProperty.get();
                if (!property || !property->material) {
                    return;
                }
                auto* material = property->material->Create();
                if (!material) {
                    return;
                }
                material->CopyMembers(property->material);
                property->SetMaterial(material, true);
                material->~BSShaderMaterial();
                RE::free(material);
                if (data.alphaProperty) {
                    RE::NiPointer<RE::NiObject> copy;
                    data.alphaProperty->CreateDeepCopy(copy);
                    if (copy) {
                        data.alphaProperty.reset(static_cast<RE::NiAlphaProperty*>(copy.get()));
                        data.alphaProperty->SetAlphaBlending(true);
                        data.alphaProperty->SetSrcBlendMode(RE::NiAlphaProperty::AlphaFunction::kSrcAlpha);
                        data.alphaProperty->SetDestBlendMode(RE::NiAlphaProperty::AlphaFunction::kInvSrcAlpha);
                    }
                }
                property->SetupGeometry(shape);
                property->FinishSetupGeometry(shape);
                geometry.emplace_back(shape);
            }
            if (auto* node = object->AsNode()) {
                for (const auto& child : node->GetChildren()) {
                    if (child) {
                        PrepareGeometry(child.get(), depth + 1);
                    }
                }
            }
        }

        bool CreateGuide()
        {
            if (unavailable) {
                return false;
            }
            if (!model && RE::BSModelDB::Demand("Actors\\Ambient\\Chicken\\Character Assets\\Chicken.nif", model, {}) != RE::BSResource::ErrorCode::kNone) {
                unavailable = true;
            }
            RE::NiPointer<RE::NiObject> copy;
            if (model) {
                model->CreateDeepCopy(copy);
            }
            root.reset(copy ? copy->AsNode() : nullptr);
            if (!root) {
                unavailable = true;
                spdlog::warn("Chicken guide model unavailable; using the wisp trail");
                return false;
            }
            root->name = "AQT_ChickenGuide";
            root->CullNode(true);
            PrepareGeometry(root.get());
            if (geometry.empty()) {
                ResetChickenGuide();
                unavailable = true;
                spdlog::warn("Chicken guide has no usable geometry; using the wisp trail");
                return false;
            }
            // The vanilla skin has its bind-pose bones directly below the mesh root.
            for (const auto& child : root->GetChildren()) {
                if (!child || !child->AsNode()) {
                    continue;
                }
                const std::string_view name{child->name.c_str()};
                const int leg = name.starts_with("Left") || name.starts_with("LAnkle") ? -1 :
                    name.starts_with("Right") || name.starts_with("RAnkle") ? 1 : 0;
                bones.push_back({child, child->local, leg});
            }
            spdlog::info("Cosmetic chicken guide created; no actor reference or AI");
            return true;
        }

        void Animate(float speed, float delta, bool animate)
        {
            const float blend = 1.0f - std::exp(-12.0f * delta);
            stride += ((animate ? std::clamp(speed / 100.0f, 0.0f, 1.0f) : 0.0f) - stride) * blend;
            gait = std::fmod(gait + std::min(speed / 55.0f, 4.0f) * tau * delta, tau);
            for (auto& bone : bones) {
                bone.node->local = bone.rest;
                const std::string_view name{bone.node->name.c_str()};
                if (bone.leg) {
                    const float phase = gait + (bone.leg < 0 ? std::numbers::pi_v<float> : 0.0f);
                    const float swing = std::sin(phase) * stride;
                    const float lift = std::max(0.0f, std::cos(phase)) * stride;
                    RE::NiMatrix3 rotation;
                    if (name.ends_with("Thigh")) {
                        rotation.SetEulerAnglesXYZ(swing * 0.4f, 0.0f, 0.0f);
                        bone.node->local.rotate = rotation * bone.rest.rotate;
                    } else if (name.ends_with("Cafe")) {
                        bone.node->local.translate.y += swing * 3.0f;
                        bone.node->local.translate.z += lift * 2.0f;
                        rotation.SetEulerAnglesXYZ(-swing * 0.35f, 0.0f, 0.0f);
                        bone.node->local.rotate = rotation * bone.rest.rotate;
                    } else {
                        bone.node->local.translate.y += swing * 7.0f;
                        bone.node->local.translate.z += lift * 5.0f;
                    }
                } else {
                    bone.node->local.translate.z += std::abs(std::sin(gait)) * stride;
                    if (name.starts_with("Neck") || name.starts_with("Head") || name == "HEAD") {
                        bone.node->local.translate.y += std::sin(gait * 2.0f) * stride * 1.5f;
                    }
                }
            }
        }
    }

    float UpdateChickenGuide(RE::NiNode* parent, std::span<const std::array<float, 3>> route,
        float playerDistance, float delta, const Settings& settings, bool visible)
    {
        const float noLimit = std::numeric_limits<float>::max();
        if (!parent || route.empty() || settings.trailStyle != 1) {
            ResetChickenGuide();
            return noLimit;
        }
        if (!root && !CreateGuide()) {
            return noLimit;
        }
        if (root->parent != parent) {
            if (root->parent) {
                root->parent->DetachChild(root.get());
            }
            parent->AttachChild(root.get());
        }
        delta = std::clamp(delta, 0.0f, 0.1f);
        const float lead = std::max(settings.chickenDistance, settings.startDistance + 200.0f);
        const auto target = SampleRoute(route, playerDistance + lead);
        if (!placed) {
            position = {target.position[0], target.position[1], target.position[2]};
            heading = std::atan2(target.direction[0], target.direction[1]);
            placed = true;
        }
        auto progress = MeasureRoute(route, {position.x, position.y, position.z});
        const float targetDistance = std::min(playerDistance + lead, progress.travelled + progress.remaining);
        if (progress.distanceSquared > 192.0f * 192.0f ||
            playerDistance - progress.travelled > 96.0f ||
            std::abs(targetDistance - progress.travelled) > lead + 384.0f) {
            relocating = true;
        }
        const float clearance = SmoothVisibility(progress.travelled - playerDistance - 64.0f);
        alpha = AdvanceFade(alpha, visible && !relocating ? clearance : 0.0f, delta, settings.fadeSeconds);
        if (relocating && alpha <= 0.0f) {
            position = {target.position[0], target.position[1], target.position[2]};
            heading = std::atan2(target.direction[0], target.direction[1]);
            progress = MeasureRoute(route, {position.x, position.y, position.z});
            relocating = false;
        }
        const float remaining = targetDistance - progress.travelled;
        const float step = visible && !relocating ? std::clamp(remaining, -normalSpeed * delta, normalSpeed * delta) : 0.0f;
        const float distance = progress.travelled + step;
        const auto sample = SampleRoute(route, distance);
        if (!relocating) {
            position = {sample.position[0], sample.position[1], sample.position[2]};
            if (std::abs(step) > 0.01f) {
                const float facing = std::atan2(sample.direction[0], sample.direction[1]) +
                    (step < 0.0f ? std::numbers::pi_v<float> : 0.0f);
                heading = std::remainder(heading + std::clamp(std::remainder(facing - heading, tau), -6.0f * delta, 6.0f * delta), tau);
            }
        }
        Animate(delta > 0.0f ? std::abs(step) / delta : 0.0f, delta, settings.animate);
        RE::NiTransform transform;
        transform.scale = 1.3f;  // Vanilla EncChicken actor height.
        transform.translate = position + RE::NiPoint3{0.0f, 0.0f, 2.0f};
        const float c = std::cos(heading), s = std::sin(heading);
        transform.rotate.entry[0][0] = c;
        transform.rotate.entry[0][1] = s;
        transform.rotate.entry[1][0] = -s;
        transform.rotate.entry[1][1] = c;
        root->local = parent->world.Invert() * transform;
        for (auto& shape : geometry) {
            shape->GetGeometryRuntimeData().shaderProperty->SetMaterialAlpha(FadeOpacity(alpha));
        }
        root->CullNode(alpha <= 0.0f);
        RE::NiUpdateData update{};
        root->Update(update);
        return std::max(playerDistance, distance);
    }

    void ResetChickenGuide(bool releaseModel)
    {
        if (root && root->parent) {
            root->parent->DetachChild(root.get());
        }
        geometry.clear();
        bones.clear();
        root.reset();
        placed = false;
        relocating = false;
        alpha = 0.0f;
        gait = 0.0f;
        stride = 0.0f;
        if (releaseModel) {
            model.reset();
            unavailable = false;
        }
    }
}
