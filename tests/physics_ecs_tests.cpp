#define SDL_MAIN_HANDLED
#include "../src/Systems/CollisionSystem.h"
#include "../src/Systems/DamageSystem.h"
#include "../src/Components/BoxColliderComponent.h"
#include <iostream>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Listener {
    int count = 0;
    bool trigger = false;
    void OnCollision(CollisionEvent& event) { ++count; trigger = event.isTrigger; }
};
int main() {
    try {
        Registry registry;
        registry.AddSystem<CollisionSystem>();
        auto bus = std::make_unique<EventBus>();
        Listener listener;
        bus->SubscribeToEvent<CollisionEvent>(&listener, &Listener::OnCollision);
        auto a = registry.CreateEntity(), b = registry.CreateEntity();
        a.AddComponent<TransformComponent>(glm::vec2(0));
        a.AddComponent<BoxColliderComponent>(10, 10);
        a.AddComponent<RigidBodyComponent>(glm::vec2(10, 0));
        b.AddComponent<TransformComponent>(glm::vec2(8, 0));
        b.AddComponent<ColliderComponent>(10, 10);
        registry.Update();
        registry.GetSystem<CollisionSystem>().Update(bus);
        Check(listener.count == 1 && !listener.trigger, "one event per pair despite solver iterations");
        Check(a.GetComponent<RigidBodyComponent>().velocity.x < 0, "actual system reflects body");
        Check(a.GetComponent<TransformComponent>().position.x < -1.9f, "actual system resolves penetration");
        Check(b.GetComponent<TransformComponent>().position.x == 8, "collider without body stays static");
        a.GetComponent<TransformComponent>().position = {0, 0};
        a.GetComponent<RigidBodyComponent>().velocity = {10, 0};
        a.GetComponent<ColliderComponent>().isTrigger = true;
        registry.GetSystem<CollisionSystem>().Update(bus);
        Check(listener.count == 2 && listener.trigger, "trigger event");
        Check(a.GetComponent<TransformComponent>().position.x == 0 && a.GetComponent<RigidBodyComponent>().velocity.x == 10, "trigger does not solve");
        b.GetComponent<ColliderComponent>().mask = 0;
        registry.GetSystem<CollisionSystem>().Update(bus);
        Check(listener.count == 2, "filter prevents event");
        a.Kill(); registry.Update(); b.Kill(); registry.Update();

        // 验证真实伤害系统：一发子弹同时重叠两个目标时，只造成一次伤害。
        DamageSystem damage;
        damage.SubscribeToEvents(bus);
        auto bullet = registry.CreateEntity();
        bullet.Group("projectiles");
        bullet.AddComponent<TransformComponent>();
        bullet.AddComponent<ColliderComponent>(ColliderComponent::Circle(3));
        bullet.GetComponent<ColliderComponent>().isTrigger = true;
        bullet.AddComponent<ProjectileComponent>(true, 10, 1000);
        auto target1 = registry.CreateEntity(), target2 = registry.CreateEntity();
        for (auto target : {target1, target2}) {
            target.Group("enemies");
            target.AddComponent<TransformComponent>();
            target.AddComponent<ColliderComponent>(10, 10);
            target.AddComponent<HealthComponent>(100);
        }
        registry.Update();
        registry.GetSystem<CollisionSystem>().Update(bus);
        const int health = target1.GetComponent<HealthComponent>().healthPercentage + target2.GetComponent<HealthComponent>().healthPercentage;
        Check(health == 190 && registry.IsPendingRemoval(bullet), "bullet cannot double damage before deferred destruction");
        registry.Update();
        registry.GetSystem<CollisionSystem>().Update(bus);
        Check(target1.GetComponent<HealthComponent>().healthPercentage + target2.GetComponent<HealthComponent>().healthPercentage == 190, "no damage next substep");
        // 绕精灵中心旋转的结果，必须与独立几何计算的约定一致。
        target1.AddComponent<SpriteComponent>("test", 20, 20);
        target1.GetComponent<TransformComponent>() = TransformComponent({100, 200}, {2, 3}, 90);
        target1.GetComponent<ColliderComponent>() = ColliderComponent(10, 4, {2, 1});
        Check(glm::length(Physics2D::ShapeOf(target1).center - glm::vec2(141, 224)) < 0.001f, "sprite pivot integration");
        std::cout << "PASS: ECS response, trigger, filtering, sprite pivot, and damage regression\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
