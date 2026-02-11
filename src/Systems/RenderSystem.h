#pragma once
#ifndef RENDER_H
#define RENDER_H
#include"../ECS/ECS.h"
#include"../Components/SpriteComponent.h"
#include"../Components/TransformComponent.h"
#include<SDL.h>
class RenderSystem :public System {
public:
	RenderSystem() {
		// 筛选需求的组件
		RequireComponent<TransformComponent>();
		RequireComponent<SpriteComponent>();
	}

	void Update(SDL_Renderer* renderer) {
		for (auto& entity : GetSystemEntities()) {
			const auto& transform = entity.GetComponent<TransformComponent>();
			const auto& sprite = entity.GetComponent<SpriteComponent>();

			SDL_Rect objRect = {
				static_cast<int>(transform.position.x),
				static_cast<int>(transform.position.y),
				sprite.width,
				sprite.height
			};
			SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
			SDL_RenderFillRect(renderer, &objRect);
		}
	}
};
#endif // RENDER_H
