#pragma once
#include <glm/glm.hpp>
enum class BodyType { Static, Kinematic, Dynamic };
// Translation-only: orientation is controlled by Transform/scripts.
struct RigidBodyComponent {
    glm::vec2 velocity{0};
    glm::vec2 force{0};
    BodyType type = BodyType::Dynamic;
    float mass = 1;
    float restitution = 0.5f;
    float friction = 0.4f;
    float gravityScale = 0; // Existing top-down levels have no gravity.
    float linearDamping = 0;
    explicit RigidBodyComponent(glm::vec2 velocity = glm::vec2(0)) : velocity(velocity) {}
    float InverseMass() const {
        return type == BodyType::Dynamic && mass > 0 ? 1.0f / mass : 0.0f;
    }
    void AddForce(glm::vec2 value) { force += value; }
    void ApplyImpulse(glm::vec2 value) { velocity += value * InverseMass(); }
};
