#pragma once
#ifndef MOVEMENTSYSTEM_H
#define MOVEMENTSYSTEM_H
#include"../ECS/ECS.h"
#include"../Components/RigidBodyComponent.h"
#include"../Components/TransformComponent.h"
class MovementSystem :public System {
public:
	MovementSystem() {
		// 筛选需求的组件
		RequireComponent<TransformComponent>();
		RequireComponent<RigidBodyComponent>();
	}

	void Update(double deltaTime) {
		for (auto& entity : GetSystemEntities()) {
			auto& transform = entity.GetComponent<TransformComponent>();
			const auto& rigidbody = entity.GetComponent<RigidBodyComponent>();

			transform.position.x += rigidbody.velocity.x * deltaTime;
			transform.position.y += rigidbody.velocity.y * deltaTime;

			/*Logger::Log("Entity id = " + std::to_string(entity.GetId()) +
				" position (" +
				std::to_string(transform.position.x) + ", " +
				std::to_string(transform.position.y) + ")");*/
		}
	}
};
#endif // !MOVEMENTSYSTEM_H
