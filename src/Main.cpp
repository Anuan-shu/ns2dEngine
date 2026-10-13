#include <iostream>
#include"Game/Game.h"
#include<sol/sol.hpp>

int main(int argc, char* argv[]) {

	Game game;

	game.Initialize();

    int level = 2;
    bool editor = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--physics-demo") level = 3;
        if (argument == "--editor") editor = true;
        if (argument == "--level=1") level = 1;
        if (argument == "--level=2") level = 2;
        if (argument == "--level=3") level = 3;
    }
	game.Run(level, editor);

	game.Destroy();
	return 0;
}
