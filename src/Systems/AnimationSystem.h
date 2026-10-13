#pragma once
#ifndef ANIMATIONSYSTEM_H
#define ANIMATIONSYSTEM

#include<SDL.h>
#include"../Time/SimulationClock.h"
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


			const int frame = static_cast<int>((SimulationClock::Milliseconds() - animation.startTime) * animation.frameSpeedRate / 1000.0);
            animation.currentFrame = animation.isLoop ? frame % animation.numFrames : (std::min)(frame, animation.numFrames - 1);
			sprite.srcRect.x = animation.currentFrame * sprite.width;
		}
	}
};
#endif // !ANIMATIONSYSTEM_H
