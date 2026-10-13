#include "../src/Game/Game.h"
#include <cstdlib>
#include <iostream>
#include <imgui/imgui.h>
int main(int argc, char** argv) {
    Game game;
    game.Initialize();
    ImGui::GetIO().IniFilename = nullptr;
    // 验证真实关卡加载、Lua 回调、物理更新和调试绘制流程。
    game.Setup(argc > 1 ? std::atoi(argv[1]) : 3);
    for (int frame = 0; frame < 180; ++frame) {
        if (frame == 10) {
            // 触发真实按键回调，验证遍历保护区中的子弹创建与组件提交。
            SDL_Event fire{};
            fire.type = SDL_KEYDOWN;
            fire.key.keysym.sym = SDLK_SPACE;
            SDL_PushEvent(&fire);
        }
        game.ProcessInput();
        game.Update();
        game.Render();
    }
    // 初始化、加载或运行出现实际错误时，测试失败；原有 Game
    // 析构函数本身使用 Err 记录日志，因此在析构前检查错误。
    for (const auto& message : Logger::messages) {
        if (message.type == LOG_ERROR) {
            std::cerr << "FAIL: " << message.message << '\n';
            game.Destroy();
            return 1;
        }
    }
    game.Destroy();
    std::cout << "PASS: loaded and ran 180 game frames\n";
}
