#pragma once
#ifndef MOVEMENTSYSTEM_H
#define MOVEMENTSYSTEM_H
#include"../ECS/ECS.h"
#include"../EventBus/EventBus.h"
#include"../Events/CollisionEvent.h"
#include"../Components/RigidBodyComponent.h"
#include"../Components/TransformComponent.h"
#include"../Components/SpriteComponent.h"
#include"../Physics/Physics2D.h"
#include"../Game/Game.h"
class MovementSystem :public System {
public:
	MovementSystem() {
		// 筛选需求的组件
		RequireComponent<TransformComponent>();
		RequireComponent<RigidBodyComponent>();
	}

	glm::vec2 gravity{0, 980};
	void Update(double deltaTime) {
		for (auto& entity : GetSystemEntities()) {
			if (entity.registry->IsPendingRemoval(entity)) continue;
			auto& transform = entity.GetComponent<TransformComponent>();
			auto& rigidbody = entity.GetComponent<RigidBodyComponent>();

			Physics2D::Integrate(transform, rigidbody, static_cast<float>(deltaTime), gravity);
			rigidbody.force = glm::vec2(0);

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
