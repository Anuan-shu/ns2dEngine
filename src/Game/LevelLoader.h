#pragma once
#ifndef LEVELLOADER_H
#define LEVELLOADER_H

#include"../ECS/ECS.h"
#include<SDL.h>
#include"../AssetStore/AssetStore.h"
#include<memory>
#include<limits>
#include<sol/sol.hpp>
class LevelLoader {
public:
	LevelLoader();
	~LevelLoader();

	void LoadLevel(sol::state& lua, const std::unique_ptr<Registry>& registry, const std::unique_ptr<AssetStore>& assetStore, SDL_Renderer* renderer, int level);
};
#endif // !LEVELLOADER_H
