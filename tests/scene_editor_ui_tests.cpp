#include "../src/Systems/RenderGUISystem.h"
#include "../src/Systems/RenderPrimitiveSystem.h"
#include <imgui/imgui.h>
#include <imgui/imgui_sdl.h>
#include <iostream>
#include <filesystem>

void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    SDL_Window* window = nullptr; SDL_Renderer* renderer = nullptr;
    bool context = false, backend = false;
    auto cleanup = [&] {
        if (backend) ImGuiSDL::Deinitialize();
        if (context) ImGui::DestroyContext();
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    };
    try {
        Check(SDL_Init(SDL_INIT_VIDEO) == 0, "初始化无窗口测试驱动");
        window = SDL_CreateWindow("editor test", 0, 0, 1280, 800, SDL_WINDOW_HIDDEN);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        Check(window && renderer, "创建真实软件渲染器");
        ImGui::CreateContext(); context = true;
        RenderGUISystem::ConfigureUI();
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DeltaTime = 1.0f / 60;
        io.KeyMap[ImGuiKey_A] = SDL_SCANCODE_A;
        ImGuiSDL::Initialize(renderer, 1280, 800); backend = true;
        Registry registry; registry.AddSystem<RenderPrimitiveSystem>(); AssetStore assets; sol::state lua;
        SceneEditor editor(registry, assets, lua, 3); RenderGUISystem gui;
        auto box = editor.Create(ColliderShape::Box, {350, 220});
        auto circle = editor.Create(ColliderShape::Circle, {480, 270});
        registry.Update(); editor.Select(box);
        SDL_Rect camera{0, 0, 1280, 800};
        auto frame = [&](ImVec2 pointer, bool left = false, bool middle = false) {
            io.MousePos = pointer; io.MouseDown[0] = left; io.MouseDown[2] = middle;
            SDL_SetRenderDrawColor(renderer, 24, 30, 38, 255); SDL_RenderClear(renderer);
            registry.GetSystem<RenderPrimitiveSystem>().Update(renderer, camera);
            ImGui::NewFrame();
            {
                auto deferred = registry.Defer();
                gui.Update(editor, registry, assets, camera);
            }
            ImGui::Render(); ImGuiSDL::Render(ImGui::GetDrawData()); SDL_RenderPresent(renderer);
            registry.Update(); editor.ApplyRequests();
        };
        auto click = [&](float x, float y) { frame({x, y}); frame({x, y}, true); frame({x, y}); };
        frame({400, 600}); frame({400, 600});
        click(50, 52); Check(!editor.IsPaused(), "顶部运行按钮连接到真实控制器");
        click(50, 52); Check(editor.IsPaused(), "顶部暂停按钮有效");
        click(114, 52); Check(editor.ConsumeStep(), "顶部单步按钮产生一次请求");
        click(50, 204); Check(editor.Entities().size() == 3, "实体列表新建按钮有效");
        click(500, 290); Check(editor.Selected() == circle, "在真实界面视口中点击选中圆形");
        frame({500, 290}); frame({500, 290}, true); frame({560, 330}, true); frame({560, 330});
        Check(glm::length(circle.GetComponent<TransformComponent>().position - glm::vec2(540, 310)) < 1, "鼠标拖动改变对象位置");
        frame({600, 360}, false, true); frame({560, 340}, false, true); frame({560, 340});
        Check(camera.x == 40 && camera.y == 20, "中键拖动平移视图");

        // 通过真实文本框选择原内容并输入新路径，不访问系统剪贴板。
        click(200, 80); io.KeyCtrl = true; io.KeysDown[SDL_SCANCODE_A] = true; frame({200, 80});
        io.KeyCtrl = false; io.KeysDown[SDL_SCANCODE_A] = false;
        io.AddInputCharactersUTF8(".build/ui-test.scene.lua"); frame({200, 80});
        Check(gui.CurrentPath() == ".build/ui-test.scene.lua", "文件路径输入框正确接收文字输入");
        gui.SaveFromShortcut(editor); frame({200, 80});
        Check(editor.status.rfind("已保存：.build/ui-test.scene.lua", 0) == 0, "保存快捷键在文本输入后使用最新路径");
        editor.status = "等待保存按钮";
        click(536, 80); Check(std::filesystem::exists(".build/ui-test.scene.lua")
            && editor.status.rfind("已保存：.build/ui-test.scene.lua", 0) == 0, "保存按钮写入所选路径");
        const auto savedPosition = circle.GetComponent<TransformComponent>().position;
        circle.GetComponent<TransformComponent>().position = {900, 900};
        editor.dirty = false; // 本测试只验证加载按钮；未保存修改确认弹窗由正常交互使用。
        click(563, 80); Check(!circle.IsValid(), "加载按钮替换对象且使旧句柄失效");
        for (auto entity : editor.Entities()) if (entity.GetComponent<ColliderComponent>().shape == ColliderShape::Circle) editor.Select(entity);
        auto selected = editor.Selected();
        Check(glm::length(selected.GetComponent<TransformComponent>().position - savedPosition) < 0.001f, "加载按钮恢复保存时的位置");
        frame({500, 600}); frame({500, 600});
        const float mass = selected.GetComponent<RigidBodyComponent>().mass;
        frame({1050, 533}); frame({1050, 533}, true); frame({1100, 533}, true); frame({1100, 533});
        Check(selected.GetComponent<RigidBodyComponent>().mass > mass, "检查器中的拖动字段实际修改刚体质量");
        io.KeyCtrl = true; click(1050, 533);
        io.KeysDown[SDL_SCANCODE_A] = true; frame({1050, 533});
        io.KeyCtrl = false; io.KeysDown[SDL_SCANCODE_A] = false;
        io.AddInputCharactersUTF8("0"); frame({1050, 533}); click(500, 600);
        Check(std::abs(selected.GetComponent<RigidBodyComponent>().mass - 0.001f) < 0.000001f, "直接输入非法动态质量也会限制到有效范围");
        cleanup();
        std::cout << "PASS: actual toolbar, hierarchy, viewport drag, pan, path input, save/load buttons and inspector\n";
    } catch (const std::exception& error) {
        if (renderer) {
            SDL_Surface* surface = SDL_CreateRGBSurface(0, 1280, 800, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
            if (surface) { SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch); SDL_SaveBMP(surface, ".build/ui-test-failure.bmp"); SDL_FreeSurface(surface); }
        }
        std::cerr << "FAIL: " << error.what() << '\n'; cleanup(); return 1;
    }
}
