#pragma once
#ifndef SCRIPTCOMPONENT_H
#define SCRIPTCOMPONENT_H
#include<sol/sol.hpp>
struct ScriptComponent {
	sol::function func;
    bool enabled = true;
	ScriptComponent(sol::function func = sol::lua_nil) {
		this->func = func;
	}

};
#endif // !SCRIPTCOMPONENT_H
