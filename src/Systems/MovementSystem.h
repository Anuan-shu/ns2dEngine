#pragma once
#ifndef MOVEMENTSYSTEM_H
#define MOVEMENTSYSTEM_H
class MovementSystem :public System {
public:
	MovementSystem() {
		// 筛选需求的组件
	}

	void Update() {
		for (auto entity : GetEntities()) {

		}
	}
};
#endif // !MOVEMENTSYSTEM_H
