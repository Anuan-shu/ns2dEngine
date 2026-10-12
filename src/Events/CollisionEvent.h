#pragma once
#include "../ECS/ECS.h"
#include "../EventBus/Event.h"
#include <glm/glm.hpp>
class CollisionEvent : public Event {
public:
    Entity a, b;
    glm::vec2 normal;
    float penetration;
    bool isTrigger;
    CollisionEvent(Entity a, Entity b, glm::vec2 normal = glm::vec2(0), float penetration = 0, bool isTrigger = false)
        : a(a), b(b), normal(normal), penetration(penetration), isTrigger(isTrigger) {}
};
