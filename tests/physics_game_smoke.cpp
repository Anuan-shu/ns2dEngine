#include "../src/Game/Game.h"
#include <cstdlib>
#include <iostream>
#include <imgui/imgui.h>
int main(int argc, char** argv) {
    Game game;
    game.Initialize();
    ImGui::GetIO().IniFilename = nullptr;
    // Exercise the real loader, Lua callbacks, physics update and debug renderer.
    game.Setup(argc > 1 ? std::atoi(argv[1]) : 3);
    for (int frame = 0; frame < 180; ++frame) {
        game.ProcessInput();
        game.Update();
        game.Render();
    }
    // Fail on real initialization/loading/runtime errors; the legacy Game
    // destructor itself logs with Err, so inspect before destruction.
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
