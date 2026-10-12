#include"Game.h"
#include"../Logger/Logger.h"
#include"LevelLoader.h"
#include"../ECS/ECS.h"
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
#include"../Systems/ScriptSystem.h"
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
	windowWidth = displayMode.w;
	windowHeight = displayMode.h;

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

void Game::Setup(int level) {
	isDebug = level == 3;
	// 给游戏添加系统
	registry->AddSystem<MovementSystem>();
	registry->AddSystem<RenderSystem>();
	registry->AddSystem<AnimationSystem>();
	registry->AddSystem<CollisionSystem>();
	registry->AddSystem<RenderColliderSystem>();
	registry->AddSystem<DamageSystem>();
	registry->AddSystem<KeyboardControlSystem>();
	registry->AddSystem<CameraMoventSystem>();
	registry->AddSystem<ProjectileEmitSystem>();
	registry->AddSystem<ProjectileLifecycleSystem>();
	registry->AddSystem<RenderTextSystem>();
	registry->AddSystem<RenderHealthBarSystem>();
	registry->AddSystem<RenderGUISystem>();
	registry->AddSystem<ScriptSystem>();

	// cpp函数与lua函数绑定
	registry->GetSystem<ScriptSystem>().CreateLuaBindings(lua);

	LevelLoader loader;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::os);
	loader.LoadLevel(lua, registry, assetStore, renderer, level);
	registry->GetSystem<DamageSystem>().SubscribeToEvents(eventBus);
	registry->GetSystem<KeyboardControlSystem>().SubscribeToEvents(eventBus);
	registry->GetSystem<ProjectileEmitSystem>().SubscribeToEvents(eventBus);
	registry->Update();
}
void Game::Update() {
	// 固定帧率上限
	int timeToWait = MILLISECS_PER_FRAME - static_cast<int>(SDL_GetTicks() - millisecsPreviousFrame);
	if (timeToWait > 0 && timeToWait <= MILLISECS_PER_FRAME) {
		SDL_Delay(timeToWait);
	}

	double deltaTime = (SDL_GetTicks() - millisecsPreviousFrame) / 1000.0;

	// 记录当前帧时间
	millisecsPreviousFrame = SDL_GetTicks();

	// 执行固定物理步之前，先提交实体的创建与销毁。
	registry->Update();

	// 更新系统
	physicsClock.Advance(deltaTime, [&](float step) {
		registry->GetSystem<ScriptSystem>().Update(step, SDL_GetTicks());
		registry->GetSystem<MovementSystem>().Update(step);
		registry->GetSystem<CollisionSystem>().Update(eventBus);
		registry->Update();
	});
	registry->GetSystem<AnimationSystem>().Update();
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
void Game::Run(int level) {
	Setup(level);
	millisecsPreviousFrame = SDL_GetTicks();
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