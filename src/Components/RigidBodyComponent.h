#pragma once
#include <glm/glm.hpp>
enum class BodyType { Static, Kinematic, Dynamic };
// 只解算平移运动；旋转角度由变换组件或脚本控制。
struct RigidBodyComponent {
    glm::vec2 velocity{0};
    glm::vec2 force{0};
    BodyType type = BodyType::Dynamic;
    float mass = 1;
    float restitution = 0.5f;
    float friction = 0.4f;
    float gravityScale = 0; // 原有俯视关卡默认不受重力影响。
    float linearDamping = 0;
    explicit RigidBodyComponent(glm::vec2 velocity = glm::vec2(0)) : velocity(velocity) {}
    float InverseMass() const {
        return type == BodyType::Dynamic && mass > 0 ? 1.0f / mass : 0.0f;
    }
    void AddForce(glm::vec2 value) { force += value; }
    void ApplyImpulse(glm::vec2 value) { velocity += value * InverseMass(); }
};
