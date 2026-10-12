#pragma once
#ifndef ANIMATIONSYSTEM_H
#define ANIMATIONSYSTEM

#include<SDL.h>
#include"../ECS/ECS.h"
#include"../Components/AnimationComponent.h"
#include"../Components/SpriteComponent.h"

class AnimationSystem :public System {
public:
	AnimationSystem() {
		RequireComponent<SpriteComponent>();
		RequireComponent<AnimationComponent>();
	}

	void Update() {
		for (auto& entity : GetSystemEntities()) {
			auto& animation = entity.GetComponent<AnimationComponent>();
			auto& sprite = entity.GetComponent<SpriteComponent>();


			animation.currentFrame = static_cast<int>((SDL_GetTicks() - animation.startTime) * animation.frameSpeedRate / 1000.0) % animation.numFrames;
			sprite.srcRect.x = animation.currentFrame * sprite.width;
		}
	}
};
#endif // !ANIMATIONSYSTEM_H
