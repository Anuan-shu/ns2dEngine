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
#include"../Systems/RenderPrimitiveSystem.h"
#include"../Editor/SceneEditor.h"
#include"../Time/SimulationClock.h"
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
	    // 在建立字体纹理之前配置中文字体和编辑器样式。
    RenderGUISystem::ConfigureUI();
    ImGui_ImplSDL2_InitForOpenGL(window, nullptr); // 此旧版接口只初始化 SDL 输入，不创建图形上下文。
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
    auto deferred = registry->Defer();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        auto& io = ImGui::GetIO();
        if (event.type == SDL_QUIT) { isRunning = false; continue; }
        if (event.type != SDL_KEYDOWN) continue;
        const auto key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE && !io.WantTextInput) { isRunning = false; continue; }
        if (editor && isDebug && !event.key.repeat && (event.key.keysym.mod & KMOD_CTRL) && key == SDLK_s) {
            editor->SetPaused(true);
            registry->GetSystem<RenderGUISystem>().SaveFromShortcut(*editor); continue;
        }
        if (editor && !io.WantTextInput && !event.key.repeat) {
            if (key == SDLK_F2 || (key == SDLK_d && !io.WantCaptureKeyboard && !io.KeyCtrl)) {
                isDebug = !isDebug;
                editor->SetPaused(isDebug);
                continue;
            }
            if (isDebug && key == SDLK_F5) { editor->SetPaused(!editor->IsPaused()); continue; }
            if (isDebug && key == SDLK_F6) { editor->RequestStep(); continue; }
        }
        // 编辑器输入和暂停期间的按键不能同时控制玩家或发射子弹。
        if (!io.WantCaptureKeyboard && editor && !editor->IsPaused()) eventBus->EmitEvent<KeyPressedEvent>(key);
    }
}

void Game::Setup(int level, bool editorMode) {
	isDebug = editorMode || level == 3;
    SimulationClock::Set(0);
	// 给游戏添加系统
	registry->AddSystem<MovementSystem>();
	registry->AddSystem<RenderSystem>();
    registry->AddSystem<RenderPrimitiveSystem>();
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
    editor = std::make_unique<SceneEditor>(*registry, *assetStore, lua, level);
    editor->SetPaused(editorMode);
    if (editorMode && !editor->Entities().empty()) editor->Select(editor->Entities().front());
    millisecsPreviousFrame = SDL_GetTicks();
}
SceneEditor& Game::GetEditor() { return *editor; }
bool Game::CaptureScreenshot(const std::string& path) {
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) return false;
    SDL_Surface* surface = SDL_CreateRGBSurface(0, width, height, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
    if (!surface) return false;
    const bool success = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0
        && SDL_SaveBMP(surface, path.c_str()) == 0;
    SDL_FreeSurface(surface);
    return success;
}
void Game::SimulateStep(float step) {
    SimulationClock::Advance(step);
    {
        auto deferred = registry->Defer();
        registry->GetSystem<ScriptSystem>().Update(step, static_cast<int>(SimulationClock::Milliseconds()));
        registry->GetSystem<MovementSystem>().Update(step);
        registry->GetSystem<CollisionSystem>().Update(eventBus);
        registry->GetSystem<AnimationSystem>().Update();
        registry->GetSystem<ProjectileEmitSystem>().Update(registry);
        registry->GetSystem<ProjectileLifecycleSystem>().Update();
    }
    registry->Update();
    editor->dirty = true;
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

    editor->ApplyRequests();
    if (editor->IsPaused()) {
        physicsClock = Physics2D::FixedStepClock{};
        if (editor->ConsumeStep()) SimulateStep(static_cast<float>(Physics2D::FixedStepClock::Step));
    } else physicsClock.Advance(deltaTime, [&](float step) { SimulateStep(step); });
    if (!isDebug) registry->GetSystem<CameraMoventSystem>().Update(camera);
    camera.x = (std::max)(0, (std::min)(camera.x, (std::max)(0, mapWidth - camera.w)));
    camera.y = (std::max)(0, (std::min)(camera.y, (std::max)(0, mapHeight - camera.h)));

}
void Game::Render() {
    auto deferred = registry->Defer();
    SDL_SetRenderDrawColor(renderer, 21, 21, 21, 255);
    SDL_RenderClear(renderer);
    registry->GetSystem<RenderSystem>().Update(renderer, assetStore, camera);
    registry->GetSystem<RenderPrimitiveSystem>().Update(renderer, camera);
    registry->GetSystem<RenderTextSystem>().Update(renderer, assetStore, camera);
    registry->GetSystem<RenderHealthBarSystem>().Update(renderer, assetStore, camera);
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(1, 1);
    ImGui::NewFrame();
    if (isDebug) {
        if (editor->showColliders) registry->GetSystem<RenderColliderSystem>().Update(renderer, camera);
        registry->GetSystem<RenderGUISystem>().Update(*editor, *registry, *assetStore, camera);
    }
    ImGui::Render();
    if (isDebug) ImGuiSDL::Render(ImGui::GetDrawData());
    SDL_RenderPresent(renderer);
}

void Game::Run(int level, bool editorMode) {
	Setup(level, editorMode);
	millisecsPreviousFrame = SDL_GetTicks();
	while (isRunning) {
		ProcessInput();
		Update();
		Render();
	}
}

void Game::Destroy() {
    assetStore->ClearAssets();
    ImGui_ImplSDL2_Shutdown();
	ImGuiSDL::Deinitialize();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}
