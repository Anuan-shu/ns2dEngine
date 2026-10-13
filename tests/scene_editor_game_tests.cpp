#include "../src/Game/Game.h"
#include "../src/Editor/SceneEditor.h"
#include "../src/Editor/SceneSerializer.h"
#include "../src/Time/SimulationClock.h"
#include <imgui/imgui.h>
#include <iostream>

void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    Game game;
    try {
        const int level = argc > 1 ? std::atoi(argv[1]) : 3;
        game.Initialize(); ImGui::GetIO().IniFilename = nullptr; game.Setup(level, true);
        auto& editor = game.GetEditor();
        const auto initial = SceneSerializer::Encode(editor.Capture());
        for (int frame = 0; frame < 3; ++frame) { game.ProcessInput(); game.Update(); game.Render(); }
        Check(editor.IsPaused() && SimulationClock::Seconds() == 0, "编辑模式启动时暂停全部模拟");
        Check(SceneSerializer::Encode(editor.Capture()) == initial, "暂停期间场景数值不变");
        SDL_Event event{}; event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_F6; SDL_PushEvent(&event);
        game.ProcessInput(); game.Update(); game.Render();
        Check(editor.IsPaused() && std::abs(SimulationClock::Seconds() - Physics2D::FixedStepClock::Step) < 1e-7, "真实 F6 按键只推进一个物理步");
        const auto afterStep = SceneSerializer::Encode(editor.Capture());
        game.Update(); game.Render();
        Check(SceneSerializer::Encode(editor.Capture()) == afterStep, "单步后继续保持暂停");
        auto circle = editor.Create(ColliderShape::Circle, {480, 270});
        circle.AddComponent<ProjectileEmitterComponent>(glm::vec2(0, -50), 20, 10000, 10, false);
        circle.GetComponent<SceneObjectComponent>().name = "演示圆形";
        editor.Select(circle);
        editor.SetPaused(false);
        for (int frame = 0; frame < 5; ++frame) { game.ProcessInput(); game.Update(); game.Render(); }
        const int emission = circle.GetComponent<ProjectileEmitterComponent>().lastEmissionTime;
        Check(emission > 8 && SimulationClock::Seconds() > 0.02, "恢复运行后模拟时钟和发射器推进");
        editor.SetPaused(true);
        const auto frozen = SceneSerializer::Encode(editor.Capture());
        SDL_Delay(30);
        for (int frame = 0; frame < 3; ++frame) { game.ProcessInput(); game.Update(); game.Render(); }
        Check(SceneSerializer::Encode(editor.Capture()) == frozen, "真实时间流逝不会推进暂停中的动画与发射计时");
        const auto scenePath = ".build/editor-level-" + std::to_string(level) + ".scene.lua";
        Check(editor.Save(scenePath), "真实关卡可保存全部可编辑对象");
        const auto saved = SceneSerializer::Encode(editor.Capture());
        circle.GetComponent<RigidBodyComponent>().mass = 12;
        Check(editor.Load(scenePath), "真实关卡快照可完整恢复资源与脚本");
        Check(!circle.IsValid() && SceneSerializer::Encode(editor.Capture()) == saved, "恢复后的对象参数与保存时一致，旧句柄失效");
        for (auto entity : editor.Entities()) if (entity.GetComponent<SceneObjectComponent>().name == "演示圆形") editor.Select(entity);
        game.Render();
        Check(game.CaptureScreenshot(".build/editor-level-" + std::to_string(level) + ".bmp"), "实际编辑器画面可以渲染和导出");
        for (const auto& message : Logger::messages) Check(message.type != LOG_ERROR, "编辑器运行没有初始化或资源错误");
        game.Destroy();
        std::cout << "PASS: pause, step, timers, native scene round-trip and editor rendering\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        game.Destroy(); return 1;
    }
}
