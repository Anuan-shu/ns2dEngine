#pragma once
#ifndef RENDER_H
#define RENDER_H
#include"../ECS/ECS.h"
#include"../Components/SpriteComponent.h"
#include"../Components/TransformComponent.h"
#include"../AssetStore/AssetStore.h"
#include<SDL.h>
#include<algorithm>
class RenderSystem :public System {
public:
	RenderSystem() {
		// 筛选需求的组件
		RequireComponent<TransformComponent>();
		RequireComponent<SpriteComponent>();
	}

	void Update(SDL_Renderer* renderer, std::unique_ptr<AssetStore>& assetStore, SDL_Rect& camera) {
		// 实体按zIndex排序
		struct RenderableEntity {
			TransformComponent transformComponent;
			SpriteComponent spriteComponent;
		};
		std::vector<RenderableEntity> renderableEntities;
		for (auto entity : GetSystemEntities()) {
			RenderableEntity renderableEntity;
			renderableEntity.transformComponent = entity.GetComponent<TransformComponent>();
			renderableEntity.spriteComponent = entity.GetComponent<SpriteComponent>();

			bool isEntityOutsideCameraView = (
				renderableEntity.transformComponent.position.x + (renderableEntity.transformComponent.scale.x * renderableEntity.spriteComponent.width) < camera.x ||
				renderableEntity.transformComponent.position.x > camera.x + camera.w ||
				renderableEntity.transformComponent.position.y + (renderableEntity.transformComponent.scale.y * renderableEntity.spriteComponent.height) < camera.y ||
				renderableEntity.transformComponent.position.y > camera.y + camera.h
				);

			if (isEntityOutsideCameraView && !renderableEntity.spriteComponent.isFixed) {
				continue;
			}

			renderableEntities.emplace_back(renderableEntity);
		}

		std::sort(renderableEntities.begin(), renderableEntities.end(),
			[](const RenderableEntity& a, const RenderableEntity& b) {
				return a.spriteComponent.zIndex < b.spriteComponent.zIndex;
			}
		);

		for (auto& entity : renderableEntities) {
			const auto& transform = entity.transformComponent;
			const auto& sprite = entity.spriteComponent;

			// 设置原始精灵纹理的源矩形
			SDL_Rect srcRect = sprite.srcRect;

			// 设置目标矩形，确定精灵在SDL窗口显示位置
			SDL_Rect dstRect = {
				static_cast<int>(transform.position.x - (sprite.isFixed ? 0 : camera.x)),
				static_cast<int>(transform.position.y - (sprite.isFixed ? 0 : camera.y)),
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
				sprite.flip
			);
		}
	}
};
#endif // RENDER_H
