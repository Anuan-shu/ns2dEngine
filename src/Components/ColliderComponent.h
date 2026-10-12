#pragma once
#include <glm/glm.hpp>
#include <cstdint>

enum class ColliderShape { Box, Circle };

// 每个实体只有一个碰撞体；尺寸和偏移使用未经缩放的局部像素坐标。
struct ColliderComponent {
    ColliderShape shape = ColliderShape::Box;
    float width = 0, height = 0, radius = 0;
    glm::vec2 offset{0}; // 局部包围矩形的左上角偏移；圆形也遵循此约定。
    bool isTrigger = false;
    std::uint32_t category = 1;
    std::uint32_t mask = 0xffffffffu;
    ColliderComponent(float width = 0, float height = 0, glm::vec2 offset = glm::vec2(0))
        : width(width), height(height), offset(offset) {}
    static ColliderComponent Circle(float radius, glm::vec2 offset = glm::vec2(0)) {
        ColliderComponent result(2 * radius, 2 * radius, offset);
        result.shape = ColliderShape::Circle;
        result.radius = radius;
        return result;
    }
};
