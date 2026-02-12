#include"Game.h"
#include"../Logger/Logger.h"
#include"../ECS/ECS.h"
#include"../Components/TransformComponent.h"
#include"../Components/RigidBodyComponent.h"
#include"../Components/SpriteComponent.h"
#include"../Systems/MovementSystem.h"
#include"../Systems/RenderSystem.h"
#include<SDL.h>
#include<SDL_image.h>
#include<glm/glm.hpp>
#include<iostream>
Game::Game() {
	isRunning = false;
	registry = std::make_unique<Registry>();
	assetStore = std::make_unique<AssetStore>();
	Logger::Log("Game constructor called");
}
Game::~Game() {
	Logger::Err("Game destructor called");
}
void Game::Initialize() {
	if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
		Logger::Err("Error initializing SDL");
		return;
	}
	// 获取显示器相关
	SDL_DisplayMode displayMode;
	SDL_GetCurrentDisplayMode(0, &displayMode);
	windowWidth = 800; // displayMode.w;
	windowHeight = 600;//displayMode.h;

	window = SDL_CreateWindow(
		NULL,
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		windowWidth,
		windowHeight,
		SDL_WINDOW_BORDERLESS
	);
	if (!window) {
		Logger::Err("Error creating SDL window");
		return;
	}
	renderer = SDL_CreateRenderer(window, -1, 0);
	if (!renderer) {
		Logger::Err("Error creating SDL window");
		return;
	}

	// 全屏模式
	// SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

	isRunning = true;
}
void Game::ProcessInput() {
	SDL_Event sdlEvent;
	while (SDL_PollEvent(&sdlEvent)) {
		switch (sdlEvent.type) {
		case SDL_QUIT:
			isRunning = false;
			break;
		case SDL_KEYDOWN:
			if (sdlEvent.key.keysym.sym == SDLK_ESCAPE) {
				isRunning = false;
			}
			break;
		}
	}

}
void Game::Setup() {
	// 给游戏添加系统
	registry->AddSystem<MovementSystem>();
	registry->AddSystem<RenderSystem>();

	// 资产管理器添加纹理
	assetStore->AddTexture(renderer, "tank-image", "./assets/images/tank-panther-right.png");
	assetStore->AddTexture(renderer, "truck-image", "./assets/images/truck-ford-right.png");

	// TODO:
	// 注册实体，添加组件
	Entity tank = registry->CreateEntity();

	// 添加组件
	// registry->AddComponent<TransformComponent>(tank, glm::vec2(10.0, 30.0), glm::vec2(1.0, 1.0), 0.0);
	// registry->AddComponent<RigidBodyComponent>(tank, glm::vec2(50.0, 0.0));
	tank.AddComponent<TransformComponent>(glm::vec2(10.0, 30.0), glm::vec2(0.0, 0.0), 0.0);
	tank.AddComponent<RigidBodyComponent>(glm::vec2(40.0, 0.0));
	tank.AddComponent<SpriteComponent>("tank-image", 32, 32);


	// tank.RemoveComponent<TransformComponent>();

	Entity truck = registry->CreateEntity();

	// 添加组件
	truck.AddComponent<TransformComponent>(glm::vec2(50.0, 100.0), glm::vec2(1.0, 1.0), 0.0);
	truck.AddComponent<RigidBodyComponent>(glm::vec2(0.0, 50.0));
	truck.AddComponent<SpriteComponent>("truck-image", 32, 32);
}
void Game::Update() {
	// 固定帧率上限
	int timeToWait = MILLISECS_PER_FRAME - (SDL_GetTicks() - millisecsPreviousFrame);
	if (timeToWait > 0 && timeToWait <= MILLISECS_PER_FRAME) {
		SDL_Delay(timeToWait);
	}

	double deltaTime = (SDL_GetTicks() - millisecsPreviousFrame) / 1000.0;

	// 记录当前帧时间
	millisecsPreviousFrame = SDL_GetTicks();

	// TODO:
	//MovementSystem.Update();
	//CollisionSystem.Update();

	// 更新系统
	registry->GetSystem<MovementSystem>().Update(deltaTime);

	// 更新注册表 待创建/销毁实体同步
	registry->Update();
}
void Game::Render() {
	SDL_SetRenderDrawColor(renderer, 21, 21, 21, 255);
	SDL_RenderClear(renderer);

	// TODO: 渲染逻辑
	registry->GetSystem<RenderSystem>().Update(renderer, assetStore);

	// 前后缓冲区交换
	SDL_RenderPresent(renderer);
}
void Game::Run() {
	Setup();
	while (isRunning) {
		ProcessInput();
		Update();
		Render();
	}
}

void Game::Destroy() {
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}