#pragma once
#ifndef DAMAGESYSTEM_H
#define DAMAGESYSTEM_H

#include<memory>
#include"../ECS/ECS.h"
#include"../Components/BoxColliderComponent.h"
#include"../Components/ProjectileComponent.h"
#include"../Components/HealthComponent.h"
#include"../EventBus/EventBus.h"
#include"../Events/CollisionEvent.h"

class DamageSystem :public System {
public:
	DamageSystem() {
		RequireComponent<BoxColliderComponent>();
	}

	void SubscribeToEvents(std::unique_ptr<EventBus>& eventBus) {
		eventBus->SubscribeToEvent<CollisionEvent>(this, &DamageSystem::onCollision);
	}
	void onCollision(CollisionEvent& event) {
		Entity a = event.a;
		Entity b = event.b;

		if (a.BelongsToGroup("projectiles") && b.HasTag("player")) {
			onProjectileHitsPlayer(a, b);
		}

		if (b.BelongsToGroup("projectiles") && a.HasTag("player")) {
			onProjectileHitsPlayer(b, a);
		}

		if (a.BelongsToGroup("projectiles") && b.BelongsToGroup("enemies")) {
			onProjectileHitsEnemy(a, b);
		}

		if (b.BelongsToGroup("projectiles") && a.BelongsToGroup("enemies")) {
			onProjectileHitsEnemy(b, a);
		}

	}
	void onProjectileHitsEnemy(Entity projectile, Entity enemy) {
		if (!projectile.HasComponent<ProjectileComponent>()) return;
		auto projectileComponent = projectile.GetComponent<ProjectileComponent>();

		if (projectileComponent.isFriendly && enemy.HasComponent<HealthComponent>()) {
			auto& healeh = enemy.GetComponent<HealthComponent>();
			healeh.healthPercentage -= projectileComponent.hitPercentDamage;

			if (healeh.healthPercentage <= 0) {
				enemy.Kill();
			}

			projectile.Kill();
		}
	}
	void onProjectileHitsPlayer(Entity projectile, Entity player) {
		if (!projectile.HasComponent<ProjectileComponent>()) return;
		auto projectileComponent = projectile.GetComponent<ProjectileComponent>();

		if (!projectileComponent.isFriendly && player.HasComponent<HealthComponent>()) {
			auto& healeh = player.GetComponent<HealthComponent>();
			healeh.healthPercentage -= projectileComponent.hitPercentDamage;

			if (healeh.healthPercentage <= 0) {
				player.Kill();
			}

			projectile.Kill();
		}
	}

	void Update() {

	}
};

#endif // !DAMAGESYSTEM_H
