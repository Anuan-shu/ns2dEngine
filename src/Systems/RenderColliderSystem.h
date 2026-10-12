#pragma once
#include "../Physics/EntityShape.h"
#include <SDL.h>

class RenderColliderSystem : public System {
public:
    RenderColliderSystem() {
        RequireComponent<TransformComponent>();
        RequireComponent<ColliderComponent>();
    }
    void Update(SDL_Renderer* renderer, SDL_Rect& camera) {
        for (auto entity : GetSystemEntities()) {
            const auto shape = Physics2D::ShapeOf(entity);
            if (!shape.valid) continue;
            const bool trigger = entity.GetComponent<ColliderComponent>().isTrigger;
            SDL_SetRenderDrawColor(renderer, trigger ? 255 : 0, trigger ? 190 : 255, 0, 255);
            const bool fixed = entity.HasComponent<SpriteComponent>() && entity.GetComponent<SpriteComponent>().isFixed;
            const glm::vec2 cameraOffset = fixed ? glm::vec2(0) : glm::vec2(camera.x, camera.y);
            auto line = [&](glm::vec2 a, glm::vec2 b) {
                a -= cameraOffset; b -= cameraOffset;
                SDL_RenderDrawLine(renderer, static_cast<int>(std::lround(a.x)), static_cast<int>(std::lround(a.y)),
                    static_cast<int>(std::lround(b.x)), static_cast<int>(std::lround(b.y)));
            };
            if (shape.type == ColliderShape::Box) {
                const auto corners = Physics2D::Corners(shape);
                for (int i = 0; i < 4; ++i) line(corners[i], corners[(i + 1) % 4]);
            } else {
                const int segments = 48;
                auto previous = shape.center + glm::vec2(shape.radius, 0);
                for (int i = 1; i <= segments; ++i) {
                    const float angle = i * 6.28318530718f / segments;
                    auto next = shape.center + glm::vec2(std::cos(angle), std::sin(angle)) * shape.radius;
                    line(previous, next); previous = next;
                }
            }
        }
    }
};
