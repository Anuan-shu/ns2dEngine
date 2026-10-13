#pragma once
#ifndef ANIMATIONCOMPONENT_H
#define ANIMATIONCOMPONENT_H

#include<SDL.h>
#include"../Time/SimulationClock.h"
struct AnimationComponent {
	int numFrames;
	int currentFrame;
	int frameSpeedRate;
	bool isLoop;
	int startTime;

	AnimationComponent(int numFrames = 1, int frameSpeedRate = 1, bool isLoop = true) {
		this->numFrames = numFrames;
		this->currentFrame = 0;
		this->frameSpeedRate = frameSpeedRate;
		this->isLoop = isLoop;
		this->startTime = SimulationClock::Milliseconds();
	}
};

#endif // !ANIMATIONCOMPONENT_H
