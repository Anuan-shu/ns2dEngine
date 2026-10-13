#pragma once
#include "../Components/SceneObjectComponent.h"
#include "../Components/TransformComponent.h"
#include "../Components/RigidBodyComponent.h"
#include "../Components/SpriteComponent.h"
#include "../Components/ColliderComponent.h"
#include "../Components/AnimationComponent.h"
#include "../Components/HealthComponent.h"
#include "../Components/ProjectileEmitterComponent.h"
#include "../Components/KeyboardControlledComponent.h"
#include "../Components/TextLabelComponent.h"
#include "../Components/PrimitiveRenderComponent.h"
#include <optional>
#include <vector>

// 场景文件只包含可序列化的值；脚本函数通过原关卡索引恢复，不保存实体 ID。
struct SceneEntityData {
    SceneObjectComponent object;
    std::string tag, group;
    std::optional<TransformComponent> transform;
    std::optional<RigidBodyComponent> body;
    std::optional<SpriteComponent> sprite;
    std::optional<ColliderComponent> collider;
    std::optional<AnimationComponent> animation;
    std::optional<HealthComponent> health;
    std::optional<ProjectileEmitterComponent> emitter;
    std::optional<KeyboardControlledComponent> keyboard;
    std::optional<TextLabelComponent> text;
    std::optional<PrimitiveRenderComponent> primitive;
    bool cameraFollow = false;
    bool hasScript = false;
    bool scriptEnabled = true;
};
struct SceneData {
    int version = 1;
    int baseLevel = 2;
    double simulationSeconds = 0;
    std::vector<SceneEntityData> entities;
};
