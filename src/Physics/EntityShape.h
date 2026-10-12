#pragma once
#include "Physics2D.h"
#include "../ECS/ECS.h"
#include "../Components/SpriteComponent.h"

namespace Physics2D {
inline WorldShape ShapeOf(Entity entity) {
    const auto& collider = entity.GetComponent<ColliderComponent>();
    auto pivot = LocalCenter(collider);
    if (entity.HasComponent<SpriteComponent>()) {
        const auto& sprite = entity.GetComponent<SpriteComponent>();
        pivot = glm::vec2(sprite.width, sprite.height) * 0.5f;
    }
    return MakeShape(entity.GetComponent<TransformComponent>(), collider, pivot);
}
inline RigidBodyComponent* BodyOf(Entity entity) {
    return entity.HasComponent<RigidBodyComponent>() ? &entity.GetComponent<RigidBodyComponent>() : nullptr;
}
}
