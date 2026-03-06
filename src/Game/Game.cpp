#include"Game.h"
#include"../Logger/Logger.h"
#include"../ECS/ECS.h"
#include"../Components/TransformComponent.h"
#include"../Components/RigidBodyComponent.h"
#include"../Components/SpriteComponent.h"
#include"../Components/AnimationComponent.h"
#include"../Components/BoxColliderComponent.h"
#include"../Components/KeyboardControlledComponent.h"
#include"../Components/CameraFollowComponent.h"
#include"../Components/ProjectileEmitterComponent.h"
#include"../Components/HealthComponent.h"
#include"../Components/TextLabelComponent.h"
#include"../Systems/MovementSystem.h"
#include"../Systems/RenderSystem.h"
#include"../Systems/AnimationSystem.h"
#include"../Systems/CollisionSystem.h"
#include"../Systems/RenderColliderSystem.h"
#include"../Systems/DamageSystem.h"
#include"../Systems/KeyboardControlSystem.h"
#include"../Systems/CameraMoventSystem.h"
#include"../Systems/ProjectileEmitSystem.h"
#include"../Systems/ProjectileLifecycleSystem.h"
#include"../Systems/RenderTextSystem.h"
#include"../Systems/RenderHealthBarSystem.h"
#include"../Systems/RenderGUISystem.h"
#include<SDL.h>
#include<SDL_image.h>
#include<glm/glm.hpp>
#include<imgui/imgui.h>
#include<imgui/imgui_sdl.h>
#include<imgui/imgui_impl_sdl.h>
#include<iostream>
#include<fstream>

int Game::windowWidth;
int Game::windowHeight;
int Game::mapWidth;
int Game::mapHeight;

Game::Game() {
	isRunning = false;
	isDebug = false;
	registry = std::make_unique<Registry>();
	assetStore = std::make_unique<AssetStore>();
	eventBus = std::make_unique<EventBus>();
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

	if (TTF_Init() != 0) {
		Logger::Err("Error initializing TTF");
		return;
	}
	// 获取显示器相关
	SDL_DisplayMode displayMode;
	SDL_GetCurrentDisplayMode(0, &displayMode);
	windowWidth = 600; // displayMode.w;
	windowHeight = 450;//displayMode.h;

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

	// imgui初始化
	ImGui::CreateContext();
	ImGuiSDL::Initialize(renderer, windowWidth, windowHeight);

	// 全屏模式
	// SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

	// 摄像机设置
	camera.x = 0;
	camera.y = 0;
	camera.w = windowWidth;
	camera.h = windowHeight;

	isRunning = true;
}
void Game::ProcessInput() {
	SDL_Event sdlEvent;
	while (SDL_PollEvent(&sdlEvent)) {
		// imgui 输入
		ImGui_ImplSDL2_ProcessEvent(&sdlEvent);
		ImGuiIO& io = ImGui::GetIO();

		int mouseX, mouseY;
		const int buttons = SDL_GetMouseState(&mouseX, &mouseY);

		io.MousePos = ImVec2(mouseX, mouseY);
		io.MouseDown[0] = buttons & SDL_BUTTON(SDL_BUTTON_LEFT);
		io.MouseDown[1] = buttons & SDL_BUTTON(SDL_BUTTON_RIGHT);


		switch (sdlEvent.type) {
		case SDL_QUIT:
			isRunning = false;
			break;
		case SDL_KEYDOWN:
			if (sdlEvent.key.keysym.sym == SDLK_ESCAPE) {
				isRunning = false;
			}
			if (sdlEvent.key.keysym.sym == SDLK_d) {
				isDebug = !isDebug;
				Logger::Log("isDebug = " + std::to_string(isDebug));
			}
			eventBus->EmitEvent<KeyPressedEvent>(sdlEvent.key.keysym.sym);
			break;
		}
	}

}

void Game::LoadLevel(int level) {
	// 给游戏添加系统
	registry->AddSystem<MovementSystem>();
	registry->AddSystem<RenderSystem>();
	registry->AddSystem<AnimationSystem>();
	registry->AddSystem<CollisionSytem>();
	registry->AddSystem<RenderColliderSystem>();
	registry->AddSystem<DamageSystem>();
	registry->AddSystem<KeyboardControlSystem>();
	registry->AddSystem<CameraMoventSystem>();
	registry->AddSystem<ProjectileEmitSystem>();
	registry->AddSystem<ProjectileLifecycleSystem>();
	registry->AddSystem<RenderTextSystem>();
	registry->AddSystem<RenderHealthBarSystem>();
	registry->AddSystem<RenderGUISystem>();

	// 资产管理器添加纹理
	assetStore->AddTexture(renderer, "tank-image", "./assets/images/tank-panther-right.png");
	assetStore->AddTexture(renderer, "truck-image", "./assets/images/truck-ford-right.png");
	assetStore->AddTexture(renderer, "tilemap-image", "./assets/tilemaps/jungle.png");
	assetStore->AddTexture(renderer, "chopper-image", "./assets/images/chopper-spritesheet.png");
	assetStore->AddTexture(renderer, "radar-image", "./assets/images/radar.png");
	assetStore->AddTexture(renderer, "bullet-image", "./assets/images/bullet.png");
	assetStore->AddTexture(renderer, "tree-image", "./assets/images/tree.png");

	// 资产管理器添加字体
	assetStore->AddFont("charriot-font", "./assets/fonts/charriot.ttf", 14);
	assetStore->AddFont("pico8-font-5", "./assets/fonts/charriot.ttf", 5);
	assetStore->AddFont("pico8-font-10", "./assets/fonts/charriot.ttf", 10);


	// 加载瓦片地图
	int tileSize = 32;
	double tileScale = 1.0;
	int mapnumCols = 25;
	int mapNumRows = 20;
	std::fstream mapFile;
	mapFile.open("./assets/tilemaps/jungle.map");

	for (int y = 0; y < mapNumRows; y++) {
		for (int x = 0; x < mapnumCols; x++) {
			char ch;
			mapFile.get(ch);
			int srcRectY = std::atoi(&ch) * tileSize;
			mapFile.get(ch);
			int srcRectX = std::atoi(&ch) * tileSize;
			mapFile.ignore();

			Entity tile = registry->CreateEntity();
			tile.Group("tiles");
			tile.AddComponent<TransformComponent>(glm::vec2(x * (tileScale * tileSize), y * (tileScale * tileSize)), glm::vec2(tileScale, tileScale), 0.0);
			tile.AddComponent<SpriteComponent>("tilemap-image", tileSize, tileSize, 0, false, srcRectX, srcRectY);
		}
	}
	mapFile.close();
	mapWidth = mapnumCols * tileSize * tileScale;
	mapHeight = mapNumRows * tileSize * tileScale;

	// 注册实体，添加组件
	Entity tank = registry->CreateEntity();
	tank.Group("enemies");
	// 添加组件
	tank.AddComponent<TransformComponent>(glm::vec2(200.0, 10.0), glm::vec2(1.0, 1.0), 0.0);
	tank.AddComponent<RigidBodyComponent>(glm::vec2(40.0, 0.0));
	tank.AddComponent<SpriteComponent>("tank-image", 32, 32, 1);
	tank.AddComponent<BoxColliderComponent>(32, 32);
	tank.AddComponent<ProjectileEmitterComponent>(glm::vec2(100.0, 0.0), 5000, 1000, 10, false);
	tank.AddComponent<HealthComponent>(100);

	// tank.RemoveComponent<TransformComponent>();

	Entity truck = registry->CreateEntity();
	truck.Group("enemies");
	// 添加组件
	truck.AddComponent<TransformComponent>(glm::vec2(100.0, 10.0), glm::vec2(1.0, 1.0), 0.0);
	truck.AddComponent<RigidBodyComponent>(glm::vec2(0.0, 0.0));
	truck.AddComponent<SpriteComponent>("truck-image", 32, 32, 2);
	truck.AddComponent<BoxColliderComponent>(32, 32);
	truck.AddComponent<ProjectileEmitterComponent>(glm::vec2(0.0, 100.0), 2000, 5000, 10, false);
	truck.AddComponent<HealthComponent>(100);


	Entity chopper = registry->CreateEntity();
	chopper.Tag("player");
	chopper.AddComponent<TransformComponent>(glm::vec2(10.0, 10.0), glm::vec2(1.0, 1.0), 0.0);
	chopper.AddComponent<RigidBodyComponent>(glm::vec2(0.0, 0.0));
	chopper.AddComponent<SpriteComponent>("chopper-image", 32, 32, 1);
	chopper.AddComponent<AnimationComponent>(2, 15, true);
	chopper.AddComponent<BoxColliderComponent>(32, 32);
	chopper.AddComponent<KeyboardControlledComponent>(glm::vec2(0, -80), glm::vec2(80, 0), glm::vec2(0, 80), glm::vec2(-80, 0));
	chopper.AddComponent<CameraFollowComponent>();
	chopper.AddComponent<HealthComponent>(100);
	chopper.AddComponent<ProjectileEmitterComponent>(glm::vec2(150.0, 150.0), 0, 10000, 10, true);

	Entity radar = registry->CreateEntity();
	radar.AddComponent<TransformComponent>(glm::vec2(windowWidth - 74, 10.0), glm::vec2(1.0, 1.0), 0.0);
	radar.AddComponent<RigidBodyComponent>(glm::vec2(0.0, 0.0));
	radar.AddComponent<SpriteComponent>("radar-image", 64, 64, 2, true);
	radar.AddComponent<AnimationComponent>(8, 5, true);

	Entity treeA = registry->CreateEntity();
	treeA.Group("obstacles");
	treeA.AddComponent<TransformComponent>(glm::vec2(300, 10), glm::vec2(1.0, 1.0), 0.0);
	treeA.AddComponent<SpriteComponent>("tree-image", 16, 32, 2, false);
	treeA.AddComponent<BoxColliderComponent>(16, 32);

	Entity treeB = registry->CreateEntity();
	treeB.Group("obstacles");
	treeB.AddComponent<TransformComponent>(glm::vec2(150, 10), glm::vec2(1.0, 1.0), 0.0);
	treeB.AddComponent<SpriteComponent>("tree-image", 16, 32, 2, false);
	treeB.AddComponent<BoxColliderComponent>(16, 32);

	Entity label = registry->CreateEntity();
	SDL_Color white = { 255,255,255 };
	label.AddComponent<TextLabelComponent>(glm::vec2(windowWidth / 2 - 40, 10), "CHOPPER", "charriot-font", white, true);


}

void Game::Setup() {
	LoadLevel(1);
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

	// 清空当前帧所有事件处理器
	eventBus->Reset();

	// 为系统订阅事件
	registry->GetSystem<MovementSystem>().SubscribeToEvents(eventBus);
	registry->GetSystem<DamageSystem>().SubscribeToEvents(eventBus);
	registry->GetSystem<KeyboardControlSystem>().SubscribeToEvents(eventBus);
	registry->GetSystem<ProjectileEmitSystem>().SubscribeToEvents(eventBus);

	// 更新注册表 待创建/销毁实体同步
	registry->Update();

	// 更新系统
	registry->GetSystem<MovementSystem>().Update(deltaTime);
	registry->GetSystem<AnimationSystem>().Update();
	registry->GetSystem<CollisionSytem>().Update(eventBus);
	registry->GetSystem<ProjectileEmitSystem>().Update(registry);
	registry->GetSystem<CameraMoventSystem>().Update(camera);
	registry->GetSystem<ProjectileLifecycleSystem>().Update();
}
void Game::Render() {
	SDL_SetRenderDrawColor(renderer, 21, 21, 21, 255);
	SDL_RenderClear(renderer);

	// TODO: 渲染逻辑
	registry->GetSystem<RenderSystem>().Update(renderer, assetStore, camera);
	registry->GetSystem<RenderTextSystem>().Update(renderer, assetStore, camera);
	registry->GetSystem<RenderHealthBarSystem>().Update(renderer, assetStore, camera);
	if (isDebug) {
		registry->GetSystem<RenderColliderSystem>().Update(renderer, camera);

		registry->GetSystem<RenderGUISystem>().Update(registry, camera);
	}


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
	ImGuiSDL::Deinitialize();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}