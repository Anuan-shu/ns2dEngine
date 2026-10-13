#pragma once
#include"../Time/SimulationClock.h"
#ifndef PROJECTILELIFECYCLESYSTEM_H
#define PROJECTILELIFECYCLESYSTEM_H
#include"../ECS/ECS.h"
#include"../Components/ProjectileComponent.h"
class ProjectileLifecycleSystem :public System {
public:
	ProjectileLifecycleSystem() {
		RequireComponent<ProjectileComponent>();
	}

	void Update() {
		for (auto entity : GetSystemEntities()) {
			auto projectile = entity.GetComponent<ProjectileComponent>();

			if (SimulationClock::Milliseconds() - projectile.startTime > projectile.duration) {
				entity.Kill();
			}
		}
	}
};
#endif // !PROJECTILELIFECYCLESYSTEM_H
