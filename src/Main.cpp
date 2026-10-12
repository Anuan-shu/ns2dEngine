#include <iostream>
#include"Game/Game.h"
#include<sol/sol.hpp>

int main(int argc, char* argv[]) {

	Game game;

	game.Initialize();

	game.Run(argc > 1 && std::string(argv[1]) == "--physics-demo" ? 3 : 2);

	game.Destroy();
	return 0;
}
