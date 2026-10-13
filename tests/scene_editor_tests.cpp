#include "../src/Editor/SceneEditor.h"
#include "../src/Editor/SceneSerializer.h"
#include "../src/Components/ScriptComponent.h"
#include "../src/Time/SimulationClock.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int checks = 0;
void Check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
template<typename F> void Reject(F&& action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, message);
}
class SceneObjects : public System { public: SceneObjects() { RequireComponent<SceneObjectComponent>(); } };
}
int main() {
    try {
        SimulationClock::Set(0);
        Registry registry; registry.AddSystem<SceneObjects>(); AssetStore assets; sol::state lua;
        lua.open_libraries(sol::lib::base);
        lua.script("calls = 0; Level = {entities = {[7] = {components = {on_update_script = {[0] = function(entity, dt, ms) calls = calls + 1 end}}}}}");
        auto tile = registry.CreateEntity(); tile.Group("tiles"); tile.AddComponent<TransformComponent>(); registry.Update();
        SceneEditor editor(registry, assets, lua, 2);
        auto circle = editor.Create(ColliderShape::Circle, {100, 200}); registry.Update(); editor.Select(circle);
        Check(editor.Entities().size() == 1 && registry.GetSystem<SceneObjects>().Contains(circle), "新建对象进入 ECS 与编辑器列表");
        circle.GetComponent<SceneObjectComponent>().name = "圆形 \"测试\"\\对象\n中文";
        circle.GetComponent<ColliderComponent>().category = 0x80000000u;
        circle.GetComponent<ColliderComponent>().mask = 0xffffffffu;
        circle.GetComponent<TransformComponent>().rotation = 37.5;
        circle.GetComponent<RigidBodyComponent>().mass = 3; circle.GetComponent<RigidBodyComponent>().velocity = {12, 24};
        editor.SetTagAndGroup("test", "objects");
        editor.snap = true; editor.grid = 16; editor.MoveSelected({117, 211});
        Check(circle.GetComponent<TransformComponent>().position == glm::vec2(112, 208), "网格吸附正确");
        editor.snap = false;
        SDL_Rect camera{20, 30, 800, 600};
        Check(editor.Pick({112 + 20 - camera.x, 208 + 20 - camera.y}, camera) == circle, "拾取包含摄像机偏移和圆形形状");
        auto source = editor.Capture();
        const auto encoded = SceneSerializer::Encode(source); const auto decoded = SceneSerializer::Decode(encoded);
        Check(SceneSerializer::Decode(std::string("\xef\xbb\xbf") + encoded).entities.size() == 1, "支持编辑器保存的 UTF-8 BOM 文件");
        Check(decoded.entities[0].object.name == source.entities[0].object.name, "中文、引号、换行和反斜杠可以往返保存");
        Check(decoded.entities[0].collider->mask == 0xffffffffu && decoded.entities[0].collider->category == 0x80000000u, "高位碰撞掩码保持完整");
        Check(decoded.entities[0].body->mass == 3 && decoded.entities[0].transform->rotation == 37.5, "物理和变换参数保持完整");
        Check(decoded.entities[0].primitive->color.b == source.entities[0].primitive->color.b, "形状外观保存完整");
        const auto scenePath = std::filesystem::path(".build/editor-test.scene.lua");
        Check(editor.Save(scenePath.string()), "场景能够写入文件");
        circle.GetComponent<TransformComponent>().position = {900, 900};
        auto duplicate = editor.DuplicateSelected(); registry.Update();
        Check(duplicate != circle && registry.GetEntityTag(duplicate).empty(), "复制使用新句柄且不复制唯一标签");
        editor.Select(duplicate); editor.DeleteSelected(); registry.Update();
        Check(!duplicate.IsValid() && !editor.Selected().IsValid(), "删除清除选择且旧句柄失效");
        Check(editor.Load(scenePath.string()), "保存的场景可重新加载");
        Check(!circle.IsValid() && tile.IsValid(), "加载替换对象但保留背景地图");
        auto restored = editor.Entities().front();
        Check(restored.GetComponent<TransformComponent>().position == glm::vec2(112, 208) && restored.HasTag("test"), "加载恢复组件与标签");
        const auto beforeFailure = SceneSerializer::Encode(editor.Capture());
        Check(!editor.Load(".build/no-such-scene.lua") && SceneSerializer::Encode(editor.Capture()) == beforeFailure, "不存在的文件不能改变当前场景");
        SceneData bad = editor.Capture(); bad.baseLevel = 3;
        SceneSerializer::Save(".build/wrong-level.scene.lua", bad);
        Check(!editor.Load(".build/wrong-level.scene.lua") && restored.IsValid(), "错误基础关卡不能替换对象");
        bad = editor.Capture(); bad.entities[0].sprite = SpriteComponent("missing", 32, 32);
        SceneSerializer::Save(".build/missing-resource.scene.lua", bad);
        Check(!editor.Load(".build/missing-resource.scene.lua") && restored.IsValid(), "缺失资源在修改前被拒绝");
        Reject([&] { SceneSerializer::Decode("return { version = 9 }"); }, "不支持的版本明确失败");
        Reject([&] { SceneSerializer::Decode("while true do end"); }, "数据读取具有执行上限");
        bad = editor.Capture(); bad.entities.push_back(bad.entities.front());
        Reject([&] { SceneSerializer::Encode(bad); }, "重复标签不能保存");
        bad = editor.Capture(); bad.entities[0].body->mass = 0;
        Reject([&] { SceneSerializer::Save(scenePath, bad); }, "非法动态质量不能覆盖文件");
        Check(SceneSerializer::Load(scenePath).entities[0].body->mass == 3, "保存失败时原文件内容保留");
        Reject([&] { SceneSerializer::Save("assets/scripts/level2.lua", editor.Capture()); }, "对象快照不能覆盖原关卡文件");
        bad = editor.Capture(); bad.entities[0].transform->position.x = (std::numeric_limits<float>::infinity)();
        Reject([&] { SceneSerializer::Encode(bad); }, "非有限数值不能保存");

        // 从原关卡函数来源恢复脚本引用及启用状态，不把函数转成文字。
        restored.GetComponent<SceneObjectComponent>().scriptIndex = 7;
        sol::function function = lua["Level"]["entities"][7]["components"]["on_update_script"][0];
        ScriptComponent script(function); script.enabled = false; restored.AddComponent<ScriptComponent>(script); registry.Update();
        Check(editor.Save(scenePath.string()) && editor.Load(scenePath.string()), "带脚本的场景可往返加载");
        restored = editor.Entities().front();
        Check(!restored.GetComponent<ScriptComponent>().enabled, "脚本启用状态保持不变");
        restored.GetComponent<ScriptComponent>().func(restored, 0.01, 0);
        Check(lua["calls"].get<int>() == 1, "加载后的函数引用可执行");
        bad = editor.Capture(); bad.entities[0].object.scriptIndex = 1234;
        SceneSerializer::Save(".build/missing-script.scene.lua", bad);
        Check(!editor.Load(".build/missing-script.scene.lua") && restored.IsValid(), "缺失脚本来源不能破坏当前场景");
        const auto unicodePath = std::filesystem::u8path(".build/中文场景.scene.lua");
        SceneSerializer::Save(unicodePath, editor.Capture());
        Check(SceneSerializer::Load(unicodePath).entities.size() == 1, "中文文件路径可以保存和读取");
        {
            auto deferred = registry.Defer();
            auto created = editor.Create(ColliderShape::Box, {10, 10});
            Check(!created.HasComponent<ColliderComponent>(), "界面遍历期间的新对象组件延迟提交");
        }
        registry.Update(); Check(editor.Entities().size() == 2, "界面提交后新对象可见");
        editor.RequestStep(); Check(editor.ConsumeStep() && !editor.ConsumeStep(), "单步请求只消费一次");
        Check(editor.Reset() && editor.Entities().empty() && tile.IsValid(), "恢复启动快照且保留地图");
        std::cout << "PASS: " << checks << " scene editor checks\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
