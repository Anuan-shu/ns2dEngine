#include "RenderGUISystem.h"
#include "../Physics/EntityShape.h"
#include "../Components/CameraFollowComponent.h"
#include "../Components/ScriptComponent.h"
#include "../Time/SimulationClock.h"
#include <imgui/imgui.h>
#include <imgui/imgui_sdl.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <type_traits>
#include <fstream>

namespace {
constexpr float LeftWidth = 250, RightWidth = 330, TopHeight = 92;
float Bounded(float value, float min, float max, float fallback = 0) {
    return std::isfinite(value) ? glm::clamp(value, min, max) : fallback;
}
void VectorBounds(glm::vec2& value) {
    value.x = Bounded(value.x, -1000000, 1000000); value.y = Bounded(value.y, -1000000, 1000000);
}
template<typename T> void Sanitize(T&) {}
void Sanitize(TransformComponent& c) {
    VectorBounds(c.position);
    c.scale.x = Bounded(c.scale.x, 0.01f, 100, 1); c.scale.y = Bounded(c.scale.y, 0.01f, 100, 1);
    c.rotation = std::isfinite(c.rotation) ? (std::max)(-36000.0, (std::min)(36000.0, c.rotation)) : 0;
}
void Sanitize(RigidBodyComponent& c) {
    VectorBounds(c.velocity); VectorBounds(c.force);
    c.mass = Bounded(c.mass, c.type == BodyType::Dynamic ? 0.001f : 0, 1000000, 1);
    c.restitution = Bounded(c.restitution, 0, 1); c.friction = Bounded(c.friction, 0, 100);
    c.gravityScale = Bounded(c.gravityScale, -100, 100); c.linearDamping = Bounded(c.linearDamping, 0, 100);
}
void Sanitize(ColliderComponent& c) {
    c.width = Bounded(c.width, 0.01f, 1000000, 1); c.height = Bounded(c.height, 0.01f, 1000000, 1);
    c.radius = Bounded(c.radius, c.shape == ColliderShape::Circle ? 0.01f : 0, 500000, 1); VectorBounds(c.offset);
    if (c.shape == ColliderShape::Circle) c.width = c.height = c.radius * 2;
}
void Sanitize(SpriteComponent& c) {
    c.width = glm::clamp(c.width, 1, 8192); c.height = glm::clamp(c.height, 1, 8192);
    c.zIndex = glm::clamp(c.zIndex, -10000, 10000);
    c.srcRect.x = glm::clamp(c.srcRect.x, 0, 1000000); c.srcRect.y = glm::clamp(c.srcRect.y, 0, 1000000);
    c.srcRect.w = c.width; c.srcRect.h = c.height;
}
void Sanitize(AnimationComponent& c) {
    c.numFrames = glm::clamp(c.numFrames, 1, 10000); c.frameSpeedRate = glm::clamp(c.frameSpeedRate, 0, 10000);
    c.currentFrame = glm::clamp(c.currentFrame, 0, c.numFrames - 1);
}
void Sanitize(HealthComponent& c) { c.healthPercentage = glm::clamp(c.healthPercentage, 0, 100); }
void Sanitize(ProjectileEmitterComponent& c) {
    VectorBounds(c.projectileVelocity); c.repeatFrequency = glm::clamp(c.repeatFrequency, 0, 3600000);
    c.projectileDuration = glm::clamp(c.projectileDuration, 1, 3600000); c.hitPercentDamage = glm::clamp(c.hitPercentDamage, 0, 100);
}
void Sanitize(KeyboardControlledComponent& c) { VectorBounds(c.upVelocity); VectorBounds(c.rightVelocity); VectorBounds(c.downVelocity); VectorBounds(c.leftVelocity); }
void Sanitize(TextLabelComponent& c) { VectorBounds(c.position); }
bool TextField(const char* label, std::string& value) {
    std::array<char, 512> buffer{};
    std::memcpy(buffer.data(), value.data(), (std::min)(value.size(), buffer.size() - 1));
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
    value = buffer.data(); return true;
}
bool AssetField(const char* label, std::string& value, const std::vector<std::string>& names) {
    bool changed = false;
    if (ImGui::BeginCombo(label, value.c_str())) {
        for (const auto& name : names) if (ImGui::Selectable(name.c_str(), name == value)) { value = name; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}
bool ColorField(const char* label, SDL_Color& value) {
    float color[4] = {value.r / 255.0f, value.g / 255.0f, value.b / 255.0f, value.a / 255.0f};
    if (!ImGui::ColorEdit4(label, color)) return false;
    value = {static_cast<Uint8>(color[0] * 255), static_cast<Uint8>(color[1] * 255), static_cast<Uint8>(color[2] * 255), static_cast<Uint8>(color[3] * 255)};
    return true;
}
template<typename F> void Attempt(SceneEditor& editor, F&& action) {
    try { action(); } catch (const std::exception& error) { editor.status = "操作失败：" + std::string(error.what()); }
}
template<typename T, typename F> void ComponentPanel(const char* title, SceneEditor& editor, F&& fields, bool removable = true) {
    auto entity = editor.Selected(); auto* value = entity.TryGetComponent<T>();
    if (!value) return;
    ImGui::PushID(title);
    if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
        if (removable && ImGui::SmallButton("移除组件")) {
            // 同时移除直接依赖此组件的行为，避免保存不完整的组合。
            if constexpr (std::is_same_v<T, SpriteComponent>) { entity.RemoveComponent<AnimationComponent>(); entity.RemoveComponent<KeyboardControlledComponent>(); }
            if constexpr (std::is_same_v<T, RigidBodyComponent>) entity.RemoveComponent<KeyboardControlledComponent>();
            if constexpr (std::is_same_v<T, ColliderComponent>) entity.RemoveComponent<PrimitiveRenderComponent>();
            entity.RemoveComponent<T>(); editor.MarkEdited();
        } else if (fields(*value)) {
            // Ctrl 单击可以直接输入数值，仍需校验范围，尤其不能让动画帧数为零。
            Sanitize(*value); editor.MarkEdited();
        }
    }
    ImGui::PopID();
}
}
void RenderGUISystem::ConfigureUI() {
    auto& io = ImGui::GetIO();
    if (std::ifstream("C:/Windows/Fonts/msyh.ttc").good())
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 16, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.FrameRounding = 4; style.GrabRounding = 4;
    style.Colors[ImGuiCol_Button] = ImVec4(0.13f, 0.30f, 0.42f, 1);
    style.Colors[ImGuiCol_Header] = ImVec4(0.13f, 0.30f, 0.42f, 1);
}
void RenderGUISystem::Toolbar(SceneEditor& editor, int width) {
    ImGui::SetNextWindowPos({12, 12}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({static_cast<float>(width - 24), TopHeight}, ImGuiCond_Always);
    ImGui::Begin("场景编辑器", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    if (ImGui::Button(editor.IsPaused() ? "运行 (F5)" : "暂停 (F5)")) editor.SetPaused(!editor.IsPaused());
    ImGui::SameLine(); if (ImGui::Button("单步 (F6)")) editor.RequestStep();
    ImGui::SameLine(); if (ImGui::Button("恢复初始场景")) ImGui::OpenPopup("恢复场景");
    if (ImGui::BeginPopupModal("恢复场景", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("将恢复启动时的对象配置，当前修改会被替换。");
        if (ImGui::Button("恢复")) { editor.RequestReset(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine(); ImGui::Checkbox("碰撞轮廓", &editor.showColliders);
    ImGui::SameLine(); ImGui::Checkbox("网格吸附", &editor.snap);
    ImGui::SameLine(); ImGui::SetNextItemWidth(60); ImGui::DragFloat("间距", &editor.grid, 1, 1, 128, "%.0f");
    ImGui::SameLine(); ImGui::Text("时间 %.2fs", SimulationClock::Seconds());
    ImGui::SetNextItemWidth((std::max)(160.0f, (std::min)(470.0f, width * 0.35f)));
    ImGui::InputText("文件", path, sizeof(path));
    // 快捷键要等文本框吸收本帧输入后再读取路径，避免保存到刚刚修改前的文件。
    if (shortcutSaveRequested) { editor.RequestSave(path); shortcutSaveRequested = false; }
    ImGui::SameLine(); if (ImGui::Button("保存")) editor.RequestSave(path);
    ImGui::SameLine(); if (ImGui::Button("加载")) {
        if (editor.dirty) ImGui::OpenPopup("加载场景"); else editor.RequestLoad(path);
    }
    if (ImGui::BeginPopupModal("加载场景", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("存在未保存修改。加载成功后会替换当前对象。");
        if (ImGui::Button("继续加载")) { editor.RequestLoad(path); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine(); ImGui::TextColored(editor.dirty ? ImVec4(1, 0.75f, 0.3f, 1) : ImVec4(0.4f, 0.8f, 0.65f, 1), "%s", editor.dirty ? "未保存修改" : "已同步");
    ImGui::End();
}
void RenderGUISystem::Hierarchy(SceneEditor& editor, Registry& registry, const SDL_Rect& camera, int height) {
    ImGui::SetNextWindowPos({12, TopHeight + 24}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({LeftWidth, static_cast<float>(height) - TopHeight - 36}, ImGuiCond_Always);
    ImGui::Begin("场景对象", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##filter", "搜索名称、标签或分组", filter, sizeof(filter));
    const auto entities = editor.Entities();
    ImGui::Text("对象 %d   地图瓦片不参与编辑", static_cast<int>(entities.size()));
    const auto position = glm::vec2(camera.x + camera.w / 2, camera.y + camera.h / 2);
    if (ImGui::Button("新建矩形")) editor.Create(ColliderShape::Box, position);
    ImGui::SameLine(); if (ImGui::Button("新建圆形")) editor.Create(ColliderShape::Circle, position);
    ImGui::Separator();
    ImGui::BeginChild("entities", {0, -76}, false);
    for (auto entity : entities) {
        const auto& object = entity.GetComponent<SceneObjectComponent>();
        const auto tag = registry.GetEntityTag(entity), group = registry.GetEntityGroup(entity);
        if (filter[0] && object.name.find(filter) == std::string::npos && tag.find(filter) == std::string::npos && group.find(filter) == std::string::npos) continue;
        ImGui::PushID(entity.GetId());
        const auto label = object.name + (tag.empty() ? "" : " [" + tag + "]");
        if (ImGui::Selectable(label.c_str(), entity == editor.Selected())) editor.Select(entity);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Separator(); ImGui::TextWrapped("%s", editor.status.c_str());
    ImGui::End();
}
void RenderGUISystem::Inspector(SceneEditor& editor, Registry& registry, AssetStore& assets, SDL_Rect& camera, int width, int height) {
    ImGui::SetNextWindowPos({width - RightWidth - 12, TopHeight + 24}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({RightWidth, static_cast<float>(height) - TopHeight - 36}, ImGuiCond_Always);
    ImGui::Begin("组件检查器", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    auto entity = editor.Selected();
    if (!entity.IsValid() || !entity.HasComponent<SceneObjectComponent>()) {
        ImGui::TextWrapped("从左侧列表选择对象，或在画面中点击对象。修改参数会自动暂停模拟。");
        ImGui::End(); return;
    }
    auto& object = entity.GetComponent<SceneObjectComponent>();
    ImGui::Text("实体 %d / 版本 %llu", entity.GetId(), static_cast<unsigned long long>(entity.GetGeneration()));
    if (TextField("名称", object.name)) editor.MarkEdited();
    auto tag = registry.GetEntityTag(entity), group = registry.GetEntityGroup(entity);
    bool identityChanged = TextField("标签", tag); identityChanged |= TextField("分组", group);
    if (identityChanged) Attempt(editor, [&] { editor.SetTagAndGroup(tag, group); });
    if (ImGui::Button("复制")) Attempt(editor, [&] { editor.DuplicateSelected(); });
    ImGui::SameLine(); if (ImGui::Button("删除")) { editor.DeleteSelected(); ImGui::End(); return; }
    ImGui::SameLine(); if (ImGui::Button("定位")) {
        // 定位只改变视图，不属于场景内容修改。
        editor.FocusSelected(camera);
    }
    if (ImGui::Button("添加组件")) ImGui::OpenPopup("add_component");
    if (ImGui::BeginPopup("add_component")) {
        auto add = [&](const char* title, bool present, auto&& action) { if (!present && ImGui::MenuItem(title)) { action(); editor.MarkEdited(); } };
        add("变换", entity.HasComponent<TransformComponent>(), [&] { entity.AddComponent<TransformComponent>(glm::vec2(camera.x + camera.w / 2, camera.y + camera.h / 2)); });
        if (entity.HasComponent<TransformComponent>()) {
            add("刚体", entity.HasComponent<RigidBodyComponent>(), [&] { entity.AddComponent<RigidBodyComponent>(); });
            add("碰撞体", entity.HasComponent<ColliderComponent>(), [&] { entity.AddComponent<ColliderComponent>(40, 40); });
            const auto textures = assets.GetTextureIds();
            if (!textures.empty()) add("精灵", entity.HasComponent<SpriteComponent>(), [&] { entity.AddComponent<SpriteComponent>(textures.front(), 32, 32, 2); });
            add("子弹发射器", entity.HasComponent<ProjectileEmitterComponent>(), [&] { entity.AddComponent<ProjectileEmitterComponent>(glm::vec2(0, -150), 1000, 10000, 10, tag == "player"); });
            add("生命值", entity.HasComponent<HealthComponent>(), [&] { entity.AddComponent<HealthComponent>(100); });
        }
        if (entity.HasComponent<SpriteComponent>()) add("动画", entity.HasComponent<AnimationComponent>(), [&] { entity.AddComponent<AnimationComponent>(); });
        if (entity.HasComponent<ColliderComponent>()) add("形状外观", entity.HasComponent<PrimitiveRenderComponent>(), [&] { entity.AddComponent<PrimitiveRenderComponent>(); });
        if (entity.HasComponent<SpriteComponent>() && entity.HasComponent<RigidBodyComponent>())
            add("键盘控制", entity.HasComponent<KeyboardControlledComponent>(), [&] { entity.AddComponent<KeyboardControlledComponent>(glm::vec2(0, -100), glm::vec2(100, 0), glm::vec2(0, 100), glm::vec2(-100, 0)); });
        const auto fonts = assets.GetFontIds();
        if (!fonts.empty()) add("文字", entity.HasComponent<TextLabelComponent>(), [&] { entity.AddComponent<TextLabelComponent>(glm::vec2(100), "Text", fonts.front(), SDL_Color{255, 255, 255, 255}, false); });
        ImGui::EndPopup();
    }
    ComponentPanel<TransformComponent>("变换", editor, [](auto& c) {
        bool changed = ImGui::DragFloat2("位置", &c.position.x, 1, -1000000, 1000000);
        changed |= ImGui::DragFloat2("缩放", &c.scale.x, 0.01f, 0.01f, 100);
        float rotation = static_cast<float>(c.rotation);
        if (ImGui::DragFloat("角度", &rotation, 1, -36000, 36000, "%.1f°")) { c.rotation = rotation; changed = true; }
        c.scale = glm::clamp(c.scale, glm::vec2(0.01f), glm::vec2(100)); return changed;
    }, false);
    ComponentPanel<RigidBodyComponent>("刚体", editor, [](auto& c) {
        int type = static_cast<int>(c.type); const char* names[] = {"静态", "运动学", "动态"};
        bool changed = ImGui::Combo("类型", &type, names, 3); c.type = static_cast<BodyType>(type);
        changed |= ImGui::DragFloat2("速度", &c.velocity.x, 1, -1000000, 1000000);
        changed |= ImGui::DragFloat2("累积力", &c.force.x, 1, -1000000, 1000000);
        changed |= ImGui::DragFloat("质量", &c.mass, 0.1f, 0.001f, 1000000);
        changed |= ImGui::SliderFloat("弹性", &c.restitution, 0, 1);
        changed |= ImGui::SliderFloat("摩擦", &c.friction, 0, 2);
        changed |= ImGui::DragFloat("重力系数", &c.gravityScale, 0.1f, -100, 100);
        changed |= ImGui::DragFloat("线性阻尼", &c.linearDamping, 0.1f, 0, 100); return changed;
    });
    ComponentPanel<ColliderComponent>("碰撞体", editor, [](auto& c) {
        int shape = static_cast<int>(c.shape); const char* names[] = {"矩形", "圆形"};
        bool changed = ImGui::Combo("形状", &shape, names, 2); c.shape = static_cast<ColliderShape>(shape);
        if (c.shape == ColliderShape::Circle) {
            if (changed && c.radius <= 0) c.radius = (std::max)(0.01f, (std::min)(c.width, c.height) * 0.5f);
            changed |= ImGui::DragFloat("半径", &c.radius, 0.5f, 0.01f, 500000);
        } else {
            changed |= ImGui::DragFloat("宽度", &c.width, 1, 0.01f, 1000000);
            changed |= ImGui::DragFloat("高度", &c.height, 1, 0.01f, 1000000);
        }
        changed |= ImGui::DragFloat2("局部偏移", &c.offset.x, 1, -1000000, 1000000);
        changed |= ImGui::Checkbox("触发器", &c.isTrigger);
        changed |= ImGui::InputScalar("碰撞层", ImGuiDataType_U32, &c.category);
        changed |= ImGui::InputScalar("碰撞掩码", ImGuiDataType_U32, &c.mask); return changed;
    });
    ComponentPanel<PrimitiveRenderComponent>("形状外观", editor, [](auto& c) { return ColorField("颜色", c.color); });
    ComponentPanel<SpriteComponent>("精灵", editor, [&](auto& c) {
        bool changed = AssetField("资源", c.assetId, assets.GetTextureIds());
        changed |= ImGui::DragInt("宽度", &c.width, 1, 1, 8192); changed |= ImGui::DragInt("高度", &c.height, 1, 1, 8192);
        changed |= ImGui::DragInt("图层", &c.zIndex, 1, -10000, 10000); changed |= ImGui::Checkbox("固定在屏幕", &c.isFixed);
        changed |= ImGui::DragInt("纹理起点 X", &c.srcRect.x, 1, 0, 1000000); changed |= ImGui::DragInt("纹理起点 Y", &c.srcRect.y, 1, 0, 1000000);
        int flip = static_cast<int>(c.flip); const char* names[] = {"不翻转", "水平", "垂直", "水平和垂直"};
        changed |= ImGui::Combo("翻转", &flip, names, 4); c.flip = static_cast<SDL_RendererFlip>(flip);
        c.srcRect.w = c.width; c.srcRect.h = c.height; return changed;
    });
    ComponentPanel<AnimationComponent>("动画", editor, [](auto& c) {
        bool changed = ImGui::DragInt("帧数", &c.numFrames, 1, 1, 128); changed |= ImGui::DragInt("每秒帧数", &c.frameSpeedRate, 1, 0, 128);
        changed |= ImGui::Checkbox("循环", &c.isLoop); c.currentFrame = (std::min)(c.currentFrame, c.numFrames - 1); return changed;
    });
    ComponentPanel<HealthComponent>("生命值", editor, [](auto& c) { return ImGui::SliderInt("百分比", &c.healthPercentage, 0, 100); });
    ComponentPanel<ProjectileEmitterComponent>("子弹发射器", editor, [](auto& c) {
        bool changed = ImGui::DragFloat2("子弹速度", &c.projectileVelocity.x, 1, -1000000, 1000000);
        changed |= ImGui::DragInt("间隔（毫秒）", &c.repeatFrequency, 100, 0, 3600000);
        changed |= ImGui::DragInt("寿命（毫秒）", &c.projectileDuration, 100, 1, 3600000);
        changed |= ImGui::SliderInt("伤害", &c.hitPercentDamage, 0, 100); changed |= ImGui::Checkbox("友方子弹", &c.isFriendly); return changed;
    });
    ComponentPanel<KeyboardControlledComponent>("键盘控制", editor, [](auto& c) {
        bool changed = ImGui::DragFloat2("向上速度", &c.upVelocity.x, 1); changed |= ImGui::DragFloat2("向右速度", &c.rightVelocity.x, 1);
        changed |= ImGui::DragFloat2("向下速度", &c.downVelocity.x, 1); changed |= ImGui::DragFloat2("向左速度", &c.leftVelocity.x, 1); return changed;
    });
    ComponentPanel<TextLabelComponent>("文字", editor, [&](auto& c) {
        bool changed = TextField("内容", c.text); changed |= AssetField("字体", c.assetId, assets.GetFontIds());
        changed |= ImGui::DragFloat2("文字位置", &c.position.x, 1); changed |= ImGui::Checkbox("固定在屏幕", &c.isFixed);
        changed |= ColorField("文字颜色", c.color); return changed;
    });
    ComponentPanel<ScriptComponent>("脚本", editor, [&](auto& c) {
        ImGui::Text("来源：第 %d 关对象 %d", editor.BaseLevel(), object.scriptIndex);
        ImGui::TextWrapped("脚本可能覆盖位置或速度。调整这些参数时可先关闭脚本。");
        return ImGui::Checkbox("启用脚本", &c.enabled);
    });
    if (!entity.HasComponent<ScriptComponent>() && object.scriptIndex >= 0 && ImGui::Button("恢复原关卡脚本"))
        Attempt(editor, [&] { editor.RestoreOriginalScript(); });
    if (entity.HasComponent<TransformComponent>()) {
        bool cameraFollow = entity.HasComponent<CameraFollowComponent>();
        if (ImGui::Checkbox("摄像机跟随", &cameraFollow)) {
            if (cameraFollow) {
                for (auto other : editor.Entities()) if (other.HasComponent<CameraFollowComponent>()) other.RemoveComponent<CameraFollowComponent>();
                entity.AddComponent<CameraFollowComponent>();
            } else entity.RemoveComponent<CameraFollowComponent>();
            editor.MarkEdited();
        }
    }
    ImGui::End();
}
void RenderGUISystem::Viewport(SceneEditor& editor, SDL_Rect& camera, int width, int height) {
    auto& io = ImGui::GetIO();
    const bool inside = io.MousePos.x > LeftWidth + 24 && io.MousePos.x < width - RightWidth - 24 && io.MousePos.y > TopHeight + 24 && io.MousePos.y < height - 12;
    if (inside && !io.WantCaptureMouse && ImGui::IsMouseClicked(0)) {
        auto entity = editor.Pick({io.MousePos.x, io.MousePos.y}, camera); editor.Select(entity);
        ImGui::SetWindowFocus(nullptr);
        if (auto* transform = entity.TryGetComponent<TransformComponent>()) {
            const bool fixed = entity.HasComponent<SpriteComponent>() && entity.GetComponent<SpriteComponent>().isFixed;
            dragOffset = transform->position - glm::vec2(io.MousePos.x, io.MousePos.y) - (fixed ? glm::vec2(0) : glm::vec2(camera.x, camera.y));
            dragging = true; dragEntity = entity;
        }
    }
    if (!io.MouseDown[0]) dragging = false;
    if (dragging && dragEntity == editor.Selected() && ImGui::IsMouseDragging(0) && inside && !io.WantCaptureMouse) {
        const bool fixed = dragEntity.HasComponent<SpriteComponent>() && dragEntity.GetComponent<SpriteComponent>().isFixed;
        editor.MoveSelected(glm::vec2(io.MousePos.x, io.MousePos.y) + (fixed ? glm::vec2(0) : glm::vec2(camera.x, camera.y)) + dragOffset);
    }
    if (inside && !io.WantCaptureMouse && ImGui::IsMouseDragging(2)) {
        camera.x -= static_cast<int>(io.MouseDelta.x); camera.y -= static_cast<int>(io.MouseDelta.y);
    }
    auto entity = editor.Selected();
    if (!entity.HasComponent<TransformComponent>()) return;
    Physics2D::WorldShape shape;
    if (entity.HasComponent<ColliderComponent>()) shape = Physics2D::ShapeOf(entity);
    else {
        auto* sprite = entity.TryGetComponent<SpriteComponent>();
        shape = Physics2D::MakeShape(entity.GetComponent<TransformComponent>(), ColliderComponent(sprite ? sprite->width : 16, sprite ? sprite->height : 16));
    }
    const bool fixed = entity.HasComponent<SpriteComponent>() && entity.GetComponent<SpriteComponent>().isFixed;
    const auto offset = fixed ? glm::vec2(0) : glm::vec2(camera.x, camera.y);
    auto* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect({LeftWidth + 24, TopHeight + 24}, {width - RightWidth - 24, static_cast<float>(height - 12)}, true);
    const ImU32 color = IM_COL32(110, 215, 255, 255);
    if (shape.type == ColliderShape::Circle) {
        auto center = shape.center - offset; draw->AddCircle({center.x, center.y}, shape.radius + 3, color, 48, 2);
    } else {
        const auto corners = Physics2D::Corners(shape);
        for (int i = 0; i < 4; ++i) {
            const auto a = corners[i] - offset, b = corners[(i + 1) % 4] - offset;
            draw->AddLine({a.x, a.y}, {b.x, b.y}, color, 2);
        }
    }
    draw->PopClipRect();
}
void RenderGUISystem::Update(SceneEditor& editor, Registry& registry, AssetStore& assets, SDL_Rect& camera) {
    if (!initialized) { std::snprintf(path, sizeof(path), "assets/scenes/level%d.scene.lua", editor.BaseLevel()); initialized = true; }
    const int width = static_cast<int>(ImGui::GetIO().DisplaySize.x), height = static_cast<int>(ImGui::GetIO().DisplaySize.y);
    Toolbar(editor, width); Hierarchy(editor, registry, camera, height); Inspector(editor, registry, assets, camera, width, height);
    Viewport(editor, camera, width, height);
}
