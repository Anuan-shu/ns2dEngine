#pragma once
#include "../Physics/EntityShape.h"
#include "../Components/PrimitiveRenderComponent.h"

// 对没有精灵的场景矩形和圆形绘制填充形状，退出编辑器后也能看到这些对象。
class RenderPrimitiveSystem : public System {
public:
    RenderPrimitiveSystem() {
        RequireComponent<TransformComponent>(); RequireComponent<ColliderComponent>(); RequireComponent<PrimitiveRenderComponent>();
    }
    void Update(SDL_Renderer* renderer, const SDL_Rect& camera) {
        for (auto entity : GetSystemEntities()) {
            if (entity.HasComponent<SpriteComponent>()) continue;
            auto shape = Physics2D::ShapeOf(entity);
            if (!shape.valid || shape.bounds.max.x < camera.x || shape.bounds.min.x > camera.x + camera.w
                || shape.bounds.max.y < camera.y || shape.bounds.min.y > camera.y + camera.h) continue;
            const auto color = entity.GetComponent<PrimitiveRenderComponent>().color;
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            const int minY = (std::max)(static_cast<int>(std::floor(shape.bounds.min.y)), camera.y);
            const int maxY = (std::min)(static_cast<int>(std::ceil(shape.bounds.max.y)), camera.y + camera.h);
            const auto corners = Physics2D::Corners(shape);
            for (int y = minY; y <= maxY; ++y) {
                float left = 0, right = 0;
                if (shape.type == ColliderShape::Circle) {
                    const float dy = y + 0.5f - shape.center.y;
                    const float width2 = shape.radius * shape.radius - dy * dy;
                    if (width2 < 0) continue;
                    const float half = std::sqrt(width2);
                    left = shape.center.x - half; right = shape.center.x + half;
                } else {
                    std::vector<float> intersections;
                    const float scan = y + 0.5f;
                    for (int i = 0; i < 4; ++i) {
                        const auto a = corners[i], b = corners[(i + 1) % 4];
                        if ((a.y <= scan && b.y > scan) || (b.y <= scan && a.y > scan))
                            intersections.push_back(a.x + (scan - a.y) * (b.x - a.x) / (b.y - a.y));
                    }
                    if (intersections.size() < 2) continue;
                    left = *std::min_element(intersections.begin(), intersections.end());
                    right = *std::max_element(intersections.begin(), intersections.end());
                }
                SDL_RenderDrawLine(renderer, static_cast<int>(std::ceil(left)) - camera.x, y - camera.y,
                    static_cast<int>(std::floor(right)) - camera.x, y - camera.y);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
};
