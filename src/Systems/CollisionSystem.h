#pragma once
#include "../Physics/EntityShape.h"
#include "../EventBus/EventBus.h"
#include "../Events/CollisionEvent.h"

class CollisionSystem : public System {
public:
    std::size_t candidateCount = 0;
    std::size_t contactCount = 0;
    CollisionSystem() {
        RequireComponent<TransformComponent>();
        RequireComponent<ColliderComponent>();
    }
    void Update(std::unique_ptr<EventBus>& eventBus) {
        auto entities = GetSystemEntities();
        std::vector<Physics2D::WorldShape> shapes;
        for (auto entity : entities) {
            shapes.push_back(entity.registry->IsPendingRemoval(entity) ? Physics2D::WorldShape{} : Physics2D::ShapeOf(entity));
        }
        const auto candidates = Physics2D::Candidates(shapes);
        candidateCount = candidates.size();
        struct Pair { Entity a, b; Physics2D::Contact contact; bool trigger; };
        std::vector<Pair> contacts;
        for (const auto& pair : candidates) {
            auto a = entities[pair.first], b = entities[pair.second];
            const auto& ca = a.GetComponent<ColliderComponent>();
            const auto& cb = b.GetComponent<ColliderComponent>();
            Physics2D::Contact contact;
            if (Physics2D::ShouldCollide(ca, cb) && Physics2D::Collide(shapes[pair.first], shapes[pair.second], contact)) {
                contacts.push_back({a, b, contact, ca.isTrigger || cb.isTrigger});
            }
        }
        contactCount = contacts.size();
        // 使用顺序冲量法解算平移响应；触发器不参与物理响应求解。
        for (int iteration = 0; iteration < 6; ++iteration) {
            for (auto& pair : contacts) {
                if (pair.trigger) continue;
                auto* ba = Physics2D::BodyOf(pair.a);
                auto* bb = Physics2D::BodyOf(pair.b);
                Physics2D::ResolveVelocity(ba, bb, pair.contact);
                Physics2D::Contact current;
                if (Physics2D::Collide(Physics2D::ShapeOf(pair.a), Physics2D::ShapeOf(pair.b), current)) {
                    Physics2D::CorrectPositions(pair.a.GetComponent<TransformComponent>(),
                        pair.b.GetComponent<TransformComponent>(), ba, bb, current);
                }
            }
        }
        // 求解后，每对接触在每个物理步只发送一次事件。Kill() 会延迟销毁实体，
        // 因此跳过待销毁实体，避免同一发子弹继续伤害多个目标。
        for (auto& pair : contacts) {
            if (pair.a.registry->IsPendingRemoval(pair.a) || pair.b.registry->IsPendingRemoval(pair.b)) continue;
            eventBus->EmitEvent<CollisionEvent>(pair.a, pair.b, pair.contact.normal, pair.contact.penetration, pair.trigger);
        }
    }
};
// 保留旧名称的别名，兼容使用原拼写的调用代码。
using CollisionSytem = CollisionSystem;
