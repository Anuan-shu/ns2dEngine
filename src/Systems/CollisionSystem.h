#pragma once
#ifndef COLLISIONSYSTEM_H
#define COLLISIONSYSTEM_H

#include"../ECS/ECS.h"
#include"../Components/BoxColliderComponent.h"
#include"../Components/TransformComponent.h"

class CollisionSytem :public System {
public:
	CollisionSytem() {
		RequireComponent<TransformComponent>();
		RequireComponent<BoxColliderComponent>();
	}

	void Update() {

	}
};

#endif // !BOXCOLLIDERSYSTEM_H
