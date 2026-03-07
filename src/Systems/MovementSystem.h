#pragma once
#ifndef MOVEMENTSYSTEM_H
#define MOVEMENTSYSTEM_H
#include"../ECS/ECS.h"
#include"../EventBus/EventBus.h"
#include"../Events/CollisionEvent.h"
#include"../Components/RigidBodyComponent.h"
#include"../Components/TransformComponent.h"
#include"../Components/SpriteComponent.h"
class MovementSystem :public System {
public:
	MovementSystem() {
		// 筛选需求的组件
		RequireComponent<TransformComponent>();
		RequireComponent<RigidBodyComponent>();
	}

	void SubscribeToEvents(const std::unique_ptr<EventBus>& eventBus) {
		eventBus->SubscribeToEvent<CollisionEvent>(this, &MovementSystem::OnCollision);
	}

	void OnCollision(CollisionEvent& event) {
		Entity a = event.a;
		Entity b = event.b;
		Logger::Log("MovementSystem 收到两个实体的碰撞事件: entityId = " + std::to_string(event.a.GetId()) + "和entityId = " + std::to_string(event.b.GetId()));

		if (a.BelongsToGroup("enemies") && b.BelongsToGroup("obstacles")) {
			OnEnemyHitsObstacle(a, b);
		}
		if (b.BelongsToGroup("enemies") && a.BelongsToGroup("obstacles")) {
			OnEnemyHitsObstacle(b, a);
		}
	}

	void OnEnemyHitsObstacle(Entity enemy, Entity obstacle) {
		if (enemy.HasComponent<RigidBodyComponent>() && enemy.HasComponent<SpriteComponent>()) {
			auto& rigidbody = enemy.GetComponent<RigidBodyComponent>();
			auto& sprite = enemy.GetComponent<SpriteComponent>();

			if (rigidbody.velocity.x != 0) {
				rigidbody.velocity.x *= -1;
				sprite.flip = (sprite.flip == SDL_FLIP_NONE) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
			}
			if (rigidbody.velocity.y != 0) {
				rigidbody.velocity.y *= -1;
				sprite.flip = (sprite.flip == SDL_FLIP_NONE) ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE;
			}
		}
	}

	void Update(double deltaTime) {
		for (auto& entity : GetSystemEntities()) {
			auto& transform = entity.GetComponent<TransformComponent>();
			const auto& rigidbody = entity.GetComponent<RigidBodyComponent>();

			transform.position.x += rigidbody.velocity.x * deltaTime;
			transform.position.y += rigidbody.velocity.y * deltaTime;

			if (entity.HasTag("player")) {
				int paddingLeft = 10;
				int paddingRight = 50;
				int paddingTop = 10;
				int paddingBottom = 50;
				transform.position.x = transform.position.x < paddingLeft ? paddingLeft : transform.position.x;
				transform.position.x = transform.position.x > Game::mapWidth - paddingRight ? Game::mapWidth - paddingRight : transform.position.x;
				transform.position.y = transform.position.y < paddingTop ? paddingTop : transform.position.y;
				transform.position.y = transform.position.y > Game::mapHeight - paddingBottom ? Game::mapHeight - paddingBottom : transform.position.y;

			}

			/*Logger::Log("Entity id = " + std::to_string(entity.GetId()) +
				" position (" +
				std::to_string(transform.position.x) + ", " +
				std::to_string(transform.position.y) + ")");*/
			bool isEntityOutsideMap = (
				transform.position.x<0 ||
				transform.position.x>Game::mapWidth ||
				transform.position.y<0 ||
				transform.position.y>Game::mapHeight
				);
			if (isEntityOutsideMap && !entity.HasTag("player")) {
				entity.Kill();
			}
		}
	}
};
#endif // !MOVEMENTSYSTEM_H
