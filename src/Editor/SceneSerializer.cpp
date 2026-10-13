#include "SceneSerializer.h"
#include <limits>
#include <sol/sol.hpp>
#include <fstream>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr std::size_t MaxFileSize = 8 * 1024 * 1024;
std::string Quote(const std::string& text) {
    std::ostringstream out; out << '"';
    for (unsigned char ch : text) {
        if (ch == '"' || ch == '\\') out << '\\' << ch;
        else if (ch < 32 || ch == 127) out << '\\' << std::setw(3) << std::setfill('0') << static_cast<int>(ch);
        else out << ch;
    }
    out << '"'; return out.str();
}
std::string Vec(glm::vec2 value) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(9);
    out << "{ x = " << value.x << ", y = " << value.y << " }"; return out.str();
}
sol::table Table(const sol::table& parent, const char* field) {
    sol::object value = parent[field];
    if (value.get_type() != sol::type::table) throw std::runtime_error(std::string("缺少表字段：") + field);
    return value.as<sol::table>();
}
std::optional<sol::table> OptionalTable(const sol::table& parent, const char* field) {
    sol::object value = parent[field];
    if (value.get_type() == sol::type::nil) return std::nullopt;
    if (value.get_type() != sol::type::table) throw std::runtime_error(std::string("字段应为表：") + field);
    return value.as<sol::table>();
}
double Number(const sol::table& table, const char* key, double min = -1000000, double max = 1000000) {
    sol::object value = table[key];
    if (value.get_type() != sol::type::number) throw std::runtime_error(std::string("字段应为数值：") + key);
    const double number = value.as<double>();
    if (!std::isfinite(number) || number < min || number > max) throw std::runtime_error(std::string("数值超出范围：") + key);
    return number;
}
int Integer(const sol::table& table, const char* key, int min = 0, int max = 1000000) {
    const auto number = Number(table, key, min, max);
    if (std::floor(number) != number) throw std::runtime_error(std::string("字段应为整数：") + key);
    return static_cast<int>(number);
}
std::uint32_t Bits(const sol::table& table, const char* key) {
    const double value = Number(table, key, 0, 4294967295.0);
    if (std::floor(value) != value) throw std::runtime_error("碰撞掩码应为整数");
    return static_cast<std::uint32_t>(value);
}
bool Boolean(const sol::table& table, const char* key) {
    sol::object value = table[key];
    if (value.get_type() != sol::type::boolean) throw std::runtime_error(std::string("字段应为布尔值：") + key);
    return value.as<bool>();
}
std::string String(const sol::table& table, const char* key, std::size_t max = 256) {
    sol::object value = table[key];
    if (value.get_type() != sol::type::string) throw std::runtime_error(std::string("字段应为文字：") + key);
    auto text = value.as<std::string>();
    if (text.size() > max || text.find('\0') != std::string::npos) throw std::runtime_error(std::string("文字过长或含有零字符：") + key);
    return text;
}
glm::vec2 ReadVec(const sol::table& table, const char* key) {
    auto vector = Table(table, key);
    return {static_cast<float>(Number(vector, "x")), static_cast<float>(Number(vector, "y"))};
}
}

std::string SceneSerializer::Encode(const SceneData& scene) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(17);
    out << "-- 小型场景编辑器保存的对象快照；背景与脚本来源由 base_level 指定。\nreturn {\n"
        << "version = " << scene.version << ", base_level = " << scene.baseLevel
        << ", simulation_seconds = " << scene.simulationSeconds << ", entity_count = " << scene.entities.size() << ",\nentities = {\n";
    for (const auto& e : scene.entities) {
        out << "{ name = " << Quote(e.object.name) << ", origin_index = " << e.object.scriptIndex
            << ", tag = " << Quote(e.tag) << ", group = " << Quote(e.group) << ",\n";
        if (e.transform) {
            const auto& c = *e.transform;
            out << "transform = { position = " << Vec(c.position) << ", scale = " << Vec(c.scale) << ", rotation = " << c.rotation << " },\n";
        }
        if (e.body) {
            const auto& c = *e.body;
            out << "body = { type = " << static_cast<int>(c.type) << ", velocity = " << Vec(c.velocity) << ", force = " << Vec(c.force)
                << ", mass = " << c.mass << ", restitution = " << c.restitution << ", friction = " << c.friction
                << ", gravity_scale = " << c.gravityScale << ", damping = " << c.linearDamping << " },\n";
        }
        if (e.sprite) {
            const auto& c = *e.sprite;
            out << "sprite = { asset = " << Quote(c.assetId) << ", width = " << c.width << ", height = " << c.height
                << ", z = " << c.zIndex << ", fixed = " << (c.isFixed ? "true" : "false") << ", flip = " << static_cast<int>(c.flip)
                << ", src_x = " << c.srcRect.x << ", src_y = " << c.srcRect.y << " },\n";
        }
        if (e.collider) {
            const auto& c = *e.collider;
            out << "collider = { shape = " << static_cast<int>(c.shape) << ", width = " << c.width << ", height = " << c.height
                << ", radius = " << c.radius << ", offset = " << Vec(c.offset) << ", trigger = " << (c.isTrigger ? "true" : "false")
                << ", category = " << c.category << ", mask = " << c.mask << " },\n";
        }
        if (e.animation) {
            const auto& c = *e.animation;
            out << "animation = { frames = " << c.numFrames << ", frame = " << c.currentFrame << ", speed = " << c.frameSpeedRate
                << ", loop = " << (c.isLoop ? "true" : "false") << ", start_ms = " << c.startTime << " },\n";
        }
        if (e.health) out << "health = { percent = " << e.health->healthPercentage << " },\n";
        if (e.emitter) {
            const auto& c = *e.emitter;
            out << "emitter = { velocity = " << Vec(c.projectileVelocity) << ", frequency_ms = " << c.repeatFrequency
                << ", duration_ms = " << c.projectileDuration << ", damage = " << c.hitPercentDamage
                << ", friendly = " << (c.isFriendly ? "true" : "false") << ", last_ms = " << c.lastEmissionTime << " },\n";
        }
        if (e.keyboard) {
            const auto& c = *e.keyboard;
            out << "keyboard = { up = " << Vec(c.upVelocity) << ", right = " << Vec(c.rightVelocity)
                << ", down = " << Vec(c.downVelocity) << ", left = " << Vec(c.leftVelocity) << " },\n";
        }
        if (e.text) {
            const auto& c = *e.text;
            out << "text = { position = " << Vec(c.position) << ", value = " << Quote(c.text) << ", font = " << Quote(c.assetId)
                << ", fixed = " << (c.isFixed ? "true" : "false") << ", r = " << static_cast<int>(c.color.r)
                << ", g = " << static_cast<int>(c.color.g) << ", b = " << static_cast<int>(c.color.b) << ", a = " << static_cast<int>(c.color.a) << " },\n";
        }
        out << "camera_follow = " << (e.cameraFollow ? "true" : "false") << ",\n";
        if (e.primitive) {
            const auto c = e.primitive->color;
            out << "primitive = { r = " << static_cast<int>(c.r) << ", g = " << static_cast<int>(c.g)
                << ", b = " << static_cast<int>(c.b) << ", a = " << static_cast<int>(c.a) << " },\n";
        }
        if (e.hasScript) out << "script = { source_index = " << e.object.scriptIndex << ", enabled = " << (e.scriptEnabled ? "true" : "false") << " },\n";
        out << "},\n";
    }
    out << "} }\n";
    const auto source = out.str();
    // 保存前也经过相同验证，拒绝写出下次无法加载的文件。
    Decode(source);
    return source;
}
SceneData SceneSerializer::Decode(const std::string& source) {
    if (source.size() > MaxFileSize) throw std::runtime_error("场景文件超过 8 MB");
    sol::state parser;
    // 数据在独立、无标准库的 Lua 状态中读取，不执行原关卡或修改游戏 Lua 状态。
    lua_sethook(parser.lua_state(), [](lua_State* state, lua_Debug*) { luaL_error(state, "场景数据执行指令过多"); }, LUA_MASKCOUNT, 2000000);
    const auto offset = source.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    auto loaded = parser.load(source.substr(offset));
    if (!loaded.valid()) { sol::error error = loaded; throw std::runtime_error(error.what()); }
    sol::protected_function chunk = loaded;
    auto result = chunk();
    if (!result.valid()) { sol::error error = result; throw std::runtime_error(error.what()); }
    sol::object object = result.get<sol::object>();
    if (object.get_type() != sol::type::table) throw std::runtime_error("场景文件必须返回数据表");
    auto root = object.as<sol::table>();
    SceneData scene;
    scene.version = Integer(root, "version", 1, 1);
    scene.baseLevel = Integer(root, "base_level", 1, 3);
    scene.simulationSeconds = Number(root, "simulation_seconds", 0, 4000000);
    const int count = Integer(root, "entity_count", 0, 10000);
    auto entries = Table(root, "entities");
    std::set<std::string> tags;
    for (int i = 1; i <= count; ++i) {
        sol::object object = entries[i];
        if (object.get_type() != sol::type::table) throw std::runtime_error("场景对象列表不完整");
        auto table = object.as<sol::table>();
        SceneEntityData e;
        e.object.name = String(table, "name"); e.tag = String(table, "tag"); e.group = String(table, "group");
        sol::object origin = table["origin_index"];
        if (origin.get_type() != sol::type::nil) e.object.scriptIndex = Integer(table, "origin_index", -1, 10000);
        if (e.group == "tiles" || e.group == "projectiles") throw std::runtime_error("场景对象不能使用保留分组 tiles 或 projectiles");
        if (!e.tag.empty() && !tags.insert(e.tag).second) throw std::runtime_error("场景中存在重复标签");
        if (auto t = OptionalTable(table, "transform")) {
            auto scale = ReadVec(*t, "scale");
            if (scale.x < 0.01f || scale.y < 0.01f || scale.x > 100 || scale.y > 100) throw std::runtime_error("缩放必须介于 0.01 与 100");
            e.transform = TransformComponent(ReadVec(*t, "position"), scale, Number(*t, "rotation", -36000, 36000));
        }
        if (auto t = OptionalTable(table, "body")) {
            RigidBodyComponent c(ReadVec(*t, "velocity"));
            c.type = static_cast<BodyType>(Integer(*t, "type", 0, 2)); c.force = ReadVec(*t, "force");
            c.mass = static_cast<float>(Number(*t, "mass", 0, 1000000));
            if (c.type == BodyType::Dynamic && c.mass <= 0) throw std::runtime_error("动态刚体质量必须大于零");
            c.restitution = static_cast<float>(Number(*t, "restitution", 0, 1)); c.friction = static_cast<float>(Number(*t, "friction", 0, 100));
            c.gravityScale = static_cast<float>(Number(*t, "gravity_scale", -100, 100)); c.linearDamping = static_cast<float>(Number(*t, "damping", 0, 100));
            e.body = c;
        }
        if (auto t = OptionalTable(table, "sprite")) {
            SpriteComponent c(String(*t, "asset"), Integer(*t, "width", 1, 8192), Integer(*t, "height", 1, 8192),
                Integer(*t, "z", -10000, 10000), Boolean(*t, "fixed"), Integer(*t, "src_x", 0, 1000000), Integer(*t, "src_y", 0, 1000000));
            c.flip = static_cast<SDL_RendererFlip>(Integer(*t, "flip", 0, 3)); e.sprite = c;
        }
        if (auto t = OptionalTable(table, "collider")) {
            ColliderComponent c;
            c.shape = static_cast<ColliderShape>(Integer(*t, "shape", 0, 1));
            c.width = static_cast<float>(Number(*t, "width", 0, 1000000)); c.height = static_cast<float>(Number(*t, "height", 0, 1000000));
            c.radius = static_cast<float>(Number(*t, "radius", 0, 1000000)); c.offset = ReadVec(*t, "offset");
            if ((c.shape == ColliderShape::Box && (c.width <= 0 || c.height <= 0)) || (c.shape == ColliderShape::Circle && c.radius <= 0))
                throw std::runtime_error("碰撞体尺寸必须大于零");
            c.isTrigger = Boolean(*t, "trigger"); c.category = Bits(*t, "category"); c.mask = Bits(*t, "mask"); e.collider = c;
        }
        if (auto t = OptionalTable(table, "animation")) {
            AnimationComponent c(Integer(*t, "frames", 1, 10000), Integer(*t, "speed", 0, 10000), Boolean(*t, "loop"));
            c.currentFrame = Integer(*t, "frame", 0, c.numFrames - 1); c.startTime = Integer(*t, "start_ms", 0, (std::numeric_limits<int>::max)()); e.animation = c;
        }
        if (auto t = OptionalTable(table, "health")) e.health = HealthComponent(Integer(*t, "percent", 0, 100));
        if (auto t = OptionalTable(table, "emitter")) {
            ProjectileEmitterComponent c(ReadVec(*t, "velocity"), Integer(*t, "frequency_ms", 0, 3600000),
                Integer(*t, "duration_ms", 1, 3600000), Integer(*t, "damage", 0, 100), Boolean(*t, "friendly"));
            c.lastEmissionTime = Integer(*t, "last_ms", 0, (std::numeric_limits<int>::max)()); e.emitter = c;
        }
        if (auto t = OptionalTable(table, "keyboard")) e.keyboard = KeyboardControlledComponent(ReadVec(*t, "up"), ReadVec(*t, "right"), ReadVec(*t, "down"), ReadVec(*t, "left"));
        if (auto t = OptionalTable(table, "text")) {
            SDL_Color color{static_cast<Uint8>(Integer(*t, "r", 0, 255)), static_cast<Uint8>(Integer(*t, "g", 0, 255)),
                static_cast<Uint8>(Integer(*t, "b", 0, 255)), static_cast<Uint8>(Integer(*t, "a", 0, 255))};
            e.text = TextLabelComponent(ReadVec(*t, "position"), String(*t, "value", 4096), String(*t, "font"), color, Boolean(*t, "fixed"));
        }
        e.cameraFollow = Boolean(table, "camera_follow");
        if (auto t = OptionalTable(table, "primitive")) {
            PrimitiveRenderComponent c;
            c.color = {static_cast<Uint8>(Integer(*t, "r", 0, 255)), static_cast<Uint8>(Integer(*t, "g", 0, 255)),
                static_cast<Uint8>(Integer(*t, "b", 0, 255)), static_cast<Uint8>(Integer(*t, "a", 0, 255))};
            e.primitive = c;
            if (!e.collider) throw std::runtime_error("形状外观需要碰撞体组件");
        }
        if (auto t = OptionalTable(table, "script")) {
            e.hasScript = true; e.object.scriptIndex = Integer(*t, "source_index", 0, 10000); e.scriptEnabled = Boolean(*t, "enabled");
        }
        if (e.animation && !e.sprite) throw std::runtime_error("动画组件需要精灵组件");
        if ((e.body || e.sprite || e.collider || e.cameraFollow) && !e.transform) throw std::runtime_error("此对象缺少变换组件");
        if (e.keyboard && !e.body) throw std::runtime_error("键盘控制需要刚体组件");
        if (e.emitter && !e.transform) throw std::runtime_error("子弹发射器需要变换组件");
        scene.entities.push_back(std::move(e));
    }
    return scene;
}
void SceneSerializer::Save(const std::filesystem::path& path, const SceneData& scene) {
    // 对象快照与原关卡结构不同，使用独立后缀，防止误覆盖关卡或源码。
    if (path.extension() != ".lua" || path.stem().extension() != ".scene")
        throw std::runtime_error("场景文件名必须以 .scene.lua 结尾");
    const auto source = Encode(scene);
    if (path.empty()) throw std::runtime_error("请选择场景文件路径");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("无法写入场景文件");
        file.write(source.data(), static_cast<std::streamsize>(source.size())); file.flush();
        if (!file) throw std::runtime_error("场景文件写入失败，原文件保留");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("无法替换场景文件，原文件保留");
    }
#else
    std::filesystem::rename(temporary, path);
#endif
}
SceneData SceneSerializer::Load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("场景文件不存在或无法读取");
    const auto size = file.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(MaxFileSize)) throw std::runtime_error("场景文件大小无效");
    std::string source(static_cast<std::size_t>(size), '\0'); file.seekg(0);
    file.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (!file && !source.empty()) throw std::runtime_error("场景文件读取失败");
    return Decode(source);
}
