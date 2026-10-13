#include "../src/ECS/ECS.h"
#include <iostream>
#include <random>
#include <string>
#include <type_traits>

namespace {
int checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<typename Exception, typename F> void Throws(F&& action, const char* message) {
    bool caught = false;
    try { action(); } catch (const Exception&) { caught = true; }
    Check(caught, message);
}
struct Position { int x; explicit Position(int x = 0) : x(x) {} };
struct Velocity { int x; explicit Velocity(int x = 0) : x(x) {} };
struct Other { int x = 0; };
struct MoveOnly {
    std::unique_ptr<int> value;
    explicit MoveOnly(int x) : value(std::make_unique<int>(x)) {}
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
};
struct Throwing {
    explicit Throwing(bool fail) { if (fail) throw std::runtime_error("构造失败"); }
};
class Motion : public System {
public:
    Motion() { RequireComponent<Position>(); RequireComponent<Velocity>(); }
};
class Positions : public System { public: Positions() { RequireComponent<Position>(); } };
class All : public System {};

void PoolRegression() {
    Pool<Position> pool(0);
    Check(pool.TryGet(0) == nullptr, "空池查询返回空指针");
    Throws<std::out_of_range>([&] { pool.Get(0); }, "不存在的组件必须明确失败");
    Throws<std::invalid_argument>([&] { pool.Set(-1, Position()); }, "拒绝负数 ID");
    for (int i = 0; i < 500; ++i) pool.Set(i, Position(i * 3));
    Check(pool.GetSize() == 500, "零容量组件池能够扩容");
    pool.Set(10, Position(999));
    Check(pool.GetSize() == 500 && pool.Get(10).x == 999, "覆盖组件不增加元素");
    Check(pool.Remove(10) && !pool.Remove(10), "重复移除保持安全");
    Check(pool.Get(499).x == 1497, "交换删除修复稀疏索引");
    Check(pool.Remove(498) && pool.Remove(0), "可删除末尾和首个组件");
    Throws<std::out_of_range>([&] { pool.Get(10); }, "移除后查询不会落到下标零");
    pool.Clear();
    Check(pool.IsEmpty() && !pool.Contains(499), "清空同时移除索引");
    pool.Set(900, Position(42));
    Check(pool.GetSize() == 1 && pool.Get(900).x == 42, "清空后可重建");
    Throws<std::out_of_range>([&] { pool[1]; }, "稠密索引检查范围");
    Pool<MoveOnly> owned(0);
    owned.Set(1, MoveOnly(1)); owned.Set(2, MoveOnly(2)); owned.Remove(1);
    Check(*owned.Get(2).value == 2, "支持不可复制且没有默认构造函数的组件");
    // 固定随机种子，与普通映射逐次比较新增、覆盖、删除和清空。
    std::mt19937 random(2026);
    std::unordered_map<int, int> expected;
    pool.Clear();
    for (int step = 0; step < 3000; ++step) {
        const int id = static_cast<int>(random() % 150);
        switch (random() % 20) {
        case 0: pool.Clear(); expected.clear(); break;
        case 1: case 2: case 3: pool.Remove(id); expected.erase(id); break;
        default: pool.Set(id, Position(step)); expected[id] = step;
        }
        Check(pool.GetSize() == expected.size(), "随机操作后组件数量一致");
        for (int entity = 0; entity < 150; ++entity) {
            const auto found = expected.find(entity);
            const auto* actual = pool.TryGet(entity);
            Check((found == expected.end()) == (actual == nullptr), "随机操作后索引一致");
            if (actual) Check(actual->x == found->second, "随机操作后组件数据一致");
        }
    }
}

void LifetimeRegression() {
    Registry registry;
    auto old = registry.CreateEntity(); old.AddComponent<Position>(12); registry.Update();
    Check(old.IsValid() && old.GetGeneration() == 0, "新实体有效");
    static_assert(std::is_same_v<decltype(std::as_const(registry).GetComponent<Position>(old)), const Position&>);
    static_assert(std::is_same_v<decltype(std::as_const(registry).TryGetComponent<Position>(old)), const Position*>);
    old.Kill(); old.Kill();
    Check(old.IsValid() && old.IsPendingRemoval(), "销毁在提交前只标记");
    Throws<std::logic_error>([&] { old.AddComponent<Velocity>(); }, "不能给待销毁实体加入组件");
    registry.Update();
    auto current = registry.CreateEntity(); current.AddComponent<Position>(24); registry.Update();
    Check(old.GetId() == current.GetId() && old.GetGeneration() != current.GetGeneration(), "复用 ID 时增加版本");
    Check(old != current && !old.IsValid() && !old.HasComponent<Position>(), "旧句柄失效");
    Check(old.TryGetComponent<Position>() == nullptr, "旧句柄安全查询");
    Throws<std::invalid_argument>([&] { old.GetComponent<Position>(); }, "旧句柄严格查询失败");
    Throws<std::invalid_argument>([&] { old.RemoveComponent<Position>(); }, "旧句柄不能修改新实体");
    old.Kill(); registry.Update();
    Check(current.IsValid() && current.GetComponent<Position>().x == 24, "旧句柄不能销毁复用槽位的新实体");
    Registry other;
    auto foreign = other.CreateEntity(); foreign.AddComponent<Position>(); other.Update();
    Check(current != foreign && !registry.IsValid(foreign), "注册表之间身份独立");
    Throws<std::invalid_argument>([&] { registry.AddComponent<Position>(foreign, 99); }, "拒绝跨注册表写入");
    Check(registry.TryGetComponent<Position>(foreign) == nullptr, "跨注册表查询返回空指针");
    Entity fabricated(current.GetId()); fabricated.registry = &registry;
    Check(!fabricated.IsValid(), "整数 ID 不能伪造有效句柄");
    Entity survivor;
    {
        Registry temporary;
        survivor = temporary.CreateEntity(); survivor.AddComponent<Position>();
    }
    Check(!survivor.IsValid() && survivor.TryGetComponent<Position>() == nullptr, "注册表析构后句柄安全失效");
    survivor.Kill();
    Throws<std::invalid_argument>([&] { survivor.GetComponent<Position>(); }, "不能解引用已销毁的注册表");
    std::optional<Registry::DeferredChanges> survivingGuard;
    {
        Registry temporary;
        survivingGuard.emplace(temporary.Defer());
    }
    survivingGuard.reset(); // 注册表先析构时，保护区也不能解引用悬空指针。
    registry.Clear();
    Check(!current.IsValid() && registry.GetEntityCount() == 0, "清空使全部旧句柄失效");
    auto fresh = registry.CreateEntity(); fresh.AddComponent<Position>(36); registry.Update();
    Check(fresh.IsValid() && fresh != current && fresh.GetComponent<Position>().x == 36, "注册表清空后可重用");
}

void MembershipRegression() {
    Registry registry;
    registry.AddSystem<Motion>(); registry.AddSystem<Positions>(); registry.AddSystem<All>();
    auto entity = registry.CreateEntity(); entity.AddComponent<Position>(1);
    Check(registry.GetSystem<Positions>().GetSystemEntities().empty(), "新实体等待提交");
    registry.Update();
    auto& motion = registry.GetSystem<Motion>();
    auto& positions = registry.GetSystem<Positions>();
    Throws<std::logic_error>([&] { motion.RequireComponent<Other>(); }, "注册后的系统签名不可变");
    Check(positions.Contains(entity) && !motion.Contains(entity), "按组件签名匹配");
    entity.AddComponent<Velocity>(2); registry.Update();
    Check(motion.Contains(entity), "新增运行时组件加入系统");
    entity.AddComponent<Velocity>(3); registry.Update(); registry.Update(); registry.AddEntityToSystems(entity);
    Check(motion.GetSystemEntities().size() == 1, "重复同步不重复加入");
    entity.RemoveComponent<Velocity>(); registry.Update();
    Check(!motion.Contains(entity) && positions.Contains(entity), "运行时移除组件离开对应系统");
    entity.RemoveComponent<Velocity>(); registry.Update();
    Check(positions.GetSystemEntities().size() == 1, "移除不存在的组件不破坏成员列表");
    Check(entity.TryGetComponent<Other>() == nullptr && !entity.HasComponent<Other>(), "尚未创建组件池的查询安全");
    Throws<std::out_of_range>([&] { entity.GetComponent<Other>(); }, "缺失组件严格查询抛错");
    entity.AddComponent<MoveOnly>(8); Check(*entity.GetComponent<MoveOnly>().value == 8, "注册表支持只移动组件");
    Throws<std::runtime_error>([&] { entity.AddComponent<Throwing>(true); }, "组件构造异常可报告");
    Check(!entity.HasComponent<Throwing>(), "构造失败不能污染签名");
    registry.RemoveSystem<Positions>(); registry.AddSystem<Positions>();
    Check(registry.GetSystem<Positions>().Contains(entity), "后注册系统匹配已有实体");
    Throws<std::logic_error>([&] { registry.AddSystem<Positions>(); }, "重复注册系统明确失败");
    registry.RemoveSystem<Positions>(); registry.RemoveSystem<Positions>();
    Throws<std::out_of_range>([&] { registry.GetSystem<Positions>(); }, "未注册系统查询明确失败");
    auto unborn = registry.CreateEntity(); unborn.AddComponent<Position>(); unborn.Kill(); registry.Update();
    Check(!unborn.IsValid() && registry.GetSystem<All>().GetSystemEntities().size() == 1, "创建后立即销毁不加入系统");
    registry.Clear();
    Check(motion.GetSystemEntities().empty() && registry.HasSystem<Motion>(), "清空保留系统并清空成员索引");
    auto fresh = registry.CreateEntity(); fresh.AddComponent<Position>(); fresh.AddComponent<Velocity>(); registry.Update();
    Check(motion.GetSystemEntities().size() == 1 && motion.Contains(fresh), "清空后的系统可正确重建");
}

void DeferredRegression() {
    Registry registry; registry.AddSystem<Positions>();
    auto entity = registry.CreateEntity(); entity.AddComponent<Position>(7); registry.Update();
    const auto& view = registry.GetSystem<Positions>().GetSystemEntities();
    static_assert(std::is_same_v<decltype(view), const std::vector<Entity>&>);
    Position* original = &entity.GetComponent<Position>();
    Entity added;
    {
        auto deferred = registry.Defer();
        for (auto handle : view) {
            handle.RemoveComponent<Position>();
            handle.AddComponent<Position>(9);
            Check(handle.GetComponent<Position>().x == 7, "结构变更在遍历时不可见");
            added = registry.CreateEntity(); added.AddComponent<Position>(10);
        }
        // 大量新增与扩容在提交前不能改变正在使用的组件地址。
        for (int i = 0; i < 500; ++i) registry.CreateEntity().AddComponent<Position>(i);
        Check(&entity.GetComponent<Position>() == original && view.size() == 1, "遍历中的组件引用和实体视图稳定");
        Check(!added.HasComponent<Position>(), "新实体的排队组件需要提交后可见");
        Throws<std::logic_error>([&] { registry.Update(); }, "禁止遍历中提交");
        Throws<std::logic_error>([&] { registry.Clear(); }, "禁止遍历中清空");
        Throws<std::logic_error>([&] { registry.RemoveSystem<Positions>(); }, "禁止遍历中移除系统");
        {
            auto nested = registry.Defer();
            added.AddComponent<Velocity>(2);
        }
        Check(!added.HasComponent<Velocity>(), "内层保护区结束不自动提交");
    }
    Check(entity.GetComponent<Position>().x == 7, "保护区析构不隐式提交");
    registry.Update();
    Check(entity.GetComponent<Position>().x == 9 && added.GetComponent<Position>().x == 10, "按请求顺序提交组件变更");
    Check(added.HasComponent<Velocity>() && view.size() == 502, "嵌套命令和系统成员统一提交");
    {
        auto deferred = registry.Defer();
        entity.AddComponent<Velocity>(4); entity.Kill();
    }
    registry.Update();
    Check(!entity.IsValid() && !registry.GetSystem<Positions>().Contains(entity), "销毁优先取消旧版本命令");
    auto replacement = registry.CreateEntity(); replacement.AddComponent<Position>(123); registry.Update();
    Check(replacement.GetId() == entity.GetId() && !replacement.HasComponent<Velocity>(), "旧命令不能泄漏到复用的实体");
    {
        auto deferred = registry.Defer(); replacement.AddComponent<Velocity>(88);
    }
    registry.Clear(); auto fresh = registry.CreateEntity(); fresh.AddComponent<Position>(); registry.Update();
    Check(!fresh.HasComponent<Velocity>(), "清空取消待提交命令");
    // 保护区异常退出后必须解除锁定；此前已排队的命令保留到显式提交。
    try { auto deferred = registry.Defer(); fresh.AddComponent<Velocity>(5); throw std::runtime_error("退出"); }
    catch (const std::runtime_error&) {}
    registry.Update(); Check(fresh.GetComponent<Velocity>().x == 5, "异常退出解除保护区");
    int visited = 0;
    registry.Each<Positions>([&](Entity handle) {
        ++visited;
        handle.RemoveComponent<Position>();
        Check(handle.HasComponent<Position>(), "Each 自动保护遍历期间的结构修改");
    });
    Check(visited == 1 && fresh.HasComponent<Position>(), "Each 返回后仍需显式提交");
    registry.Update();
    Check(!fresh.HasComponent<Position>() && registry.GetSystem<Positions>().GetSystemEntities().empty(), "Each 的命令正常提交");
}

void IndexRegression() {
    Registry registry;
    auto a = registry.CreateEntity(), b = registry.CreateEntity();
    a.Tag("player"); a.Tag("player"); a.Tag("hero");
    Check(!registry.TryGetEntityByTag("player") && registry.GetEntityByTag("hero") == a, "改名同步双向标签索引");
    Throws<std::invalid_argument>([&] { b.Tag("hero"); }, "标签唯一性冲突明确失败");
    Check(!b.HasTag("hero") && a.HasTag("hero"), "失败不能破坏原标签");
    a.Group("old"); a.Group("new"); a.Group("new");
    Check(!a.BelongsToGroup("old") && registry.GetEntitiesByGroup("old").empty(), "换组移除旧成员");
    Check(registry.GetEntitiesByGroup("new").size() == 1, "重复分组不重复添加");
    {
        auto deferred = registry.Defer(); a.Tag("updated"); a.Group("updated");
        Check(a.HasTag("hero") && a.BelongsToGroup("new"), "标签与分组遵循延迟提交");
    }
    registry.Update();
    Check(a.HasTag("updated") && a.BelongsToGroup("updated"), "标签与分组提交正确");
    registry.RemoveEntityTag(a); registry.RemoveEntityTag(a);
    registry.RemoveEntityGroup(a); registry.RemoveEntityGroup(a);
    Check(!a.HasTag("updated") && !a.BelongsToGroup("updated"), "重复移除标签分组安全");
    Throws<std::out_of_range>([&] { registry.GetEntityByTag("missing"); }, "未知标签查询明确失败");
    a.Tag("reuse"); a.Group("reuse"); a.Kill(); registry.Update();
    auto fresh = registry.CreateEntity(); registry.Update();
    Check(!fresh.HasTag("reuse") && !fresh.BelongsToGroup("reuse") && registry.GetEntitiesByGroup("reuse").empty(), "销毁清理所有标签分组索引");
}

void RegistryStressRegression() {
    Registry registry; registry.AddSystem<Motion>(); registry.AddSystem<Positions>();
    struct Expected { Entity entity; bool alive = true, position = false, velocity = false; int x = 0; };
    std::vector<Expected> model;
    std::mt19937 random(20261013);
    for (int step = 0; step < 1200; ++step) {
        {
            auto deferred = registry.Defer();
            if (model.empty() || random() % 4 == 0) {
                auto entity = registry.CreateEntity();
                model.push_back({entity});
            } else {
                auto& expected = model[random() % model.size()];
                if (expected.alive) switch (random() % 5) {
                case 0: expected.entity.Kill(); expected.alive = false; break;
                case 1: expected.entity.AddComponent<Position>(step); expected.position = true; expected.x = step; break;
                case 2: expected.entity.RemoveComponent<Position>(); expected.position = false; break;
                case 3: expected.entity.AddComponent<Velocity>(step); expected.velocity = true; break;
                case 4: expected.entity.RemoveComponent<Velocity>(); expected.velocity = false; break;
                }
            }
        }
        registry.Update();
        std::size_t aliveCount = 0, positionCount = 0, motionCount = 0;
        for (const auto& expected : model) {
            Check(expected.entity.IsValid() == expected.alive, "随机生命周期与版本号一致");
            if (!expected.alive) continue;
            ++aliveCount;
            Check(expected.entity.HasComponent<Position>() == expected.position, "随机组件签名一致");
            Check(expected.entity.HasComponent<Velocity>() == expected.velocity, "随机组件增删一致");
            const bool moves = expected.position && expected.velocity;
            Check(registry.GetSystem<Positions>().Contains(expected.entity) == expected.position, "随机单组件系统成员一致");
            Check(registry.GetSystem<Motion>().Contains(expected.entity) == moves, "随机多组件系统成员一致");
            if (expected.position) { ++positionCount; Check(expected.entity.GetComponent<Position>().x == expected.x, "随机组件内容一致"); }
            if (moves) ++motionCount;
        }
        Check(registry.GetEntityCount() == aliveCount, "随机操作后存活数量一致");
        Check(registry.GetSystem<Positions>().GetSystemEntities().size() == positionCount, "随机操作后无重复单组件成员");
        Check(registry.GetSystem<Motion>().GetSystemEntities().size() == motionCount, "随机操作后无重复多组件成员");
    }
}

template<int N> struct TypeTag {};
template<int N> void ExhaustTypes() {
    if constexpr (N > 0) {
        try { Component<TypeTag<N>>::GetId(); }
        catch (const std::length_error&) { return; }
        ExhaustTypes<N - 1>();
    } else throw std::runtime_error("组件类型上限未生效");
}
}
int main() {
    try {
        PoolRegression(); LifetimeRegression(); MembershipRegression(); DeferredRegression(); IndexRegression(); RegistryStressRegression();
        // 类型上限测试最后执行，避免影响前面的组件注册。
        ExhaustTypes<40>();
        Throws<std::length_error>([] { Component<TypeTag<99>>::GetId(); }, "超过组件上限必须抛出受控异常");
        std::cout << "PASS: " << checks << " ECS checks\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
