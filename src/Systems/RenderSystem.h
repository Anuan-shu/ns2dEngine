#pragma once
#ifndef RENDER_H
#define RENDER_H
#include"../ECS/ECS.h"
#include"../Components/SpriteComponent.h"
#include"../Components/TransformComponent.h"
#include"../AssetStore/AssetStore.h"
#include<SDL.h>
class RenderSystem :public System {
public:
	RenderSystem() {
		// 筛选需求的组件
		RequireComponent<TransformComponent>();
		RequireComponent<SpriteComponent>();
	}

	void Update(SDL_Renderer* renderer, std::unique_ptr<AssetStore>& assetStore) {
		for (auto& entity : GetSystemEntities()) {
			const auto& transform = entity.GetComponent<TransformComponent>();
			const auto& sprite = entity.GetComponent<SpriteComponent>();

			// 设置原始精灵纹理的源矩形
			SDL_Rect srcRect = sprite.srcRect;

			// 设置目标矩形，确定精灵在SDL窗口显示位置
			SDL_Rect dstRect = {
				static_cast<int>(transform.position.x),
				static_cast<int>(transform.position.y),
				static_cast<int>(sprite.width * transform.scale.x),
				static_cast<int>(sprite.height * transform.scale.y)
			};

			// 绘制png纹理
			SDL_RenderCopyEx(
				renderer,
				assetStore->GetTexture(sprite.assetId),
				&srcRect,
				&dstRect,
				transform.rotation,
				NULL,
				SDL_FLIP_NONE
			);
		}
	}
};
#endif // RENDER_H
