#pragma once
#ifndef PROHECTILEEMITSYSTEM_H
#define PROHECTILEEMITSYSTEM_H

#include"../ECS/ECS.h"
#include"../EventBus/EventBus.h"
#include"../Events/KeyPressedEvent.h"
#include"../Components/TransformComponent.h"
#include"../Components/RigidBodyComponent.h"
#include"../Components/SpriteComponent.h"
#include"../Components/BoxColliderComponent.h"
#include"../Components/ProjectileEmitterComponent.h"
#include"../Components/ProjectileComponent.h"
#include"../Components/CameraFollowComponent.h"
#include<SDL.h>
#include"../Time/SimulationClock.h"

class ProjectileEmitSystem :public System {
public:
	ProjectileEmitSystem() {
		RequireComponent<TransformComponent>();
		RequireComponent<ProjectileEmitterComponent>();
	}

	void SubscribeToEvents(std::unique_ptr<EventBus>& eventBus) {
		eventBus->SubscribeToEvent<KeyPressedEvent>(this, &ProjectileEmitSystem::OnKeyPressed);
	}

	void OnKeyPressed(KeyPressedEvent& event) {
		if (event.symbol == SDLK_SPACE) {
			for (auto entity : GetSystemEntities()) {
            if (entity.IsPendingRemoval()) continue;
				if (entity.HasComponent<CameraFollowComponent>() && entity.HasComponent<RigidBodyComponent>()) {
					const auto projectileEmitter = entity.GetComponent<ProjectileEmitterComponent>();
					const auto transform = entity.GetComponent<TransformComponent>();
					const auto rigidbody = entity.GetComponent<RigidBodyComponent>();

					glm::vec2 projectilePosition = transform.position;
					if (entity.HasComponent<SpriteComponent>()) {
						auto sprite = entity.GetComponent<SpriteComponent>();
						projectilePosition.x += (transform.scale.x * sprite.width / 2);
						projectilePosition.y += (transform.scale.y * sprite.height / 2);
					}

					glm::vec2 projectileVelocity = projectileEmitter.projectileVelocity;
					int directionX = 0;
					int directionY = 0;
					if (rigidbody.velocity.x > 0)directionX = 1;
					if (rigidbody.velocity.x < 0)directionX = -1;
					if (rigidbody.velocity.y > 0)directionY = 1;
					if (rigidbody.velocity.y < 0)directionY = -1;
					projectileVelocity.x = projectileEmitter.projectileVelocity.x * directionX;
					projectileVelocity.y = projectileEmitter.projectileVelocity.y * directionY;

					Entity projectile = entity.registry->CreateEntity();
					projectile.Group("projectiles");
					projectile.AddComponent<TransformComponent>(projectilePosition, glm::vec2(1.0, 1.0), 0.0);
					projectile.AddComponent<RigidBodyComponent>(projectileVelocity);
					projectile.AddComponent<SpriteComponent>("bullet-texture", 4, 4, 4);
					// 提交前不能查询新组件，先在局部配置好触发器再加入命令队列。
                BoxColliderComponent collider(4, 4);
                collider.isTrigger = true;
                projectile.AddComponent<BoxColliderComponent>(collider);
					projectile.AddComponent<ProjectileComponent>(projectileEmitter.isFriendly, projectileEmitter.hitPercentDamage, projectileEmitter.projectileDuration);

				}
			}
		}
	}

	void Update(std::unique_ptr<Registry>& registry) {
		for (auto entity : GetSystemEntities()) {
            if (entity.IsPendingRemoval()) continue;
			auto& projectileEmitter = entity.GetComponent<ProjectileEmitterComponent>();
			const auto transform = entity.GetComponent<TransformComponent>();

			if (projectileEmitter.repeatFrequency == 0) {
				continue;
			}

			if (SimulationClock::Milliseconds() - projectileEmitter.lastEmissionTime > projectileEmitter.repeatFrequency) {
				glm::vec2 projectilePosition = transform.position;
				if (entity.HasComponent<SpriteComponent>()) {
					const auto sprite = entity.GetComponent<SpriteComponent>();
					projectilePosition.x += (transform.scale.x * sprite.width / 2);
					projectilePosition.y += (transform.scale.y * sprite.height / 2);
				}

				Entity projectile = registry->CreateEntity();
				projectile.Group("projectiles");
				projectile.AddComponent<TransformComponent>(projectilePosition, glm::vec2(1.0, 1.0), 0.0);
				projectile.AddComponent<RigidBodyComponent>(projectileEmitter.projectileVelocity);
				projectile.AddComponent<SpriteComponent>("bullet-texture", 4, 4, 4);
				// 提交前不能查询新组件，先在局部配置好触发器再加入命令队列。
                BoxColliderComponent collider(4, 4);
                collider.isTrigger = true;
                projectile.AddComponent<BoxColliderComponent>(collider);
				projectile.AddComponent<ProjectileComponent>(projectileEmitter.isFriendly, projectileEmitter.hitPercentDamage, projectileEmitter.projectileDuration);

				projectileEmitter.lastEmissionTime = SimulationClock::Milliseconds();

			}
		}
	}
};

#endif // !PROHECTILEEMITSYSTEM_H
