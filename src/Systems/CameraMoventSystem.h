#pragma once
#ifndef CAMERAMOVENTSYSTEM_H
#define CAMERAMOVENTSYSTEM_H

#include"../ECS/ECS.h"
#include"SDL.h"
#include"../Components/CameraFollowComponent.h"
#include"../Components/TransformComponent.h"
class  CameraMoventSystem :public System {
public:
	CameraMoventSystem() {
		RequireComponent<CameraFollowComponent>();
		RequireComponent<TransformComponent>();
	}

	void Update(SDL_Rect& camera) {
		for (auto entity : GetSystemEntities()) {
			auto transform = entity.GetComponent<TransformComponent>();

			if (transform.position.x + (camera.w / 2) < Game::mapWidth) {
				camera.x = transform.position.x - (Game::windowWidth / 2);
			}

			if (transform.position.y + (camera.h / 2) < Game::mapHeight) {
				camera.y = transform.position.y - (Game::windowHeight / 2);
			}

			camera.x = camera.x < 0 ? 0 : camera.x;
			camera.y = camera.y < 0 ? 0 : camera.y;

			camera.x = (std::max)(0, (std::min)(camera.x, Game::mapWidth - camera.w));
			camera.y = (std::max)(0, (std::min)(camera.y, Game::mapHeight - camera.h));


			// Logger::Log("摄像机位置：" + std::to_string(camera.x) + " " + std::to_string(camera.y));
		}
	}
};

#endif // !CAMERAMOVENTSYSTEM_H
