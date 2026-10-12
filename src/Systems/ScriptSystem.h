#pragma once
#ifndef SCRIPTSYSTEM
#define SCRIPTSYSTEM
#include"../ECS/ECS.h"
#include"../Components/ScriptComponent.h"
#include"../Components/TransformComponent.h"
#include<tuple>

std::tuple<double, double> GetEntityPosition(Entity entity) {
	if (entity.HasComponent<TransformComponent>()) {
		const auto transform = entity.GetComponent<TransformComponent>();
		return std::make_tuple(transform.position.x, transform.position.y);
	}
	else {
		Logger::Err("获取位置实体无TransformComponent");
		return std::make_tuple(0.0, 0.0);
	}
}

std::tuple<double, double> GetEntityVelocity(Entity entity) {
	if (entity.HasComponent<RigidBodyComponent>()) {
		const auto rigidbody = entity.GetComponent<RigidBodyComponent>();
		return std::make_tuple(rigidbody.velocity.x, rigidbody.velocity.y);
	}
	else {
		Logger::Err("获取速度实体无RigidbodyComponent");
		return std::make_tuple(0.0, 0.0);
	}
}

void SetEntityVelocity(Entity entity, double x, double y) {
	if (entity.HasComponent<RigidBodyComponent>()) {
		auto& rigidbody = entity.GetComponent<RigidBodyComponent>();
		rigidbody.velocity.x = x;
		rigidbody.velocity.y = y;
	}
	else {
		Logger::Err("设置速度实体无RigidbodyComponent");
	}
}

void SetEntityRotation(Entity entity, double angle) {
	if (entity.HasComponent<TransformComponent>()) {
		auto& transform = entity.GetComponent<TransformComponent>();
		transform.rotation = angle;
	}
	else {
		Logger::Err("设置旋转实体无TransformComponent");
	}
}

void SetEntityAnimationFrame(Entity entity, int frame) {
	if (entity.HasComponent<AnimationComponent>()) {
		auto& animation = entity.GetComponent<AnimationComponent>();
		animation.currentFrame = frame;
	}
	else {
		Logger::Err("设置动画帧实体无AnimationComponent");
	}
}

void SetProjectileVelocity(Entity entity, double x, double y) {
	if (entity.HasComponent<ProjectileEmitterComponent>()) {
		auto& projectileEmitter = entity.GetComponent<ProjectileEmitterComponent>();
		projectileEmitter.projectileVelocity.x = x;
		projectileEmitter.projectileVelocity.y = y;
	}
	else {
		Logger::Err("设置发射速度实体无ProjectileEmitterComponent");
	}
}

void SetEntityPosition(Entity entity, double x, double y) {
	if (entity.HasComponent<TransformComponent>()) {
		auto& transform = entity.GetComponent<TransformComponent>();
		transform.position.x = x;
		transform.position.y = y;
	}
	else {
		Logger::Err("设置位置实体无TransformComponent");
	}
}

class ScriptSystem :public System {
public:
	ScriptSystem() {
		RequireComponent<ScriptComponent>();
	}

	void CreateLuaBindings(sol::state& lua) {
		lua.new_usertype<Entity>(
			"entity",
			"get_id", &Entity::GetId,
			"destroy", &Entity::Kill,
			"has_tag", &Entity::HasTag,
			"belongs_to_group", &Entity::BelongsToGroup
		);

		lua.set_function("set_position", SetEntityPosition);
		lua.set_function("get_position", GetEntityPosition);
		lua.set_function("get_velocity", GetEntityVelocity);
		lua.set_function("set_velocity", SetEntityVelocity);
        lua.set_function("add_force", [](Entity entity, float x, float y) {
            if (entity.HasComponent<RigidBodyComponent>()) entity.GetComponent<RigidBodyComponent>().AddForce({x, y});
        });
        lua.set_function("apply_impulse", [](Entity entity, float x, float y) {
            if (entity.HasComponent<RigidBodyComponent>()) entity.GetComponent<RigidBodyComponent>().ApplyImpulse({x, y});
        });
		lua.set_function("set_rotation", SetEntityRotation);
		lua.set_function("set_projectile_velocity", SetProjectileVelocity);
		lua.set_function("set_animation_frame", SetEntityAnimationFrame);
	}

	void Update(double deltaTime, int ellapsedTime) {
		for (auto entity : GetSystemEntities()) {
			if (entity.registry->IsPendingRemoval(entity)) continue;
			const auto script = entity.GetComponent<ScriptComponent>();
			script.func(entity, deltaTime, ellapsedTime);
		}
	}
};
#endif // !SCRIPTSYSTEM
