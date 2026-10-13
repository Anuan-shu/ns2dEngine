#pragma once
#include <string>
#include <utility>

// 编辑器持久化对象的名字和原关卡脚本来源；地图瓦片及运行时子弹不携带此组件。
struct SceneObjectComponent {
    std::string name;
    int scriptIndex = -1;
    explicit SceneObjectComponent(std::string name = "对象", int scriptIndex = -1)
        : name(std::move(name)), scriptIndex(scriptIndex) {}
};
