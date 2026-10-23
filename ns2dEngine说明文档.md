# ns2dEngine 重构版源码精读与面试学习文档

实现教程修订：2026-10-16。对应源码：`2b03a82`。前18章按“代码怎么写、怎样运行”的顺序重新编写，后面的问答、练习和资料索引保留。

本文可以从头独立阅读。每章先给出模块使用方式和内部结构，再贴出当前项目中的关键实现，随后跟踪调用和数据变化，最后才讨论取舍。标注“源码节选”的代码直接取自本地文件；标注“教学示例”的代码用于演示调用或说明等价流程，不表示项目里另外存在这段函数。

第一次阅读，顺着“创建一个敌人，让它移动、碰撞、显示，再在编辑器中保存和恢复”的例子走。暂时不熟悉的API会在出现时说明，不要求先翻完整源码。源码链接供你之后打断点或查上下文。

## 目录

1. [先看一个游戏对象怎样写出来，再看总体架构](#chapter-1)
2. [Game怎么组织初始化、输入、更新和绘制](#chapter-2)
3. [Entity和Registry怎样实现创建、访问和销毁](#chapter-3)
4. [Pool怎样用三条数组存放、查找和删除组件](#chapter-4)
5. [从组件类型到系统成员：AddComponent整条链](#chapter-5)
6. [Defer怎样保护遍历，Update怎样提交命令](#chapter-6)
7. [标签和分组怎样维护双向索引](#chapter-7)
8. [固定步长和模拟时钟的代码怎样配合](#chapter-8)
9. [Collider如何转换成参与检测的世界形状](#chapter-9)
10. [从形状数组到接触列表：宽阶段和窄阶段代码](#chapter-10)
11. [物理运动与响应：从组件到速度和位置变化](#chapter-11)
12. [EventBus、输入和伤害怎样连起来](#chapter-12)
13. [Lua关卡和脚本函数怎样成为运行中的对象](#chapter-13)
14. [RenderSystem、动画、图元、相机和文字怎样绘制](#chapter-14)
15. [场景编辑器的类怎样组织，操作怎样落到ECS](#chapter-15)
16. [场景快照怎样编码、校验、保存并恢复](#chapter-16)
17. [AssetStore、日志和底层C++机制怎样服务各模块](#chapter-17)
18. [把各模块连成两个可完整推演的例子](#chapter-18)
19. [性能、局限和下一步设计：如何说得可信](#chapter-19)
20. [测试证据：如何证明理解而不夸大](#chapter-20)
21. [六十个高频问题：先回答，再接追问](#chapter-21)
22. [现场推演与改代码练习](#chapter-22)
23. [项目讲述、连续追问与七天掌握计划](#chapter-23)
24. [源码导航和断点建议](#chapter-24)

<a id="chapter-1"></a>

## 1. 先看一个游戏对象怎样写出来，再看总体架构

### 1.1 从一小段能看懂的调用开始

下面是教学示例：在初始化或其他**没有开启Defer保护区**的阶段创建一个可移动敌人。完整游戏使用Lua关卡创建对象，但最终会调用相同的C++接口。

```cpp
Registry registry;
registry.AddSystem<MovementSystem>();
registry.AddSystem<RenderSystem>();

Entity enemy = registry.CreateEntity();
enemy.AddComponent<TransformComponent>(glm::vec2(100, 200));
enemy.AddComponent<RigidBodyComponent>(glm::vec2(60, 0));
enemy.AddComponent<SpriteComponent>("tank-texture", 32, 32, 2);
enemy.Group("enemies");
registry.Update();
```

按代码顺序理解：第一行创建一份“游戏对象数据库”；接着放入移动和渲染两个处理器。CreateEntity申请一个对象身份；三个AddComponent分别保存位置、速度和图像信息；Group保存类别；Update把这个实体加入满足条件的系统。

这个敌人没有对应的`class Enemy : public GameObject`。它的数据分散在三个按类型管理的数组里，`enemy`只是找到这些数据的句柄。`registry.Update()`也不是“让敌人移动一帧”：这里负责提交结构变化、更新系统成员，真正移动由MovementSystem::Update执行。

### 1.2 Component就是明确的数据结构

位置组件的完整定义如下。`glm::vec2`是一个含x、y两个数的二维向量；position是位置，scale是缩放，rotation是角度。

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Components/TransformComponent.h:1)）：

```cpp
#pragma once
#ifndef TRANSFORMCOMPONENT_H
#define TRANSFORMCOMPONENT_H
#include<glm/glm.hpp>
struct TransformComponent {
	glm::vec2 position;
	glm::vec2 scale;
	double rotation;

	TransformComponent(glm::vec2 position = glm::vec2(0, 0),
		glm::vec2 scale = glm::vec2(1, 1), double rotation = 0.0) {
		this->position = position;
		this->scale = scale;
		this->rotation = rotation;
	}
};
#endif // !TRANSFORMCOMPONENT_H
```

构造`TransformComponent(glm::vec2(100,200))`时，位置采用传入值，缩放用默认(1,1)，旋转用默认0。一个结构体保存状态，系统取得它的引用后修改字段，不需要通过虚函数让每个对象自行Update。

RigidBodyComponent除了速度，还保存质量、力、材质等，后面的物理章节会逐项解释。SpriteComponent保存纹理资源名、尺寸和绘制方式，并不把整张PNG像素复制进每个敌人。

### 1.3 运行时真正存在的对象

| 对象 | 可理解成什么 | 关键内容 |
|---|---|---|
| Game | 总控制器 | 窗口、渲染器、Registry、资源、事件、Lua、编辑器 |
| Registry | 一份游戏世界 | 槽位、组件池、系统、标签与待处理修改 |
| Entity | 对象身份卡 | 所属世界、槽位ID、版本、生命周期标记 |
| Pool<T> | 某类数据的仓库 | 连续的T对象、对应实体、快速定位索引 |
| System | 批量处理器 | 要求哪些组件、当前匹配哪些实体 |
| EventBus | 同步通知中心 | 某类事件对应哪些成员函数回调 |
| AssetStore | 已加载资源目录 | 字符串ID到纹理/字体指针 |
| SceneEditor | 编辑操作控制器 | 选择对象、暂停状态、快照和保存加载请求 |

整个架构的数据流可以先记成：**加载器把配置变组件 → Registry组织数据和成员 → Game调用系统 → 系统改数据或绘制 → 编辑器使用同一套数据接口操作场景。**

### 1.4 同一个敌人怎样被多个系统处理

Movement要求Transform和RigidBody；Render要求Transform和Sprite。上面的敌人两组条件都满足，所以两个系统各保存它的句柄；它的组件仍只有一份，不会为每个系统复制一份敌人。

```text
敌人身份 enemy
  ├─ Transform池中的位置 (100,200)
  ├─ RigidBody池中的速度 (60,0)
  └─ Sprite池中的纹理 tank-texture

Movement列表含enemy → 改Transform
Render列表含enemy   → 读修改后的Transform并绘制
```

至此，ECS的三个字母才有具体含义：Entity负责身份、Component保存数据、System处理相应实体集合。Registry是把它们连接起来的管理者。项目采用这种组织方式的收益与成本，会在数据结构讲完后讨论。

<a id="chapter-2"></a>

## 2. Game怎么组织初始化、输入、更新和绘制

### 2.1 Game拥有的核心成员

下面是教学摘写，省略方法声明和部分显示参数，保留所有权关系：

```cpp
class Game {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Physics2D::FixedStepClock physicsClock;
    SDL_Rect camera;

    sol::state lua;
    std::unique_ptr<Registry> registry;
    std::unique_ptr<AssetStore> assetStore;
    std::unique_ptr<EventBus> eventBus;
    std::unique_ptr<SceneEditor> editor;
};
```

`unique_ptr`表示Game负责独占这些模块的生命周期。SDL窗口和渲染器仍用裸指针，由Destroy显式释放。`sol::state`是Lua运行环境；camera不是一台真正的摄像机对象，而是表示世界中可见区域的矩形。

### 2.2 启动过程一层层展开

Main创建Game，调用Initialize，解析关卡和编辑参数，然后调用Run。Initialize创建SDL/TTF环境、窗口、renderer和ImGui；此时并没有加载全部游戏实体。

Run很短，先Setup，再进入循环：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Game/Game.cpp:232)）：

```cpp
void Game::Run(int level, bool editorMode) {
	Setup(level, editorMode);
	millisecsPreviousFrame = SDL_GetTicks();
	while (isRunning) {
		ProcessInput();
		Update();
		Render();
	}
}
```

Setup做五件事：注册各个System；向Lua注册游戏API；执行选定关卡并创建资源与实体；注册键盘与碰撞订阅并提交Registry；创建SceneEditor并捕获启动快照。编辑模式下还把模拟设为暂停。这样第一轮输入发生时，系统和事件回调都已经准备好。

### 2.3 输入怎样被分给GUI和游戏

ProcessInput通过SDL_PollEvent依次读取窗口事件，首先调用ImGui的输入处理，让它更新键盘焦点、鼠标和文字输入状态。然后处理退出、保存、切编辑模式、暂停和单步快捷键。只有界面不占用键盘、且游戏没有暂停，才向EventBus发布KeyPressedEvent。

源码中的最终游戏分发条件为：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Game/Game.cpp:122)）：

```cpp
// 编辑器输入和暂停期间的按键不能同时控制玩家或发射子弹。
if (!io.WantCaptureKeyboard && editor && !editor->IsPaused()) eventBus->EmitEvent<KeyPressedEvent>(key);
```

这一条件使“在输入框里敲方向键”和“操作玩家”不会无条件同时发生。整个ProcessInput有Defer保护，按键回调创建子弹时只排组件结构命令，下一Update先统一提交。Defer的内部实现在第6章。

### 2.4 一个固定模拟步里，系统按什么顺序调用

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Game/Game.cpp:173)）：

```cpp
void Game::SimulateStep(float step) {
    SimulationClock::Advance(step);
    {
        auto deferred = registry->Defer();
        registry->GetSystem<ScriptSystem>().Update(step, static_cast<int>(SimulationClock::Milliseconds()));
        registry->GetSystem<MovementSystem>().Update(step);
        registry->GetSystem<CollisionSystem>().Update(eventBus);
        registry->GetSystem<AnimationSystem>().Update();
        registry->GetSystem<ProjectileEmitSystem>().Update(registry);
        registry->GetSystem<ProjectileLifecycleSystem>().Update();
    }
    registry->Update();
    editor->dirty = true;
}
```

逐句跟踪：先推进模拟时间；建立结构修改保护区；脚本先修改位置/速度/力；Movement积分；Collision检测和响应；Animation选帧；Emit生成子弹；Lifecycle处理过期。大括号结束只解除保护，随后Registry.Update真正提交结构变化。最后dirty=true，表示场景可能发生变化。

因此，Lua本步施加的力会被本步Movement使用；自动子弹在碰撞之后创建，子步结束才进入系统，下个子步才参与移动和碰撞。不能只背一个系统列表而忽略这种行为。

### 2.5 每帧Update又怎样安排固定步

Update先等待以限制帧率并计算真实帧间隔，然后提交输入阶段的结构命令，执行编辑器保存/加载请求。运行中交给FixedStepClock决定执行几个SimulateStep；暂停时清理积压，只有消费单步请求才执行一个固定步。最后处理相机跟随与边界。

Render则每帧都执行，即使暂停也会清屏、绘制精灵/图元/文字/血条、构建编辑器界面、SDL_RenderPresent提交画面。这样暂停只是世界停止前进，界面仍然可操作。

### 2.6 退出代码如何与成员生命周期配合

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Game/Game.cpp:242)）：

```cpp
void Game::Destroy() {
    assetStore->ClearAssets();
    ImGui_ImplSDL2_Shutdown();
	ImGuiSDL::Deinitialize();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}
```

先ClearAssets再销毁renderer，是因为Texture依赖renderer；先让业务对象中的Lua函数引用结束，再关闭Lua，则由Game成员声明和逆序析构关系保证。editor最后声明先析构，Lua更早声明晚析构。

当前Initialize失败只是return，主流程的失败传播仍可加强；Destroy也不是一个处理任意初始化阶段、可无限重复调用的完整状态机。先理解正常流程怎样写，再把这些视为后续健壮性问题。

<a id="chapter-3"></a>

## 3. Entity和Registry怎样实现创建、访问和销毁

### 3.1 先区分“句柄”和“存储槽位”

Entity是外部拿到的身份值，Registry内部用slots数组保存每个槽位当前状态。下面是源码中的成员节选：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:40)）：

```cpp
class Entity {
    int id = -1;
    std::uint64_t generation = 0;
    std::uint64_t ownerIdentity = 0;
    std::weak_ptr<RegistryLifetime> lifetime;
    Entity(int id, std::uint64_t generation, Registry* owner,
           const std::shared_ptr<RegistryLifetime>& lifetime);
    Registry& Owner() const;
    friend class Registry;
public:
    // 直接构造的实体没有所属注册表，只能作为无效句柄。
    explicit Entity(int id = -1) : id(id) {}
    Registry* registry = nullptr; // 保留旧调用方式；普通业务代码优先使用实体成员函数。
    int GetId() const { return id; }
    std::uint64_t GetGeneration() const { return generation; }
    bool IsValid() const;
    bool IsPendingRemoval() const;
    void Kill() const;
    void Tag(const std::string& tag) const;
    bool HasTag(const std::string& tag) const;
    void Group(const std::string& group) const;
    bool BelongsToGroup(const std::string& group) const;
```

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:167)）：

```cpp
struct Slot {
    std::uint64_t generation = 0;
    bool alive = false;
    bool committed = false;
    Signature signature;
};
```

把它想成：`slots[7]`是第7个可复用位置；`Entity(id=7,generation=3,ownerIdentity=2)`是“第2个世界中第7槽第3次生命”的身份卡。位置可复用，身份不能混淆。

| 字段 | 放在哪里 | 具体用途 |
|---|---|---|
| id | Entity | 作为slots及池稀疏表的索引 |
| generation | Entity和Slot | 比较句柄对应哪一代对象 |
| ownerIdentity | Entity | 区分不同Registry分配的同号实体 |
| lifetime | Entity中的weak_ptr | 使用Registry前确认世界仍存在 |
| registry | Entity中的裸指针 | 通过检查后调用所属Registry |
| alive | Slot | 该槽位当前是否有对象 |
| committed | Slot | 是否已加入系统成员体系 |
| signature | Slot | 该实体当前有哪些组件 |

### 3.2 CreateEntity完整实现及一次调用的结果

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:73)）：

```cpp
Entity Registry::CreateEntity() {
    int id;
    if (freeIds.empty()) {
        if (slots.size() >= static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            throw std::length_error("实体 ID 空间已耗尽");
        id = static_cast<int>(slots.size());
        slots.emplace_back();
    } else { id = freeIds.front(); freeIds.pop_front(); }
    auto& slot = slots[id];
    slot.alive = true; slot.committed = false; slot.signature.reset();
    const auto entity = Handle(id);
    dirtyEntities.insert(entity);
    return entity;
}
```

第一次创建时freeIds为空，于是在slots尾部添加一个Slot，id就是新增前的长度。被销毁槽位会进入freeIds，后续创建则从队列头复用。

取到slot后把alive设true、committed设false并清签名。Handle(id)把当前代数、Registry指针和token信息装成Entity。dirtyEntities记录“这个实体需要重新匹配系统”，最后返回身份卡。

假设此前没有实体：调用后`enemy.id=0`、`slots[0].alive=true`、`signature=全0`、`committed=false`。这时没有任何业务组件；系统成员要等Update。CreateEntity并不分配一个包含位置/图片的Enemy大对象。

### 3.3 调用enemy.GetComponent，实际转发到哪里

Entity自己的模板方法非常短：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:335)）：

```cpp
template<typename T> T& Entity::GetComponent() const { return Owner().GetComponent<T>(*this); }
```

`*this`把整张身份卡交给Registry，T指定组件类型。Owner先保证目标Registry可访问：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:15)）：

```cpp
Registry& Entity::Owner() const {
    const auto token = lifetime.lock();
    if (!token || !token->owner || token->owner != registry || !registry->IsValid(*this))
        throw std::invalid_argument("实体句柄无效或所属注册表已销毁");
    return *registry;
}
```

`lifetime.lock()`得到标记；无标记、owner为空、指针不一致或身份无效，都会抛错。只有前面的短路检查通过后，才会访问registry。Registry本身再检查槽位和代数：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:58)）：

```cpp
bool Registry::IsValid(Entity entity) const {
    return entity.registry == this && entity.ownerIdentity == lifetime->identity && entity.GetId() >= 0
        && static_cast<std::size_t>(entity.GetId()) < slots.size() && slots[entity.GetId()].alive
        && slots[entity.GetId()].generation == entity.GetGeneration();
}
```

这段条件从左到右验证世界、ID范围、alive和generation。不是比较指针就算有效，也不是只检查id没有越界。

### 3.4 RegistryLifetime是怎样建立和失效的

Registry构造时创建一个小的共享标记，内容为`{this,nextIdentity.fetch_add(1)}`。Entity保存标记的weak_ptr，不延长Registry生命。Registry析构把标记owner设nullptr。

例如外部保护区仍持有token的shared_ptr，token尚未释放，但Registry已经析构。此时weak_ptr仍可能lock成功，owner为空才能阻止访问。因此“token存在”和“世界存在”是两层检查。

### 3.5 Kill并不立即释放，提交才完成销毁

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:87)）：

```cpp
void Registry::KillEntity(Entity entity) {
    if (IsValid(entity)) entitiesToBeKilled.insert(entity);
}
```

KillEntity只把有效Entity放入待删除set，重复Kill自动去重。真正清理发生在DestroyEntity，关键尾部如下：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:118)）：

```cpp
    for (auto& pool : componentPools) if (pool) pool->RemoveEntityFromPool(id);
    auto& slot = slots[id];
    slot.signature.reset(); slot.alive = false; slot.committed = false;
    dirtyEntities.erase(entity);
    // 版本号达到上限后永久停用该槽位，避免回绕使旧句柄重新有效。
    if (slot.generation != (std::numeric_limits<std::uint64_t>::max)()) {
        ++slot.generation;
        freeIds.push_back(id);
    }
}
```

在这段之前，代码已移出所有System并删除标签、分组；这段移除各池组件，清空Slot并递增代数，然后才允许ID复用。代数达到uint64上限就不再回收这个槽位，防止回绕。

### 3.6 跟踪一次复用

```text
A：world=2，id=7，generation=3
Kill(A)：A仍有效，pending=true，旧组件暂时可读
Update：组件移除，slots[7].alive=false，generation=4，7入freeIds
创建B：获得id=7、generation=4
旧A再次Get：Registry比较3与4不相等，拒绝
```

这就是三个字段实际怎样协作。好处是Lua、编辑选择和延迟命令可以长期保存身份卡而不误指新对象；代价是句柄较大，每次安全访问都需要检查。公开registry裸指针仍是兼容入口，绕过成员函数直接解引用不会获得同样保护。

<a id="chapter-4"></a>

## 4. Pool怎样用三条数组存放、查找和删除组件

### 4.1 先看池内部真正的成员

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:114)）：

```cpp
template<typename T> class Pool : public IPool {
    static constexpr std::size_t Missing = (std::numeric_limits<std::size_t>::max)();
    std::vector<T> data;
    std::vector<int> denseEntities;
    std::vector<std::size_t> sparse;
```

以Pool<TransformComponent>为例，data存真正的Transform，denseEntities存每个位置属于哪个实体，sparse用实体id查data下标。Missing是一个不可能作为合法下标的最大size_t值，表示没有组件。

```text
data：           [A的位置, B的位置, C的位置]
denseEntities：  [8,       2,       11]
sparse：sparse[8]=0，sparse[2]=1，sparse[11]=2
```

对于id=11，先取sparse[11]=2，再取data[2]。denseEntities反过来告诉你data[2]属于11，主要在删除补位时使用。

### 4.2 添加/覆盖代码

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:127)）：

```cpp
void Set(int id, T object) {
    if (id < 0) throw std::invalid_argument("组件池不接受负数实体 ID");
    if (Contains(id)) { data[sparse[id]] = std::move(object); return; }
    if (static_cast<std::size_t>(id) >= sparse.size()) sparse.resize(static_cast<std::size_t>(id) + 1, Missing);
    data.push_back(std::move(object));
    try { denseEntities.push_back(id); }
    catch (...) { data.pop_back(); throw; }
    sparse[id] = data.size() - 1;
}
```

逐段读：负ID拒绝；已有组件则直接移动赋值覆盖并返回；新增时保证sparse足够长，空位置填Missing；data.push_back构造真实组件；denseEntities加入实体id；最后写sparse[id]发布位置。

第二个push若失败会撤回data，避免两个稠密数组长度不同。构造函数用reserve预留，而不是resize默认构造100个T，所以data.size就是真实组件数，也能支持没有默认构造函数的组件。

### 4.3 查找代码非常短，但经过两层含义

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:124)）：

```cpp
bool Contains(int id) const {
    return id >= 0 && static_cast<std::size_t>(id) < sparse.size() && sparse[id] != Missing;
}
```

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:150)）：

```cpp
T* TryGet(int id) { return Contains(id) ? &data[sparse[id]] : nullptr; }
```

Contains只验证池中有这个槽位的数据，不验证Entity的world/generation；完整身份由Registry入口负责。身份验证和存储定位是两个模块的分工。

TryGet返回指针便于表达缺失；Get在TryGet为空时抛out_of_range，否则返回引用。查询不会像旧版unordered_map的operator[]那样偷偷插入默认索引0。

### 4.4 删除代码和数组变化

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:136)）：

```cpp
bool Remove(int id) {
    if (!Contains(id)) return false;
    const auto index = sparse[id], last = data.size() - 1;
    if (index != last) {
        data[index] = std::move(data[last]);
        const int movedEntity = denseEntities[last];
        denseEntities[index] = movedEntity;
        sparse[movedEntity] = index;
    }
    data.pop_back(); denseEntities.pop_back(); sparse[id] = Missing;
    return true;
}
```

删除id=2：index=1、last=2；把C移动到data[1]，denseEntities[1]=11，修正sparse[11]=1；pop尾部；sparse[2]=Missing。最终只有[A,C]，不移动全部后续元素。

两个重要结果：数组仍紧凑，但不保持实体顺序；尾部对象真正析构，持有的Lua引用等会释放，不只是逻辑数量减一。

### 4.5 整个池应该始终满足什么

`data.size()==denseEntities.size()`；每个有效位置i都满足`sparse[denseEntities[i]]==i`；不存在组件的实体为Missing。Clear同时清三数组。插入通常摊销O(1)，但扩sparse需要初始化新范围，移动/析构组件的成本也另计。

vector扩容会让旧引用失效；补位即使地址不变，也可能换成另一个实体的数据。因此Pool本身不保证组件长期稳定地址，要靠第6章的阶段保护，并在跨阶段时保存Entity而不是T*。

这里是每类组件的连续AoS，不是把Transform所有字段再拆成单独数组的完整SoA。系统遍历次序与不同池次序也不必相同，不能只因使用vector就宣称所有访问都顺序。

<a id="chapter-5"></a>

## 5. 从组件类型到系统成员：AddComponent整条链

### 5.1 组件类型编号是这样产生的

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:20)）：

```cpp
constexpr unsigned int MAX_COMPONENTS = 32;
using Signature = std::bitset<MAX_COMPONENTS>;

struct IComponent {
protected:
    static std::atomic<unsigned int> nextId;
    static unsigned int AllocateId();
};
template <typename T> class Component : public IComponent {
public:
    static unsigned int GetId() {
        static const unsigned int id = AllocateId();
        return id;
    }
};
```

Component<Transform>和Component<Sprite>是不同模板实例，各有一个函数局部static id。第一次执行GetId时AllocateId从共享原子计数器取号，以后返回相同号。最多32类，超过上限抛length_error。

假设Transform=0、RigidBody=1、Sprite=2：componentPools[0]保存Pool<Transform>，签名第0位表示实体是否有Transform。这个编号不是实体ID，也不保证不同程序运行中固定顺序。

### 5.2 真正写进组件池的函数

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:253)）：

```cpp
template<typename T> void Registry::SetComponentNow(Entity entity, T value) {
    dirtyEntities.insert(entity);
    const auto componentId = Component<T>::GetId();
    if (componentId >= componentPools.size()) componentPools.resize(componentId + 1);
    if (!componentPools[componentId]) componentPools[componentId] = std::make_unique<Pool<T>>();
    auto& pool = static_cast<Pool<T>&>(*componentPools[componentId]);
    pool.Set(entity.GetId(), std::move(value));
    slots[entity.GetId()].signature.set(componentId);
}
```

它先标脏；取得类型编号；必要时扩componentPools并创建Pool<T>；把IPool还原成Pool<T>；将组件移动写入以实体ID索引的池；最后置签名位。

因此一次添加会同时影响三处：**池中的数据、Slot.signature、dirtyEntities**。如果只把数据放进池却不改签名，系统查询就不知道它存在；如果只改签名却没有数据，Get会失败。

### 5.3 Registry查询如何定位组件

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:292)）：

```cpp
template<typename T> const T* Registry::TryGetComponent(Entity entity) const {
    if (!HasComponent<T>(entity)) return nullptr;
    return static_cast<const Pool<T>&>(*componentPools[Component<T>::GetId()]).TryGet(entity.GetId());
}
```

HasComponent先验证实体再看签名位；验证通过后按类型ID找池，再按实体id通过sparse找组件。GetComponent只是增加“缺失要抛异常”的接口语义。

完整路径现在可以自己走通：`enemy.GetComponent<Transform>() → Owner → Registry::Get/TryGet → componentPools[typeId] → Pool::TryGet(enemy.id) → data[sparse[id]]`。

### 5.4 System内部保存什么，具体系统怎样声明需求

教学摘写，省略方法实现：

```cpp
class System {
    Signature componentSignature;
    std::vector<Entity> entities;
    std::unordered_map<Entity,std::size_t,EntityHash> indices;
    bool signatureFrozen = false;
};
```

MovementSystem构造函数依次RequireComponent<Transform>()和RequireComponent<RigidBody>()。RequireComponent取得相应类型编号并把componentSignature对应位置1，于是移动要求变成一个固定bitset。

Registry对一个实体重新匹配的源码如下：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:90)）：

```cpp
void Registry::AddEntityToSystems(Entity entity) {
    if (deferDepth) throw std::logic_error("不能在遍历期间修改系统成员");
    RequireAlive(entity);
    const auto& signature = slots[entity.GetId()].signature;
    for (auto& item : systems) {
        const auto& required = item.second->GetComponentSignature();
        if (!IsPendingRemoval(entity) && (signature & required) == required) item.second->AddEntityToSystem(entity);
        else item.second->RemoveEntityFromSystem(entity);
    }
}
```

例如实体签名111、移动需求011，按位与为011，等于需求，因此加入；只有Transform+Sprite的101与011得到001，不加入。额外组件不影响已满足的要求。

### 5.5 系统成员如何加入和移除

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:32)）：

```cpp
void System::AddEntityToSystem(Entity entity) {
    if (Contains(entity)) return;
    entities.push_back(entity);
    try { indices.emplace(entity, entities.size() - 1); }
    catch (...) { entities.pop_back(); throw; }
}
```

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:38)）：

```cpp
void System::RemoveEntityFromSystem(Entity entity) {
    const auto found = indices.find(entity);
    if (found == indices.end()) return;
    const auto index = found->second;
    if (index != entities.size() - 1) {
        entities[index] = entities.back();
        indices.at(entities[index]) = index;
    }
    entities.pop_back();
    indices.erase(found);
}
```

vector用于遍历，indices用完整句柄找到位置。加入先去重；移除同样尾部补位并修正索引，所以成员删除平均常数定位，不保持顺序。GetSystemEntities返回const vector&，避免复制整个列表。

### 5.6 注册系统和运行中增删组件怎样连接

AddSystem构造系统、冻结需求签名并放入type_index到系统的表，然后匹配已经committed的实体。新增实体与改组件会标dirty，下一Update统一重匹配。注册后不允许再RequireComponent，否则条件变了但缓存成员没跟着重建。

保护区外AddComponent会立即改变组件数据，成员仍要等Update；保护区内连数据写入也延迟。这个区别在编辑器“刚点添加，为什么本帧尚未出现新控件”中很重要。

<a id="chapter-6"></a>

## 6. Defer怎样保护遍历，Update怎样提交命令

### 6.1 先看使用者写的代码

```cpp
{ // 教学示例
    auto guard = registry.Defer();
    for (auto e : movement.GetSystemEntities()) {
        e.GetComponent<TransformComponent>().position.x += 1;
        e.RemoveComponent<RigidBodyComponent>();
    }
}
registry.Update();
```

position修改是普通数值写入，马上可见；Remove会改变组件存储和系统成员，在guard存在时只排队。这样本次遍历仍可读取原RigidBody，不会中途补位/扩容破坏引用。

### 6.2 guard本身其实就是一段计数与RAII代码

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:196)）：

```cpp
class DeferredChanges {
    std::shared_ptr<RegistryLifetime> lifetime;
    friend class Registry;
    explicit DeferredChanges(Registry& registry) : lifetime(registry.lifetime) { ++registry.deferDepth; }
public:
    DeferredChanges(const DeferredChanges&) = delete;
    DeferredChanges& operator=(const DeferredChanges&) = delete;
    DeferredChanges(DeferredChanges&& other) noexcept : lifetime(std::move(other.lifetime)) {}
    ~DeferredChanges() { if (lifetime && lifetime->owner) --lifetime->owner->deferDepth; }
};
```

构造时deferDepth++，析构时检查世界仍活着再减。移动构造转移token，禁止复制防止同一个保护被重复减。嵌套时深度可能为2，内层退出变1，外层退出才变0。

析构没有Update调用，保护结束不代表新组件已经存在；它只表示允许之后在安全点提交。异常离开作用域也会析构并解除保护。

### 6.3 AddComponent如何在立即执行与排队之间切换

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h:262)）：

```cpp
template<typename T, typename... Args> void Registry::AddComponent(Entity entity, Args&&... args) {
    RequireWritable(entity);
    Component<T>::GetId(); // 排队前先检查组件类型数量上限。
    if (deferDepth) {
        auto value = std::make_shared<T>(std::forward<Args>(args)...);
        commands.emplace_back([this, entity, value] {
            if (IsValid(entity) && !IsPendingRemoval(entity)) SetComponentNow(entity, std::move(*value));
        });
    } else SetComponentNow(entity, T(std::forward<Args>(args)...));
}
```

RequireWritable检查实体有效且未待销毁；先注册类型检查上限；deferDepth非0时构造一个暂存T，用shared_ptr捕获到lambda，lambda稍后再检查完整Entity后调用SetComponentNow。否则直接构造T并写池。

暂存值要活到提交，所以不能捕获函数栈上的局部引用。使用shared_ptr的另一个具体原因是C++17 std::function要求目标可复制，包装后lambda可复制，T仍可以move-only。它不是说组件池整体改成共享所有权。

RemoveComponent也做同样分支，区别是命令只需保存Entity，执行时调用RemoveComponentNow<T>移池数据、清签名、标dirty。

### 6.4 Update完整源码：三阶段而不是一团操作

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:128)）：

```cpp
void Registry::Update() {
    if (deferDepth || updating) throw std::logic_error("不能在遍历期间或递归调用时提交结构变更");
    updating = true;
    struct ResetFlag { bool& flag; ~ResetFlag() { flag = false; } } reset{updating};
    // 销毁优先；指向已销毁版本的排队命令会自动跳过。
    while (!entitiesToBeKilled.empty()) {
        const auto entity = *entitiesToBeKilled.begin();
        DestroyEntity(entity);
        entitiesToBeKilled.erase(entity);
    }
    while (!commands.empty()) {
        auto command = std::move(commands.front());
        commands.pop_front();
        command();
    }
    while (!dirtyEntities.empty()) {
        const auto entity = *dirtyEntities.begin();
        if (IsValid(entity)) {
            AddEntityToSystems(entity);
            slots[entity.GetId()].committed = true;
        }
        dirtyEntities.erase(entity);
    }
}
```

按顺序读：检查不能在保护区或递归提交；RAII标记保证退出时updating恢复false；处理全部待销毁对象；逐条从commands头部取出并执行；最后重匹配dirty实体并置committed。

为什么销毁先：同一实体先Add再Kill，先销毁会使捕获的旧身份失效，后面的Add自动跳过；它不会落到复用该槽位的新实体上。

为什么最后匹配：一批命令可能先加又删同一组件，只需按最终签名同步系统。dirty集合自动去重，避免无谓重复。

### 6.5 一次排队的可观察过程

```text
初始：e有Transform，无RigidBody，不在Movement
进入Defer，Add<RigidBody>(velocity=60)
此时：命令有了，但Has<RigidBody>=false
退出Defer：仍未提交
Update：执行SetComponentNow → 数据进池，签名置位，实体标dirty
        重匹配 → e进入Movement
下一次Movement.Update → 按速度移动
```

当前命令队列是deque<std::function<void()>>，有序提交但没有整体回滚。命令执行前会从队列移出；如果中途抛异常，已执行修改保留，后续命令仍留队列，dirty同步可能还没完成。不能把Defer叫作数据库事务或互斥锁。

<a id="chapter-7"></a>

## 7. 标签和分组怎样维护双向索引

### 7.1 数据成员先摆出来

```cpp
// 源码成员摘写
std::unordered_map<std::string,Entity> entityPerTag;
std::unordered_map<int,std::string> tagPerEntity;
std::unordered_map<std::string,std::set<Entity>> entitiesPerGroup;
std::unordered_map<int,std::string> groupPerEntity;
```

标签把名字映射到唯一实体，反向表方便按实体删除或改名；分组把一个名字映射到多个实体，反向表方便换组。当前每个实体最多一个tag和一个group，不是任意多标签集合。

### 7.2 TagEntity怎样处理改名与冲突

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp:165)）：

```cpp
void Registry::TagEntity(Entity entity, const std::string& tag) {
    RequireWritable(entity);
    if (tag.empty()) throw std::invalid_argument("标签不能为空");
    if (deferDepth) {
        commands.emplace_back([this, entity, tag] { if (IsValid(entity) && !IsPendingRemoval(entity)) TagEntity(entity, tag); });
        return;
    }
    const auto existing = entityPerTag.find(tag);
    if (existing != entityPerTag.end() && existing->second != entity) throw std::invalid_argument("标签已被其他实体占用");
    const auto previous = tagPerEntity.find(entity.GetId());
    if (previous != tagPerEntity.end() && previous->second == tag) return;
    if (previous != tagPerEntity.end()) entityPerTag.erase(previous->second);
    entityPerTag.insert_or_assign(tag, entity);
    tagPerEntity.insert_or_assign(entity.GetId(), tag);
}
```

先检查有效和非空；保护区内排命令；立即执行时先查新tag是否被其他实体占用；相同tag重复设置直接返回；否则移除旧正向关联、写新正向和反向表。

具体例子：e从player改hero，删除entityPerTag["player"]，写entityPerTag["hero"]=e，并把tagPerEntity[e.id]改hero。若别人已占hero，提前抛错，不先删player。

### 7.3 分组与查询的实际行为

GroupEntity先查已有组，相同值不重复加入，否则RemoveEntityGroup清旧成员，然后把e放入entitiesPerGroup[new]并改反向表。旧组为空时删除空集合。销毁直接清两类索引后才复用槽位。

TryGetEntityByTag返回optional，未知时为空；GetEntityByTag未找到抛异常。GetEntitiesByGroup未知时返回空vector。GetEntities扫描槽位，只收alive且非pending；GetEntityCount数alive，因此待销毁阶段可能与可操作对象列表数量不同。

这套索引的作用是查询与清理，不代替组件签名；例如属于enemies并不自动意味着有Health，伤害回调还要检查组件。


<a id="chapter-8"></a>

## 8. 固定步长和模拟时钟的代码怎样配合

### 8.1 两个类分别存什么

FixedStepClock保存“真实时间还有多少没转成模拟步”，SimulationClock保存“游戏已经前进了多少秒”。它们不是两个重复计时器。

SimulationClock的完整类如下：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Time/SimulationClock.h:1)）：

```cpp
#pragma once
#include <cmath>
#include <cstdint>
#include <stdexcept>

// 当前单线程游戏的模拟时钟；窗口与输入仍使用真实时间，游戏行为使用此时钟。
class SimulationClock {
    inline static double seconds = 0;
public:
    static double Seconds() { return seconds; }
    static std::uint32_t Milliseconds() { return static_cast<std::uint32_t>(seconds * 1000.0); }
    static void Set(double value) {
        if (!std::isfinite(value) || value < 0 || value > 4000000)
            throw std::invalid_argument("模拟时间超出有效范围");
        seconds = value;
    }
    static void Advance(double delta) { Set(seconds + delta); }
};
```

seconds是inline static，当前游戏共用一个累计值。Seconds返回秒，Milliseconds把秒乘1000转换成毫秒；Set校验非负、有限和范围；Advance只是Set(seconds+delta)。动画、子弹出生和发射时间都从这里读取。

### 8.2 固定步时钟怎样消费真实时间

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:177)）：

```cpp
class FixedStepClock {
    double accumulator = 0;
public:
    static constexpr double Step = 1.0 / 120.0;
    static constexpr int MaxSteps = 8;
    template<class F> int Advance(double frameTime, F&& step) {
        accumulator += (std::max)(0.0, (std::min)(frameTime, 0.25));
        int count = 0;
        while (accumulator + 1e-12 >= Step && count < MaxSteps) {
            step(static_cast<float>(Step));
            accumulator -= Step;
            ++count;
        }
        if (accumulator >= Step) accumulator = std::fmod(accumulator, Step);
        return count;
    }
};
```

上面的源码节选取自类定义；阅读时把step当作Game传入的回调，它的内容就是SimulateStep(h)。accumulator最初为0，收到一帧真实耗时就加上，够1/120秒便执行一次回调并扣去这段时间。

例如一帧经过1/60秒，可执行约2个物理步；1/144秒不足一步，先积累，下一帧再消费。循环最多8次；剩下还有整步时用fmod丢掉整步积压，只保留余数。0.25秒是输入帧耗时上限，不是每帧可以补0.25秒；8步最多约66.67毫秒。

### 8.3 暂停和单步的控制代码

SceneEditor保存两个bool：paused与stepRequested；相关方法原理是下面的教学摘写：

```cpp
void RequestStep() { paused=true; stepRequested=true; }
bool ConsumeStep() {
    bool result=stepRequested;
    stepRequested=false;
    return result;
}
```

Game::Update暂停分支每帧把physicsClock重置为空累加器，如果ConsumeStep为true就执行一次SimulateStep。因为ConsumeStep清标志，下帧不会继续自动跑。运行分支则调用Advance(deltaTime,回调)。

```text
暂停5秒：真实时间增加，Update和Render继续，但模拟seconds不变
点击单步：seconds增加1/120，脚本/物理/动画/发射/寿命各跑一次
下一帧：仍paused，无新请求，不再推进
恢复：按后续真实帧耗时正常前进，不补算5秒暂停
```

### 8.4 为什么动画和子弹也必须改时间来源

若只停止Movement，而动画用SDL_GetTicks，暂停时动画仍走；若子弹寿命用真实时间，恢复时会突然过期。当前这些组件的startTime和系统读取都改用SimulationClock，保持同一参考系。

模拟步在SimulateStep开头推进，因此Lua得到本步推进后的累计毫秒。当前没有渲染插值；静态单时钟也限制多个独立世界并行计时。Milliseconds用uint32，而一些组件用int，还应考虑很长运行时长的类型范围；这不影响理解当前日常流程，但属于准确的能力边界。

<a id="chapter-9"></a>

## 9. Collider如何转换成参与检测的世界形状

### 9.1 先看碰撞组件与临时几何结构

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Components/ColliderComponent.h:1)）：

```cpp
#pragma once
#include <glm/glm.hpp>
#include <cstdint>

enum class ColliderShape { Box, Circle };

// 每个实体只有一个碰撞体；尺寸和偏移使用未经缩放的局部像素坐标。
struct ColliderComponent {
    ColliderShape shape = ColliderShape::Box;
    float width = 0, height = 0, radius = 0;
    glm::vec2 offset{0}; // 局部包围矩形的左上角偏移；圆形也遵循此约定。
    bool isTrigger = false;
    std::uint32_t category = 1;
    std::uint32_t mask = 0xffffffffu;
    ColliderComponent(float width = 0, float height = 0, glm::vec2 offset = glm::vec2(0))
        : width(width), height(height), offset(offset) {}
    static ColliderComponent Circle(float radius, glm::vec2 offset = glm::vec2(0)) {
        ColliderComponent result(2 * radius, 2 * radius, offset);
        result.shape = ColliderShape::Circle;
        result.radius = radius;
        return result;
    }
};
```

shape决定矩形或圆；width/height是矩形尺寸，radius是圆半径；offset是局部偏移；trigger决定是否只产生事件；category和mask用于选择允许接触的类别。Circle静态工厂构造直径尺寸并切换类型，方便外部创建。

算法不直接拿Entity到处查询，而先构建WorldShape：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:13)）：

```cpp
struct AABB { glm::vec2 min{0}, max{0}; };
struct WorldShape {
    ColliderShape type = ColliderShape::Box;
    glm::vec2 center{0}, half{0}, xAxis{1, 0}, yAxis{0, 1};
    float radius = 0;
    AABB bounds;
    bool valid = false;
};
struct Contact {
    glm::vec2 normal{1, 0}; // 法线由 A 指向 B；修正穿透时，A 沿法线反方向移动。
    float penetration = 0;
};
```

center是世界中心；half是矩形半长；xAxis/yAxis是旋转后的单位方向；bounds是外接AABB；valid标记尺寸是否有效。Contact保存碰撞输出：法线和穿透深度。

### 9.2 ShapeOf把实体组件拼起来

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/EntityShape.h:7)）：

```cpp
inline WorldShape ShapeOf(Entity entity) {
    const auto& collider = entity.GetComponent<ColliderComponent>();
    auto pivot = LocalCenter(collider);
    if (entity.HasComponent<SpriteComponent>()) {
        const auto& sprite = entity.GetComponent<SpriteComponent>();
        pivot = glm::vec2(sprite.width, sprite.height) * 0.5f;
    }
    return MakeShape(entity.GetComponent<TransformComponent>(), collider, pivot);
}
```

先取Collider，默认pivot是碰撞体局部中心；有Sprite时改为图片中心；再把Transform、Collider和pivot交给MakeShape。这样物理围绕与SDL相同的图片中心旋转。

### 9.3 MakeShape逐步计算坐标

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:30)）：

```cpp
inline WorldShape MakeShape(const TransformComponent& t, const ColliderComponent& c, glm::vec2 pivot) {
    WorldShape s;
    s.type = c.shape;
    const float angle = static_cast<float>(t.rotation * 3.14159265358979323846 / 180.0);
    s.xAxis = {std::cos(angle), std::sin(angle)};
    s.yAxis = {-s.xAxis.y, s.xAxis.x};
    s.center = t.position + pivot * t.scale + Rotate((LocalCenter(c) - pivot) * t.scale, s.xAxis, s.yAxis);
    s.half = glm::abs(glm::vec2(c.width, c.height) * t.scale) * 0.5f;
    // 圆形非均匀缩放时使用保守的外包圆，不按椭圆处理。
    s.radius = c.radius * (std::max)(std::abs(t.scale.x), std::abs(t.scale.y));
    s.valid = c.shape == ColliderShape::Circle ? c.radius > 0 && s.radius > Epsilon
        : c.width > 0 && c.height > 0 && s.half.x > Epsilon && s.half.y > Epsilon;
    const glm::vec2 extent = c.shape == ColliderShape::Circle ? glm::vec2(s.radius)
        : glm::abs(s.xAxis) * s.half.x + glm::abs(s.yAxis) * s.half.y;
    s.bounds = {s.center - extent, s.center + extent};
    return s;
}
```

代码先把度转弧度，算出两个方向轴；接着计算世界中心：`position+pivot*scale+Rotate((localCenter-pivot)*scale)`。步骤是“减局部旋转中心 → 缩放相对向量 → 旋转 → 加上旋转中心的世界位置”，不能把position直接当中心。

矩形localCenter=offset+(width/2,height/2)，圆=offset+(radius,radius)。矩形half取缩放后尺寸绝对值的一半。旋转矩形外接AABB的半范围为`abs(xAxis)*half.x+abs(yAxis)*half.y`，再用center±extent得到bounds。

### 9.4 数字算一次就能看懂

position=(100,200)，scale=(2,3)，rotation=90°，pivot=(10,10)，矩形尺寸=(10,4)，offset=(2,1)。局部中心=(7,3)，相对pivot=(-3,-7)，缩放=(-6,-21)，旋转后=(21,-6)；pivot的世界位置=(120,230)，加起来center=(141,224)。

同一WorldShape用于检测和调试绘制，避免两边独立写缩放/偏移公式。圆非均匀缩放用较大轴系数扩大半径，表示保守圆而非椭圆；负缩放几何可算，但编辑器限制正缩放，图片渲染也没有完整负尺寸支持。

<a id="chapter-10"></a>

## 10. 从形状数组到接触列表：宽阶段和窄阶段代码

### 10.1 CollisionSystem先建立平行数组

它取得系统实体列表，给每个实体生成WorldShape并push进shapes。两条数组相同位置一一对应：`entities[i]`的形状就是`shapes[i]`。待销毁实体放一个valid=false的空形状，保留位置对应但不参与检测。

候选对返回的是两个数组下标，不是Entity ID。拿到pair.first后先回entities取身份，再读Collider做过滤，这个区别很容易在口述时说错。

### 10.2 Candidates完整实现

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:119)）：

```cpp
inline std::vector<std::pair<std::size_t, std::size_t>> Candidates(const std::vector<WorldShape>& shapes) {
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < shapes.size(); ++i) if (shapes[i].valid) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return shapes[a].bounds.min.x == shapes[b].bounds.min.x ? a < b : shapes[a].bounds.min.x < shapes[b].bounds.min.x;
    });
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto a = order[i];
        for (std::size_t j = i + 1; j < order.size(); ++j) {
            const auto b = order[j];
            if (shapes[b].bounds.min.x > shapes[a].bounds.max.x) break;
            if (Overlap(shapes[a].bounds, shapes[b].bounds)) pairs.emplace_back(a, b);
        }
    }
    return pairs;
}
```

先把有效形状下标放入order；按bounds.min.x排序，值相同用下标作次级顺序；双循环扫描右侧对象，遇到下一个min.x超过当前max.x就break；X未分离再做完整AABB重叠，命中就记录一对下标。

例如X区间A=[0,10]、B=[8,15]、C=[30,40]：对A先查B，再看到C.min=30>10直接结束；A不会与之后所有更右对象比较。它减少稀疏场景工作，但所有X区间重叠时仍可能O(N²)。

### 10.3 接触过滤与按类型分派

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:57)）：

```cpp
inline bool ShouldCollide(const ColliderComponent& a, const ColliderComponent& b) {
    return (a.category & b.mask) != 0 && (b.category & a.mask) != 0;
}
```

例如A.category=1、mask=2，B.category=2、mask=1，两边位与都非0才允许。isTrigger不影响能否检测，只影响后面的求解。

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:109)）：

```cpp
inline bool Collide(const WorldShape& a, const WorldShape& b, Contact& contact) {
    if (!a.valid || !b.valid || !Overlap(a.bounds, b.bounds)) return false;
    if (a.type == ColliderShape::Box && b.type == ColliderShape::Box) return BoxBox(a, b, contact);
    if (a.type == ColliderShape::Circle && b.type == ColliderShape::Circle) return CircleCircle(a, b, contact);
    if (a.type == ColliderShape::Box) return BoxCircle(a, b, contact);
    const bool hit = BoxCircle(b, a, contact);
    contact.normal = -contact.normal;
    return hit;
}
```

Collide先排无效形状和AABB分离，再选择BoxBox、CircleCircle、BoxCircle。最后一个分支是A为圆B为盒，内部复用BoxCircle(B,A)，结束后翻转法线，保持外部A→B约定。

### 10.4 BoxBox的SAT具体怎样写

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:60)）：

```cpp
inline bool BoxBox(const WorldShape& a, const WorldShape& b, Contact& contact) {
    const auto delta = b.center - a.center;
    contact.penetration = (std::numeric_limits<float>::max)();
    for (auto axis : {a.xAxis, a.yAxis, b.xAxis, b.yAxis}) {
        const float ra = a.half.x * std::abs(glm::dot(a.xAxis, axis)) + a.half.y * std::abs(glm::dot(a.yAxis, axis));
        const float rb = b.half.x * std::abs(glm::dot(b.xAxis, axis)) + b.half.y * std::abs(glm::dot(b.yAxis, axis));
        const float distance = glm::dot(delta, axis);
        const float depth = ra + rb - std::abs(distance);
        if (depth < 0) return false;
        if (depth < contact.penetration) {
            contact.penetration = depth;
            contact.normal = distance < 0 ? -axis : axis;
        }
    }
    return true;
}
```

for遍历A和B各自的x/y轴，共4条。ra/rb是两个矩形投影到当前轴的半长；distance是中心差投影；depth=ra+rb-abs(distance)。负值表示分离，立刻false；否则保留最小depth及对应法线。

投影半长公式中的dot表示“某条矩形轴在检测轴方向上的比例”，乘half就是该方向长度贡献，两个轴贡献相加。不需要先逐个算4个顶点的投影再求min/max，两者对矩形可以得到等价范围。

若A投影[0,10]、B投影[4,6]，交集长2但推出需要6；当前中心半径depth能处理包含情况。相切depth=0也保留为接触。

### 10.5 圆圆与圆盒的实现分支

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:76)）：

```cpp
inline bool CircleCircle(const WorldShape& a, const WorldShape& b, Contact& contact) {
    const auto delta = b.center - a.center;
    const float distance2 = glm::dot(delta, delta), radius = a.radius + b.radius;
    if (distance2 > radius * radius) return false;
    const float distance = std::sqrt(distance2);
    contact.normal = distance > Epsilon ? delta / distance : glm::vec2(1, 0);
    contact.penetration = radius - distance;
    return true;
}
```

先平方距离排分离，命中后才sqrt；normal是归一化中心差，penetration是半径和减距离；中心重合时用(1,0)防除零。

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:85)）：

```cpp
inline bool BoxCircle(const WorldShape& box, const WorldShape& circle, Contact& contact) {
    const auto delta = circle.center - box.center;
    const glm::vec2 local(glm::dot(delta, box.xAxis), glm::dot(delta, box.yAxis));
    const auto nearest = glm::clamp(local, -box.half, box.half);
    const auto difference = local - nearest;
    const float distance2 = glm::dot(difference, difference);
    if (distance2 > circle.radius * circle.radius) return false;
    if (distance2 > Epsilon * Epsilon) {
        const float distance = std::sqrt(distance2);
        contact.normal = Rotate(difference / distance, box.xAxis, box.yAxis);
        contact.penetration = circle.radius - distance;
    } else {
        // 圆心位于矩形内部时，选择最近的出口边，避免产生零长度法线。
        const auto remaining = box.half - glm::abs(local);
        if (remaining.x < remaining.y) {
            contact.normal = box.xAxis * (local.x < 0 ? -1.0f : 1.0f);
            contact.penetration = circle.radius + remaining.x;
        } else {
            contact.normal = box.yAxis * (local.y < 0 ? -1.0f : 1.0f);
            contact.penetration = circle.radius + remaining.y;
        }
    }
    return true;
}
```

圆心先投影到盒的局部坐标，clamp取得盒上最近点。外部命中时用差向量求方向和深度；内部时差为0，比较到最近出口边的距离，选出口方向，深度加上圆半径。

到这里已经从实体组件得到一份Contact。它只有normal和penetration，没有接触点，因此后面只能实现当前的平移求解，不能凭这份数据完整处理转动。

<a id="chapter-11"></a>

## 11. 物理运动与响应：从组件到速度和位置变化

### 11.1 RigidBodyComponent的完整字段

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Components/RigidBodyComponent.h:1)）：

```cpp
#pragma once
#include <glm/glm.hpp>
enum class BodyType { Static, Kinematic, Dynamic };
// 只解算平移运动；旋转角度由变换组件或脚本控制。
struct RigidBodyComponent {
    glm::vec2 velocity{0};
    glm::vec2 force{0};
    BodyType type = BodyType::Dynamic;
    float mass = 1;
    float restitution = 0.5f;
    float friction = 0.4f;
    float gravityScale = 0; // 原有俯视关卡默认不受重力影响。
    float linearDamping = 0;
    explicit RigidBodyComponent(glm::vec2 velocity = glm::vec2(0)) : velocity(velocity) {}
    float InverseMass() const {
        return type == BodyType::Dynamic && mass > 0 ? 1.0f / mass : 0.0f;
    }
    void AddForce(glm::vec2 value) { force += value; }
    void ApplyImpulse(glm::vec2 value) { velocity += value * InverseMass(); }
};
```

velocity是每秒像素速度；force累计本步施加的力；type为Static/Kinematic/Dynamic；mass用于逆质量；restitution控制反弹；friction控制切向响应；gravityScale选择重力影响；linearDamping衰减速度。

InverseMass只给正质量Dynamic返回1/m，其他返回0。ApplyImpulse直接加`J*inverseMass`，AddForce只是先把力加起来，真正作用要等积分。

### 11.2 Movement怎样把力变成运动

Movement遍历Transform+RigidBody实体，跳过pending；取两个组件引用，调用Integrate，然后把force清零。Integrate源码如下：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:136)）：

```cpp
inline void Integrate(TransformComponent& t, RigidBodyComponent& b, float dt, glm::vec2 gravity) {
    if (b.type == BodyType::Static || (b.type == BodyType::Dynamic && b.InverseMass() == 0)) return;
    if (b.type == BodyType::Dynamic) {
        b.velocity += (b.force * b.InverseMass() + gravity * b.gravityScale) * dt;
        b.velocity /= 1.0f + (std::max)(0.0f, b.linearDamping) * dt;
    }
    t.position += b.velocity * dt;
}
```

Static或零逆质量Dynamic不动；正常Dynamic先`velocity+=(force/m+gravity*gravityScale)*dt`，然后除以`1+damping*dt`施加阻尼；最后统一用新velocity更新position。Kinematic跳过受力部分，但仍按设定速度移动。

例如m=2、F=(120,0)、dt=1/120，本步速度增0.5；冲量J=(120,0)则一次增60，不乘dt。先速度后位置就是半隐式欧拉。清force使持续力需要每步重新添加。

Movement后面还有玩家地图边界夹紧和非玩家越界Kill，属于现有玩法规则，不是通用物理公式。

### 11.3 法线冲量和摩擦的实际函数

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:147)）：

```cpp
inline void ResolveVelocity(RigidBodyComponent* a, RigidBodyComponent* b, const Contact& c) {
    const float ia = a ? a->InverseMass() : 0, ib = b ? b->InverseMass() : 0, sum = ia + ib;
    if (sum <= 0) return;
    auto relative = Velocity(b) - Velocity(a);
    const float speed = glm::dot(relative, c.normal);
    if (speed >= 0) return;
    const float restitution = (std::min)(a ? glm::clamp(a->restitution, 0.0f, 1.0f) : 1.0f,
                                        b ? glm::clamp(b->restitution, 0.0f, 1.0f) : 1.0f);
    // 抑制静止接触中的微小反弹；速度单位为像素/秒。
    const float impulse = -(1 + (speed < -1.0f ? restitution : 0.0f)) * speed / sum;
    if (a) a->velocity -= c.normal * (impulse * ia);
    if (b) b->velocity += c.normal * (impulse * ib);
    relative = Velocity(b) - Velocity(a);
    auto tangent = relative - c.normal * glm::dot(relative, c.normal);
    const float length = glm::length(tangent);
    if (length <= Epsilon) return;
    tangent /= length;
    const float friction = std::sqrt((std::max)(0.0f, a ? a->friction : 0.4f) * (std::max)(0.0f, b ? b->friction : 0.4f));
    const float jt = glm::clamp(-glm::dot(relative, tangent) / sum, -impulse * friction, impulse * friction);
    if (a) a->velocity -= tangent * (jt * ia);
    if (b) b->velocity += tangent * (jt * ib);
}
```

先读两边逆质量，和为0就都不可推动，返回；relative=vB-vA，speed=dot(relative,n)，speed>=0表示沿法线已在分离，不再冲量。

反弹系数取两者clamp后的较小值，低于1像素/秒的微小接近按无反弹处理。`impulse=-(1+e)*speed/(ia+ib)`是冲量标量，A减n*j*ia、B加n*j*ib。

随后重新计算relative，减去法向分量得到切线；若长度近0返回，否则归一化。friction取两材质几何平均，切向冲量限制在±friction*法向冲量，再按逆质量更新双方速度。

手算：等质量1，A速度10、B速度0、n=+x、e=1，relative=-10、j=10，最后A=0、B=10；e=0时j=5，双方都5。当前没有转动惯量和接触点力臂，分母只有平移逆质量。

### 11.4 位置修正代码

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h:169)）：

```cpp
inline void CorrectPositions(TransformComponent& a, TransformComponent& b,
                             const RigidBodyComponent* ba, const RigidBodyComponent* bb, const Contact& c) {
    const float ia = ba ? ba->InverseMass() : 0, ib = bb ? bb->InverseMass() : 0;
    if (ia + ib <= 0) return;
    const auto correction = c.normal * ((std::max)(c.penetration - 0.01f, 0.0f) * 0.8f / (ia + ib));
    a.position -= correction * ia;
    b.position += correction * ib;
}
```

穿透先减0.01容差，乘0.8分次消除；按逆质量分配，A沿-n、B沿+n。速度冲量只是避免继续接近，已经产生的重叠还需要这一步。容差和比例是当前像素尺度下的调参，不是任意单位系统都适合的常量。

### 11.5 CollisionSystem怎样组织六次求解和一次通知

下面给出系统完整Update，前面每个算法函数现在都有对应位置：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/CollisionSystem.h:14)）：

```cpp
void Update(std::unique_ptr<EventBus>& eventBus) {
    const auto& entities = GetSystemEntities();
    std::vector<Physics2D::WorldShape> shapes;
    for (auto entity : entities) {
        shapes.push_back(entity.registry->IsPendingRemoval(entity) ? Physics2D::WorldShape{} : Physics2D::ShapeOf(entity));
    }
    const auto candidates = Physics2D::Candidates(shapes);
    candidateCount = candidates.size();
    struct Pair { Entity a, b; Physics2D::Contact contact; bool trigger; };
    std::vector<Pair> contacts;
    for (const auto& pair : candidates) {
        auto a = entities[pair.first], b = entities[pair.second];
        const auto& ca = a.GetComponent<ColliderComponent>();
        const auto& cb = b.GetComponent<ColliderComponent>();
        Physics2D::Contact contact;
        if (Physics2D::ShouldCollide(ca, cb) && Physics2D::Collide(shapes[pair.first], shapes[pair.second], contact)) {
            contacts.push_back({a, b, contact, ca.isTrigger || cb.isTrigger});
        }
    }
    contactCount = contacts.size();
    // 使用顺序冲量法解算平移响应；触发器不参与物理响应求解。
    for (int iteration = 0; iteration < 6; ++iteration) {
        for (auto& pair : contacts) {
            if (pair.trigger) continue;
            auto* ba = Physics2D::BodyOf(pair.a);
            auto* bb = Physics2D::BodyOf(pair.b);
            Physics2D::ResolveVelocity(ba, bb, pair.contact);
            Physics2D::Contact current;
            if (Physics2D::Collide(Physics2D::ShapeOf(pair.a), Physics2D::ShapeOf(pair.b), current)) {
                Physics2D::CorrectPositions(pair.a.GetComponent<TransformComponent>(),
                    pair.b.GetComponent<TransformComponent>(), ba, bb, current);
            }
        }
    }
    // 求解后，每对接触在每个物理步只发送一次事件。Kill() 会延迟销毁实体，
    // 因此跳过待销毁实体，避免同一发子弹继续伤害多个目标。
    for (auto& pair : contacts) {
        if (pair.a.registry->IsPendingRemoval(pair.a) || pair.b.registry->IsPendingRemoval(pair.b)) continue;
        eventBus->EmitEvent<CollisionEvent>(pair.a, pair.b, pair.contact.normal, pair.contact.penetration, pair.trigger);
    }
}
```

前半建立shapes和候选，contacts保存Entity A/B、Contact和trigger；中间六轮只解非trigger，位置修正前会重算当前几何；最后才逐接触发事件，发布前检查双方pending。

六轮不是时间前进六次，也不发六次伤害。contactCount是初次收集的接触数，包含trigger；candidateCount尚未经过类别过滤；发出的事件数还可能因pending而更少。

当前位置修正可能制造新接触，但不每轮重建候选；事件携带初始深度，不是最终残余深度。未实现角动力学、CCD、关节、热启动和跨步接触缓存；120Hz仍可能使高速物体跨过薄墙，不能把小步长当连续检测。

<a id="chapter-12"></a>

## 12. EventBus、输入和伤害怎样连起来

### 12.1 先从外部订阅写法理解接口

DamageSystem注册`SubscribeToEvent<CollisionEvent>(this,&DamageSystem::onCollision)`。模板参数说明事件类型，this是处理对象，成员函数指针告诉总线发生事件时调用哪个方法。

回调包装器的主要定义如下：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/EventBus/EventBus.h:26)）：

```cpp
template <typename TOwner, typename TEvent>
class EventCallBack :public IEventCallBack {
private:

	typedef void (TOwner::* CallbackFunction)(TEvent&);
	TOwner* ownerInstance;
	CallbackFunction callbackFunction;

	virtual void Call(Event& e)override {
		std::invoke(callbackFunction, ownerInstance, static_cast<TEvent&>(e));
	}

public:
	EventCallBack(TOwner* ownerInstance, CallbackFunction callbackFunction) {
		this->ownerInstance = ownerInstance;
		this->callbackFunction = callbackFunction;
	}
	~EventCallBack() override = default;
};
```

包装器保存ownerInstance和callbackFunction；Call把通用Event&还原成TEvent&，std::invoke把成员函数指针、对象、事件结合调用。不同包装器通过IEventCallBack虚接口统一存在一张表中。

### 12.2 总线表和订阅代码

```cpp
// 源码成员摘写
using HandlerList = std::list<std::unique_ptr<IEventCallBack>>;
std::map<std::type_index,std::unique_ptr<HandlerList>> subscribers;
```

最外层按C++事件类型分组，某一类对应一个回调链表。Subscribe找到/创建链表，make_unique一个具体EventCallBack，再移入链表尾部。

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/EventBus/EventBus.h:63)）：

```cpp
template <typename TEvent, typename TOwner>
void SubscribeToEvent(TOwner* ownerInstance, void (TOwner::* callbackFunction)(TEvent&)) {
	if (!subscribers[typeid(TEvent)].get()) {
		subscribers[typeid(TEvent)] = std::make_unique<HandlerList>();
	}
	auto subscriber = std::make_unique<EventCallBack<TOwner, TEvent>>(ownerInstance, callbackFunction);
	subscribers[typeid(TEvent)]->push_back(std::move(subscriber));
}
```

### 12.3 发事件时不是排队，而是直接调用

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/EventBus/EventBus.h:73)）：

```cpp
template <typename TEvent, typename ...TArgs>
void EmitEvent(TArgs&& ...args) {
	auto handlers = subscribers[typeid(TEvent)].get();

	if (handlers) {
		for (auto it = handlers->begin(); it != handlers->end(); it++) {
			auto handler = it->get();
			TEvent event(std::forward<TArgs>(args)...);
			handler->Execute(event);
		}
	}
}
```

发布CollisionEvent会立刻查对应列表、逐个构造事件并Execute；处理完才返回CollisionSystem。每个handler循环里单独构造事件，所以事件字段不是共享同一个对象；但Entity指向同一世界，改Health会被其他逻辑看到。

总线不拥有owner对象，unique_ptr只拥有包装器。当前没有完整退订token，系统移除后要避免悬空回调；Reset会清订阅表，也不能在分发中随意销毁当前链表。

### 12.4 伤害怎样从事件改到组件

onCollision根据projectiles、enemies、player分组/标签判断双方角色，再调用命中处理。命中敌人的函数如下：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/DamageSystem.h:43)）：

```cpp
void onProjectileHitsEnemy(Entity projectile, Entity enemy) {
	if (!projectile.HasComponent<ProjectileComponent>()) return;
	auto projectileComponent = projectile.GetComponent<ProjectileComponent>();

	if (projectileComponent.isFriendly && enemy.HasComponent<HealthComponent>()) {
		auto& healeh = enemy.GetComponent<HealthComponent>();
		healeh.healthPercentage -= projectileComponent.hitPercentDamage;

		if (healeh.healthPercentage <= 0) {
			enemy.Kill();
		}

		projectile.Kill();
	}
}
```

先确认有Projectile；友军子弹且敌人有Health时才扣血；血量<=0时Kill敌人，随后Kill子弹。没有直接delete，结构清理由Registry子步末提交。

CollisionSystem下一对发事件前发现子弹pending，就跳过，所以同一发子弹不会继续给第二个重叠敌人扣血。这是当前发布链的规则，不是Damage接口自身对任意重复调用都有幂等保证。

### 12.5 两条子弹生成路径

空格事件回调遍历发射器，当前用CameraFollow+RigidBody识别可手动发射对象；按速度符号决定方向，组合Transform、RigidBody、Sprite、Collider和Projectile。自动发射则在每个子步比较模拟毫秒与lastEmissionTime，达到间隔就生成并更新时间。

关键局部代码逻辑如下，属于教学摘写：

```cpp
auto projectile=registry.CreateEntity();
projectile.Group("projectiles");
// 添加位置、速度、图片等组件……
BoxColliderComponent collider(4,4);
collider.isTrigger=true;
projectile.AddComponent<BoxColliderComponent>(collider);
projectile.AddComponent<ProjectileComponent>(friendly,damage,duration);
```

在Defer内必须先本地配置trigger，再Add；排队Add后马上Get新Collider会找不到。手动生成在帧开始提交，自动生成在子步末提交；自动子弹最早下一子步运行。

### 12.6 键盘和寿命系统的具体规则

KeyboardControl的方向键把RigidBody.velocity改为配置的四向速度，同时切Sprite.srcRect.y为对应行；LALT清速度，没有完整KEYUP多键状态机。静止速度为0时手动发射可能得到静止子弹，独立Facing状态是后续改进。

ProjectileLifecycle每子步检查`模拟毫秒-startTime>duration`就Kill，因此暂停不会过期。它只要求Projectile组件，不负责敌人血量和物理触发规则。各System是沿同一组件世界分工，而不是互相深拷贝游戏对象。

<a id="chapter-13"></a>

## 13. Lua关卡和脚本函数怎样成为运行中的对象

### 13.1 关卡数据长什么样

下面是按当前格式写的教学示例，不依赖你先打开level2.lua：

```lua
Level = {
    assets = {
        [0] = { type="texture", id="tank-texture", file="./assets/images/tank-panther-spritesheet.png" }
    },
    tilemap = { -- 这里配置地图文件、图集、行列数、tile_size与scale
    },
    entities = {
        [0] = {
            group="enemies",
            components={
                transform={position={x=100,y=200},scale={x=1,y=1},rotation=0},
                rigidbody={velocity={x=60,y=0}},
                sprite={texture_asset_id="tank-texture",width=32,height=32},
                on_update_script={ [0]=function(entity,dt,ms)
                    -- 可以调用绑定的C++ API
                end }
            }
        }
    }
}
```

Lua的table既能保存嵌套数据也能保存函数。当前assets和entities显式从0读取，缺项即停止，是项目格式约定。真正运行关卡还需完整tilemap配置，这里省略的是例子内容，不是加载器不需要。

### 13.2 LevelLoader逐层做了什么

LoadLevel按编号拼文件路径，load_file检查加载，再script_file执行，得到主Lua全局Level。先遍历assets调用AssetStore，再读取地图每格创建Transform+Sprite的tiles实体，最后遍历entities。

每个对象：CreateEntity → 加SceneObject保存名称/来源索引 → 设置tag/group → 对存在的配置字段调用AddComponent。以Transform为例，Lua的position.x/y转成glm::vec2，再调用第1章相同的C++添加接口。

脚本配置不是拿字符串当代码每帧执行，而是加载时从`on_update_script[0]`取sol::function放进ScriptComponent。相关组件完整定义如下：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Components/ScriptComponent.h:1)）：

```cpp
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
```

### 13.3 Lua调用C++到底怎么绑定

CreateLuaBindings先用new_usertype注册Entity可调用的方法，再用set_function注册自由函数。例如速度设置绑定到这个函数：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/ScriptSystem.h:90)）：

```cpp
	void CreateLuaBindings(sol::state& lua) {
		lua.new_usertype<Entity>(
			"entity",
			"get_id", &Entity::GetId,
            "is_valid", &Entity::IsValid,
            "get_generation", &Entity::GetGeneration,
			"destroy", &Entity::Kill,
			"has_tag", &Entity::HasTag,
			"belongs_to_group", &Entity::BelongsToGroup
		);

		lua.set_function("set_position", SetEntityPosition);
		lua.set_function("get_position", GetEntityPosition);
		lua.set_function("get_velocity", GetEntityVelocity);
		lua.set_function("set_velocity", SetEntityVelocity);
```

`new_usertype<Entity>("entity",名称,成员函数指针,...)`把C++类型的方法暴露给Lua；`set_function("set_velocity",SetEntityVelocity)`把Lua全局名称关联到C++函数。参数转换由Sol处理，游戏状态修改由函数体完成。

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/ScriptSystem.h:31)）：

```cpp
void SetEntityVelocity(Entity entity, double x, double y) {
	if (entity.HasComponent<RigidBodyComponent>()) {
		auto& rigidbody = entity.GetComponent<RigidBodyComponent>();
		rigidbody.velocity.x = x;
		rigidbody.velocity.y = y;
	}
	else {
		Logger::Err("设置速度实体无RigidbodyComponent");
	}
}
```

Lua执行`set_velocity(entity,60,0)`，Sol负责把userdata和数字转成C++参数；这个函数检查RigidBody，取得组件引用后写x/y，修改的是池中原数据。它不是修改Lua里另一份速度副本。

同理，get_position/get_velocity返回tuple，由绑定层转成多个Lua返回值；add_force/apply_impulse调用RigidBody的接口。Entity还暴露is_valid和get_generation用于观察身份。

### 13.4 C++每步怎样调用Lua

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/ScriptSystem.h:116)）：

```cpp
void Update(double deltaTime, int ellapsedTime) {
	for (auto entity : GetSystemEntities()) {
		if (entity.registry->IsPendingRemoval(entity)) continue;
		const auto script = entity.GetComponent<ScriptComponent>();
		if (script.enabled) script.func(entity, deltaTime, ellapsedTime);
	}
}
```

系统按Script签名筛选对象，跳过pending，取ScriptComponent，enabled为true就调用func(entity,h,模拟毫秒)。Game把它放在Movement前，所以脚本本步加力后积分可以立刻使用。

这里按值取Script组件会复制函数引用，不会复制Lua虚拟机。Lua环境由Game持有，函数引用必须先于state结束；Entity的安全句柄防止旧Lua引用误改复用对象，但不代表整个脚本系统有执行沙箱。

### 13.5 编辑快照怎样恢复脚本

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp:43)）：

```cpp
std::optional<sol::function> SceneEditor::ScriptFor(const SceneEntityData& data) const {
    if (!data.hasScript) return std::nullopt;
    if (data.object.scriptIndex < 0) throw std::runtime_error("此脚本没有可保存的原关卡来源");
    sol::table level = lua["Level"];
    sol::table entities = level["entities"];
    sol::optional<sol::table> source = entities[data.object.scriptIndex];
    if (!source) throw std::runtime_error("原关卡中不存在对应脚本对象");
    sol::table components = source.value()["components"];
    sol::optional<sol::table> scripts = components["on_update_script"];
    if (!scripts) throw std::runtime_error("原关卡中不存在对应更新脚本");
    sol::object function = scripts.value()[0];
    if (function.get_type() != sol::type::function) throw std::runtime_error("脚本来源不是有效函数");
    return function.as<sol::function>();
}
```

没有脚本返回nullopt；有脚本则要求scriptIndex非负；去当前主Lua的Level.entities对应位置找components.on_update_script[0]，确认是函数，转成sol::function。

保存的是**来源和启用状态**，不是函数地址、字节码、闭包上值或协程。原关卡索引变化可能使来源失效；这也不等于热重载。行为脚本的逐实体错误隔离尚可加强，不能因为场景数据解析另有protected_function，就说所有Lua行为都已安全隔离。

<a id="chapter-14"></a>

## 14. RenderSystem、动画、图元、相机和文字怎样绘制

### 14.1 Sprite保存的是绘制参数

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Components/SpriteComponent.h:1)）：

```cpp
#pragma once
#ifndef SPRITECOMPONENT_H
#define SPRITECOMPONENT_H
#include<glm/glm.hpp>
#include<string>
#include<SDL.h>
struct SpriteComponent {
	std::string assetId;
	int width;
	int height;
	int zIndex;
	SDL_RendererFlip flip;
	bool isFixed;
	SDL_Rect srcRect;

	SpriteComponent(std::string assetId = "", int width = 0, int height = 0, int zIndex = 0, bool isFixed = false, int srcRectX = 0, int srcRectY = 0) {
		this->assetId = assetId;
		this->height = height;
		this->width = width;
		this->zIndex = zIndex;
		this->flip = SDL_FLIP_NONE;
		this->isFixed = isFixed;
		this->srcRect = { srcRectX,srcRectY,width,height };
	}
};
#endif // !SPRITECOMPONENT_H
```

assetId找AssetStore中的纹理；width/height表示单帧尺寸；srcRect是从图集中取哪块；zIndex控制精灵内部先后；isFixed决定是否减相机；flip是图片翻转。纹理不在组件里重复持有像素。

### 14.2 精灵绘制有三个阶段

第一阶段遍历Transform+Sprite，复制成Renderables，做视口剔除；第二阶段按zIndex排序；第三阶段计算dstRect并调用SDL。核心绘制部分源码节选：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/RenderSystem.h:54)）：

```cpp
// 设置原始精灵纹理的源矩形
SDL_Rect srcRect = sprite.srcRect;

// 设置目标矩形，确定精灵在SDL窗口显示位置
SDL_Rect dstRect = {
	static_cast<int>(transform.position.x - (sprite.isFixed ? 0 : camera.x)),
	static_cast<int>(transform.position.y - (sprite.isFixed ? 0 : camera.y)),
	static_cast<int>(sprite.width * transform.scale.x),
	static_cast<int>(sprite.height * transform.scale.y)
};

// 绘制png纹理
SDL_RenderCopyEx(
	renderer,
	assetStore->GetTexture(sprite.assetId),
	&srcRect,
	&dstRect,
	transform.rotation,
	NULL,
	sprite.flip
);
```

屏幕x=世界x-camera.x，fixed时不减；目标宽=精灵宽*scale.x。srcRect只是采样范围，dstRect才是屏幕范围。SDL_RenderCopyEx还接收角度和flip，底层真正画图由SDL负责。

例如世界(600,300)、camera(400,200)，32×32图像scale=(2,2)，屏幕位置就是(200,100)、大小64×64。相机移动不必挪所有世界对象，只改变绘制变换。

### 14.3 动画不是重新加载图片，而是改srcRect

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/AnimationSystem.h:18)）：

```cpp
	void Update() {
		for (auto& entity : GetSystemEntities()) {
			auto& animation = entity.GetComponent<AnimationComponent>();
			auto& sprite = entity.GetComponent<SpriteComponent>();


			const int frame = static_cast<int>((SimulationClock::Milliseconds() - animation.startTime) * animation.frameSpeedRate / 1000.0);
            animation.currentFrame = animation.isLoop ? frame % animation.numFrames : (std::min)(frame, animation.numFrames - 1);
			sprite.srcRect.x = animation.currentFrame * sprite.width;
		}
	}
```

经过模拟时间乘帧速率得到frame，循环取模，非循环停末帧；然后srcRect.x=currentFrame*sprite.width。例如4帧、8fps、375ms，得到第3帧，srcRect.x=3*32=96。

键盘方向改变srcRect.y选择图集行。暂停时模拟时间不变，所以动画不跳帧。Lua直接改currentFrame会被后面Animation按时间覆盖，手动控帧需要额外模式，不是仅绑定set_animation_frame就有完整动画控制器。

### 14.4 新建无纹理矩形/圆怎样可见

PrimitiveRenderComponent只有颜色等外观状态；RenderPrimitiveSystem要求Transform+Collider+Primitive，有Sprite则跳过。它调用同一个ShapeOf得到世界形状，先裁屏外范围，再用扫描线填充。

教学摘写：

```cpp
for (int y=minY; y<=maxY; ++y) {
    float scan=y+0.5f;
    if (shape.type==Circle) {
        float dy=scan-shape.center.y;
        float w2=shape.radius*shape.radius-dy*dy;
        if (w2<0) continue;
        left=shape.center.x-sqrt(w2);
        right=shape.center.x+sqrt(w2);
    } else {
        // 扫描线与矩形4条边求交，取最左与最右交点
    }
    SDL_RenderDrawLine(renderer,left-camera.x,y-camera.y,
                      right-camera.x,y-camera.y);
}
```

圆直接用圆方程求水平半宽；矩形对非水平跨越边插值求x交点，用半开区间避免顶点重复算。这个填充算法是当前简单凸形状的实现，不是自动支持任意凹多边形的渲染器。

### 14.5 相机和调试绘制

CameraFollow+Transform被Camera系统处理，按对象位置减半屏设置camera，之后限制到mapSize-viewSize的合法范围。编辑器打开时停止自动跟随，允许中键平移；当前没有平滑插值跟随。

RenderColliderSystem复用WorldShape，矩形取四角连线，圆用48段近似，绿色普通、黄色trigger。调试只读几何，不改变实际形状。

### 14.6 文字与血条的完整资源过程

RenderText每次取TextLabel，按字体ID找TTF_Font；TTF_RenderText_Blended生成Surface；SDL_CreateTextureFromSurface生成临时Texture；释放Surface；查询纹理尺寸、计算fixed或世界dstRect；SDL_RenderCopy；销毁临时Texture。

血条先根据Health颜色和百分比绘矩形，再走类似文字流程显示数值。当前位置与单位尺寸来自Transform/Sprite，生命值则由Damage修改，因此伤害后本帧绘制能看到新值。

文字不变也每帧创建纹理，后续可以按字体/文本/颜色缓存并在变化时重建。精灵排序只覆盖Sprite，图元、文字、血条还有固定系统顺序；旋转精灵剔除未用旋转外接框，同层排序也未稳定定义，这些都是理解实现后才能准确分析的边界。


<a id="chapter-15"></a>

## 15. 场景编辑器的类怎样组织，操作怎样落到ECS

### 15.1 先看三个层次怎样连接

```text
RenderGUISystem：画按钮、列表、输入框、视口，收集用户动作
        ↓ 调用
SceneEditor：选择、创建、复制、删除、暂停、捕获、恢复、请求执行
        ↓ 调用
Registry / AssetStore / 主Lua：实际对象、资源与脚本
```

UI不直接负责把文件转成所有实体；SceneEditor不需要知道每个ImGui按钮的屏幕位置。这样控制器操作可以单独测试，真实按钮是否调用正确也能另做UI测试。

SceneEditor主要成员教学摘写：

```cpp
Registry& registry;
AssetStore& assets;
sol::state& lua;
int baseLevel;
Entity selected;
SceneData initial;
bool paused=false, stepRequested=false;
enum class Request { None,Save,Load,Reset } request=Request::None;
std::string requestPath;
```

引用表示编辑器使用Game已经拥有的模块，不另建第二份游戏世界。selected是完整Entity，initial是启动时快照。request是单个请求槽，不是能积累无限操作的队列；后来的请求会覆盖前一个。

### 15.2 可编辑对象怎样列出来

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp:18)）：

```cpp
std::vector<Entity> SceneEditor::Entities() const {
    std::vector<Entity> result;
    for (auto entity : registry.GetEntities()) if (entity.HasComponent<SceneObjectComponent>()) result.push_back(entity);
    return result;
}
```

遍历Registry可见实体，只保留有SceneObject的对象。地图tiles和运行时子弹通常没有SceneObject，所以既不会出现在对象列表，也不会被Capture写入对象快照。SceneObject保存名字和原关卡脚本索引，不保存所有业务数据。

Selected返回前再次检查有效且非pending，因此加载或删除后旧句柄不会“选中新对象”。列表搜索按对象名称、tag/group文本过滤；它没有真正的父子层级树，不能因函数叫Hierarchy就认为存在父子Transform。

### 15.3 新建矩形按钮最终执行的代码

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp:90)）：

```cpp
Entity SceneEditor::Create(ColliderShape shape, glm::vec2 position) {
    MarkEdited();
    SceneEntityData data;
    data.object.name = shape == ColliderShape::Box ? "新建矩形" : "新建圆形";
    data.group = "objects"; data.transform = TransformComponent(position);
    data.body = RigidBodyComponent();
    data.collider = shape == ColliderShape::Box ? ColliderComponent(40, 40) : ColliderComponent::Circle(20);
    data.primitive = PrimitiveRenderComponent();
    if (shape == ColliderShape::Circle) data.primitive->color = {235, 180, 75, 255};
    selected = Spawn(data, std::nullopt);
    return selected;
}
```

先MarkEdited暂停并标脏，创建SceneEntityData，设置对象名、objects组、Transform、默认RigidBody、Collider和Primitive颜色，最后交Spawn生成实体并选择它。

Spawn的实现把每个optional值真正变成组件：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp:57)）：

```cpp
Entity SceneEditor::Spawn(const SceneEntityData& data, const std::optional<sol::function>& script) {
    auto entity = registry.CreateEntity();
    entity.AddComponent<SceneObjectComponent>(data.object);
    if (!data.tag.empty()) entity.Tag(data.tag);
    if (!data.group.empty()) entity.Group(data.group);
    Add(entity, data.transform); Add(entity, data.body); Add(entity, data.sprite); Add(entity, data.collider);
    Add(entity, data.animation); Add(entity, data.health); Add(entity, data.emitter); Add(entity, data.keyboard); Add(entity, data.text);
    Add(entity, data.primitive);
    if (data.cameraFollow) entity.AddComponent<CameraFollowComponent>();
    if (script) {
        ScriptComponent component(*script); component.enabled = data.scriptEnabled;
        entity.AddComponent<ScriptComponent>(component);
    }
    return entity;
}
```

Add(entity,optional)辅助函数只在有值时调用entity.AddComponent。CameraFollow是标记组件，script则从已验证来源创建ScriptComponent并恢复enabled。若当前GUI处于Defer，创建的句柄先存在，但组件在下一提交才可查，因此按钮逻辑不能立刻Get新组件。

### 15.4 检查器如何把控件绑定到真实字段

ComponentPanel是一个模板：T指定组件，fields回调具体绘制这个组件字段。下面是源码：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/RenderGUISystem.cpp:79)）：

```cpp
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
```

先TryGet<T>，没有就不画面板；有则把`*value`直接交fields，控件可改原组件。点击移除时先清理直接依赖，再排Remove；字段改变时Sanitize校验范围，MarkEdited暂停。

例如刚体面板的教学摘写：

```cpp
ComponentPanel<RigidBodyComponent>("刚体",editor,[&](auto& c) {
    bool changed=ImGui::DragFloat("质量",&c.mass,0.1f,0.001f,1000000);
    changed |= ImGui::DragFloat2("速度",&c.velocity.x,1);
    return changed;
});
```

关键是控件收到组件字段地址，数据修改无需另做“把临时窗口值拷回世界”。但添加/移除组件是结构操作，要排队，和普通字段修改不同。

移Sprite会连带移Animation和Keyboard；移RigidBody连带移Keyboard；移Collider连带移Primitive。Sanitize还处理Ctrl直接输入越界和NaN等，不能只依赖滑条显示的最小最大值。依赖规则目前手写且分散，不是通用反射系统。

### 15.5 场景画面点击怎样选中对象

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp:144)）：

```cpp
Entity SceneEditor::Pick(glm::vec2 screen, const SDL_Rect& camera) const {
    Entity result; int bestZ = (std::numeric_limits<int>::min)();
    for (auto entity : Entities()) {
        auto* transform = entity.TryGetComponent<TransformComponent>();
        if (!transform) continue;
        auto* sprite = entity.TryGetComponent<SpriteComponent>();
        const auto point = screen + (sprite && sprite->isFixed ? glm::vec2(0) : glm::vec2(camera.x, camera.y));
        Physics2D::WorldShape shape;
        if (sprite) shape = Physics2D::MakeShape(*transform, ColliderComponent(sprite->width, sprite->height));
        else if (entity.HasComponent<ColliderComponent>()) shape = Physics2D::ShapeOf(entity);
        else shape = Physics2D::MakeShape(*transform, ColliderComponent(16, 16));
        const auto delta = point - shape.center;
        const bool hit = shape.type == ColliderShape::Circle ? glm::dot(delta, delta) <= shape.radius * shape.radius
            : std::abs(glm::dot(delta, shape.xAxis)) <= shape.half.x && std::abs(glm::dot(delta, shape.yAxis)) <= shape.half.y;
        const int z = sprite ? sprite->zIndex : entity.HasComponent<PrimitiveRenderComponent>() ? 100001 : 0;
        if (hit && z >= bestZ) { result = entity; bestZ = z; }
    }
    return result;
}
```

每个对象先取Transform；点击点对普通对象加camera转成世界坐标，fixed对象不加。优先按Sprite整张显示矩形构造形状；无Sprite则使用Collider，仍没有就用16×16回退盒。

圆看`dot(delta,delta)<=r²`；矩形把delta投影到两条局部轴，分别比较half。命中后按z优先级保留对象，图元用固定高优先级，Sprite用zIndex。同层命中使用后遍历对象，未做纹理alpha拾取，文字也没有画面边界选择。

### 15.6 拖动为什么要记录抓取偏移

按下时RenderGUI保存dragEntity，以及`dragOffset=对象position-鼠标世界位置`。拖动时新的position=当前鼠标世界位置+dragOffset，保持抓住的位置而非把对象左上角瞬移到鼠标。

移动落在控制器的函数如下：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp:131)）：

```cpp
void SceneEditor::MoveSelected(glm::vec2 position) {
    auto entity = Selected(); if (!entity.IsValid()) return;
    if (snap && grid > 0) position = glm::round(position / grid) * grid;
    if (auto* transform = entity.TryGetComponent<TransformComponent>()) { transform->position = position; MarkEdited(); }
    else if (auto* text = entity.TryGetComponent<TextLabelComponent>()) { text->position = position; MarkEdited(); }
}
```

snap开启时round(position/grid)*grid；有Transform就改位置，否则可改Text位置。MarkEdited会暂停。UI只在鼠标位于视口且未被ImGui捕获时拖动；中键MouseDelta修改camera形成画布平移。

### 15.7 复制、删除和脚本开关

DuplicateSelected先CaptureEntity，再恢复脚本来源，名称加副本、位置平移24，清唯一tag和CameraFollow，再Spawn新身份。普通组件值复制，Lua闭包状态不保证深复制。

DeleteSelected是MarkEdited、entity.Kill、清选择；真正删除仍在Registry.Update。Script.enabled可在检查器关闭，避免Lua在恢复运行时立刻覆盖用户拖动的位置。

### 15.8 保存/加载按钮为什么先记请求

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp:181)）：

```cpp
void SceneEditor::ApplyRequests() {
    const auto action = request; request = Request::None;
    if (action == Request::Save) Save(requestPath);
    if (action == Request::Load) Load(requestPath);
    if (action == Request::Reset) Reset();
}
```

Toolbar调用RequestSave/Load/Reset只改request和path；Game下一Update在Defer外调用ApplyRequests，先取出并清None再执行。这样操作内部需要Registry.Update时，不会在GUI正在遍历组件的阶段重建场景。

dirty由数值编辑和模拟步置true，保存/恢复清零，是保守的“可能发生修改”，不是精确diff。恢复和脏场景加载有确认框；当前没有撤销/重做，单请求槽也不保存编辑历史。

<a id="chapter-16"></a>

## 16. 场景快照怎样编码、校验、保存并恢复

### 16.1 先定义能离开运行内存的数据

下面是SceneData的实际定义，头文件引用省略：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneData.h:17)）：

```cpp
struct SceneEntityData {
    SceneObjectComponent object;
    std::string tag, group;
    std::optional<TransformComponent> transform;
    std::optional<RigidBodyComponent> body;
    std::optional<SpriteComponent> sprite;
    std::optional<ColliderComponent> collider;
    std::optional<AnimationComponent> animation;
    std::optional<HealthComponent> health;
    std::optional<ProjectileEmitterComponent> emitter;
    std::optional<KeyboardControlledComponent> keyboard;
    std::optional<TextLabelComponent> text;
    std::optional<PrimitiveRenderComponent> primitive;
    bool cameraFollow = false;
    bool hasScript = false;
    bool scriptEnabled = true;
};
struct SceneData {
    int version = 1;
    int baseLevel = 2;
    double simulationSeconds = 0;
    std::vector<SceneEntityData> entities;
};
```

optional表示组件存在性；没有Sprite与“一个空名字Sprite”不同。cameraFollow/hasScript表示标记和函数是否存在，scriptEnabled表示开关；SceneData保存版本、基础关卡和模拟时间，不保存Entity ID、组件池下标、SDL指针或函数地址。

### 16.2 Capture怎样从实体拼出快照

CaptureEntity逐种TryGet组件，有则拷值到对应optional；Copy辅助函数让各组件共用这套逻辑。Capture遍历可编辑Entities，把每份SceneEntityData放进数组：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp:38)）：

```cpp
SceneData SceneEditor::Capture() const {
    SceneData data; data.baseLevel = baseLevel; data.simulationSeconds = SimulationClock::Seconds();
    for (auto entity : Entities()) data.entities.push_back(CaptureEntity(entity));
    return data;
}
```

快照保留速度、累计力、动画和发射时间等值，但不包括tiles、运行时子弹、Lua全局/闭包/协程。baseLevel让加载知道去哪套关卡资源和脚本来源中解析。

### 16.3 写出的文件格式是什么

以下为教学示例，完整文件还包含每种组件的已验证字段：

```lua
return {
  version=1, base_level=2, simulation_seconds=3.5, entity_count=1,
  entities={
    {name="敌人",origin_index=7,tag="",group="enemies",
     transform={position={x=100,y=200},scale={x=1,y=1},rotation=0},
     camera_follow=false,
     script={source_index=7,enabled=true}}
  }
}
```

它返回一个值table，而原关卡给全局Level赋值，两个格式不同。保存强制`.scene.lua`后缀，不直接覆盖level2.lua。

Encode用ostringstream逐组件输出字段，classic locale确保小数点；double精度17、vec2 float精度9用于往返；Quote转义引号、反斜杠和控制字符，中文UTF-8字节保留。Encode结尾再次Decode验证，避免写出下次不能读的文件。

### 16.4 Decode先在独立Lua里取得返回表

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneSerializer.cpp:145)）：

```cpp
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
```

检查8MB上限；新建parser，不打开标准库；挂指令计数hook限制循环；跳过UTF-8 BOM；加载并protected调用chunk，确认返回table。这个parser与游戏主Lua不同，不会直接改游戏里的Level或实体。

之后读取version=1、baseLevel范围、模拟时间、对象数<=10000。每个对象检查字段类型、有限性、整数性、尺寸/质量范围、标签唯一和组件依赖。

数字字段的共同读取函数如下：

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneSerializer.cpp:42)）：

```cpp
double Number(const sol::table& table, const char* key, double min = -1000000, double max = 1000000) {
    sol::object value = table[key];
    if (value.get_type() != sol::type::number) throw std::runtime_error(std::string("字段应为数值：") + key);
    const double number = value.as<double>();
    if (!std::isfinite(number) || number < min || number > max) throw std::runtime_error(std::string("数值超出范围：") + key);
    return number;
}
```

Number先验Lua类型，转double，再isfinite及范围判断；Integer进一步floor等于自身，防止0.5静默变0。mask验证[0,2³²-1]内整数后转uint32，保留高位。文字还拒绝内嵌零字符和过长内容。

### 16.5 Restore怎样避免先清场再发现文件坏了

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp:72)）：

```cpp
void SceneEditor::Restore(const SceneData& input) {
    registry.Update();
    // 先完整验证文件、资源和脚本，再替换对象；错误文件不能先清掉当前场景。
    auto scene = SceneSerializer::Decode(SceneSerializer::Encode(input));
    if (scene.baseLevel != baseLevel) throw std::runtime_error("场景属于另一个基础关卡，请从对应关卡启动后再加载");
    std::vector<std::optional<sol::function>> scripts;
    for (const auto& entity : scene.entities) {
        if (entity.sprite && !assets.HasTexture(entity.sprite->assetId)) throw std::runtime_error("找不到精灵资源：" + entity.sprite->assetId);
        if (entity.text && !assets.HasFont(entity.text->assetId)) throw std::runtime_error("找不到字体资源：" + entity.text->assetId);
        scripts.push_back(ScriptFor(entity));
    }
    for (auto entity : registry.GetEntities()) if (!entity.BelongsToGroup("tiles")) entity.Kill();
    registry.Update();
    SimulationClock::Set(scene.simulationSeconds);
    for (std::size_t i = 0; i < scene.entities.size(); ++i) Spawn(scene.entities[i], scripts[i]);
    registry.Update();
    selected = Entity(); paused = true; stepRequested = false; dirty = false;
}
```

逐段执行：先提交已有命令；Encode→Decode走统一值校验；baseLevel必须一致；全部纹理、字体和脚本来源先验证并保存函数引用。验证完成后，Kill非tiles对象并提交，恢复模拟时间，Spawn全部快照对象再提交，清选择、暂停、清单步与dirty。

这样可预期的错误字段、缺资源和坏脚本索引会在替换前拒绝，背景瓦片保留，运行时子弹被清理。若替换开始后发生内存不足等异常，当前没有把旧世界完整回滚，因此不是任意失败都原子恢复的事务。

### 16.6 保存到硬盘的代码

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneSerializer.cpp:239)）：

```cpp
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
```

验证后缀并Encode成功才写文件；先在相同目标目录写`.tmp`、flush和检查，最后Windows用MoveFileExW替换目标，其他平台rename。直接截断目标文件后失败会毁旧文件，临时文件缩小这个风险。

SceneEditor::Save在提交后Capture，确认脚本来源，再调用这个保存函数，成功清dirty，失败把信息写status。Load从路径读文本Decode，再Restore；路径使用u8path配合Windows宽字符API，支持中文路径。

### 16.7 这套流程的保证具体到哪一步

Defer保证遍历时结构不立刻变；Decode/环境预检保证常见错误输入在替换前拒绝；临时文件替换保护旧文件；三者合起来仍不表示内存状态在任意异常下可回滚。

8MB只是源文本上限，Lua还可能分配更多内存；无标准库加指令hook降低风险，但没有自定义allocator内存预算。脚本来源仍依赖原关卡索引，也不是能独立带走全部资源和VM状态的游戏存档。

<a id="chapter-17"></a>

## 17. AssetStore、日志和底层C++机制怎样服务各模块

### 17.1 资源表的成员与读取

AssetStore内部是两张map：`string→SDL_Texture*`与`string→TTF_Font*`。精灵组件只保存assetId，绘制时GetTexture(assetId)查表；文字类似取Font。

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/AssetStore/AssetStore.cpp:25)）：

```cpp
void AssetStore::AddTexture(SDL_Renderer* renderer, const std::string& assetId, const std::string& filePath) {
	SDL_Surface* surface = IMG_Load(filePath.c_str());
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_FreeSurface(surface);

	// 纹理添加进映射
	textures.emplace(assetId, texture);

	Logger::Log("新纹理已加入，assetId = " + assetId);
}
```

IMG_Load把磁盘图像解码成CPU Surface；SDL_CreateTextureFromSurface生成renderer关联纹理；Surface释放；Texture放入表。资源共享使许多Sprite可引用同一张图集，无需每个实体加载一遍。

GetTextureIds/GetFontIds枚举非空资源供检查器下拉选择，Has方法用于Restore预检。当前Add重复ID未处理新指针回收、Get用operator[]会插空、加载失败检查不完整；这些是资源模块的具体改进点，不要把编辑器验证扩大成所有入口都健壮。

### 17.2 统一清理实际调用哪些释放函数

源码节选（[查看对应位置](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/AssetStore/AssetStore.cpp:13)）：

```cpp
void AssetStore::ClearAssets() {
	for (auto texture : textures) {
		SDL_DestroyTexture(texture.second);
	}
	textures.clear();

	for (auto font : fonts) {
		TTF_CloseFont(font.second);
	}
	fonts.clear();
}
```

Texture用SDL_DestroyTexture，Font用TTF_CloseFont，随后clear容器。AssetStore析构也调用ClearAssets，正常Game::Destroy会提前清理，让纹理先于renderer释放。

SDL资源不能用普通delete，若改RAII要配置专用删除器。unique_ptr能保证调用删除器，但仍必须在所属renderer有效时调用，所有权和上下文顺序是两件事。

### 17.3 Logger是怎样工作的

Log/Err构造LogEntry，保存级别和带时间的字符串，输出控制台，再push到静态vector。测试会检查LOG_ERROR判断正常路径是否出现资源/初始化错误；它不是完整的日志数据库或异步日志库。

vector未设上限，长期大量日志会增长；控制台IO也影响性能。可加入级别过滤、环形缓冲与采样。时间字符串目前固定长度缓冲未按实际写入长度收缩，输出中可能保留尾部零字符，也是可以定位到实现的小问题。

### 17.4 现在再回看用到的C++技术

| 代码中的机制 | 具体承担的工作 |
|---|---|
| 模板Component<T>/Pool<T> | 为每类组件生成类型安全接口与存储 |
| IPool虚接口 | Registry不知道T也能统一Remove/Clear |
| type_index | 在系统/事件表中按运行期C++类型定位 |
| unique_ptr | 独占模块和池、拥有回调包装器 |
| shared/weak token | 标记世界身份和寿命，不共同拥有Registry |
| lambda+std::function | 捕获待提交组件值与完整句柄 |
| forward/move | 保留构造参数值类别、把组件值移进池 |
| RAII guard | 作用域进入/退出维护deferDepth和updating |
| optional | 查询失败或组件不存在有明确值表示 |

这些技术是为了实现具体数据流，不是各自独立的“高级特性展示”。理解GetComponent一路怎么调用，才知道模板、类型擦除和指针分别在哪层工作。

### 17.5 构建与测试入口怎样组织

CMake默认构建不依赖SDL的ecs_tests/physics_tests；有SDL路径增加物理ECS集成；开启NS2D_BUILD_GAME后把游戏、Editor、GUI和SDL/ImGui代码构成ns2d_game_core库，再链接游戏及各层测试程序。

算法测试直接构造值，ECS测试实例化真实Registry，编辑器测试调用控制器，UI测试注入ImGui输入，游戏测试跑真实关卡。它们共享相同实现，所以不是另外写一套简化引擎通过测试。具体上次运行记录在第20章，本次修改文档不改动引擎。

<a id="chapter-18"></a>

## 18. 把各模块连成两个可完整推演的例子

### 18.1 从加载敌人到被子弹击杀

假设Lua配置一名enemies对象：Transform(100,200)、RigidBody(60,0)、Sprite、Collider和Health100。

1. LevelLoader读字段，CreateEntity申请身份；AddComponent把值放相应池，设置签名、标dirty；SceneObject保存可编辑名和脚本来源。
2. Setup的Registry.Update按最终签名，把它加入Movement、Render、Collision、HealthBar等系统，不复制组件。
3. 每个SimulateStep先推进模拟时钟，再脚本与Movement更新位置；没有重力时1/120秒约移动0.5像素。
4. 碰撞系统ShapeOf把新位置和Collider变世界形状，排序扫描输出候选，再过滤和精测。
5. 普通物体接触进入冲量、摩擦、位置修正；子弹为trigger，只保留游戏事件。
6. Damage按分组和阵营取Health引用扣血。子弹Kill立即进pending，后续接触不再发布它的事件；敌人生命耗尽也Kill。
7. 子步末Update清池、成员和索引，代数递增；Render时已不在列表，画面不再绘它。外部旧句柄仍是旧代数，无法操作后续复用对象。

现在每一步都能回到前文已给出的成员和函数，不需要靠“ECS会自动处理”跳过关键过程。

### 18.2 从鼠标拖动到保存、修改、重新加载

1. ProcessInput先给ImGui更新鼠标状态；Render的Viewport确认点击未被面板捕获。
2. Pick把鼠标转到对应坐标系，投影到OBB局部轴或圆距离，结合层级选中完整Entity。
3. 按下记录dragOffset，拖动MoveSelected改Transform并暂停；普通字段立刻生效，没有增删池。
4. 点击保存只记RequestSave，下一Update在保护区外ApplyRequests。
5. Capture把可编辑实体转SceneData，Encode转文本且Decode验证，脚本保留来源而不是函数指针，写tmp后替换目标。
6. 再次修改对象后Load先独立解析及校验，确认资源和主Lua脚本来源存在，才Kill旧非瓦片对象、恢复时间、Spawn新对象并Update。
7. old selected的generation已失效；控制器清选择并保持暂停。画面显示保存时的位置，不会带回运行时子弹或Lua全部内部状态。

### 18.3 学习实现后再讨论设计取舍

本项目的技术联系主要在于：稀疏池给出数据位置，句柄验证给出身份，Defer定义何时能改变位置，系统索引缓存处理集合，模拟时钟定义什么时候前进，场景DTO把运行态转为可验证的持久值。

这些不是各自孤立的术语。后面的性能、问答和练习都以这里的具体实现为基础；遇到未准备的问题，沿“当前数据 → 调用函数 → 状态变化 → 提交时机”的顺序推导即可。



<a id="chapter-19"></a>

## 19. 性能、局限和下一步设计：如何说得可信

### 19.1 当前数据结构成本表

| 操作 | 成本与限定 |
|---|---|
| Entity有效性 | 弱token检查加槽位验证，算法常数，但不是无成本 |
| Pool查询/删除定位 | 数组索引O(1)，组件移动/析构成本另计 |
| Pool新增 | 稠密数组摊销常数，sparse扩展按新增索引区间初始化 |
| 系统成员删除 | 哈希平均O(1)定位加补位，不保持顺序 |
| dirty同步 | 约D×S次签名判断，叠加集合与成员维护 |
| 完整实体销毁 | 遍历系统和池、清索引，不是整体O(1) |
| 对象列表 | 扫槽位、过滤，再构建结果 |
| 精灵准备 | O(N)+O(VlogV)，有副本和资源查询 |
| 碰撞 | 排序、扫描、窄阶段、6轮求解；最坏O(N²) |
| 场景保存/恢复 | 值拷贝、编码/校验、资源脚本检查、实体重建 |

### 19.2 一次合格的性能对照怎么做

固定构建配置、关卡、实体数量、窗口尺寸和日志，分开测脚本、积分、候选扫描、窄阶段、求解、事件、渲染准备与SDL提交。等待限帧时间不等于算法CPU成本；首次资源加载不混进稳态帧。

记录平均和高分位帧耗时、候选/接触/事件数量、命令数量、分配次数，并在改动前后检查结果一致性。不能从“哈希改数组”推导整体游戏加速多少倍；也不能把测试程序总耗时当作游戏FPS。

### 19.3 重构已经解决的旧问题

| 旧问题 | 新机制 |
|---|---|
| 旧句柄误访问复用实体 | world identity+generation+alive |
| 注册表先销毁后句柄访问 | weak生命周期token与owner置空 |
| 组件池默认构造空槽、容量与长度混用 | reserve+真实稠密数组+稀疏索引 |
| 组件删除后系统成员未更新 | dirtyEntities+最终签名同步 |
| 遍历时增删组件导致引用失效 | Defer与有序结构命令 |
| 系统列表反复拷贝、删除全扫 | const视图+成员索引补位 |
| 编辑时动画/寿命仍按真实时间推进 | SimulationClock统一计时 |
| 通用对象无法编辑与保存 | Inspector+SceneData+Serializer |
| 旧GUI纹理ID不匹配 | 从AssetStore实际列表选资源 |
| 退出纹理释放顺序错误 | Destroy先ClearAssets再销毁renderer |

旧文档可用于理解演进，但不能把这些已经修复的问题继续当作当前缺陷。

### 19.4 当前仍有的边界

- 单线程，原子类型编号不意味着所有世界操作线程安全。
- 组件类型上限32，进程级分配，Registry.Clear不重置类型编号。
- 没有角动力学、CCD、热启动、复杂接触缓存或关节。
- 没有完整撤销重做、层级变换、地图画笔、资源导入、脚本源码编辑。
- 运行期Lua保护和AssetStore重复/失败处理仍不完整；场景验证不等于全系统安全。
- 场景依赖基础关卡和脚本索引，未保存完整Lua状态或任意实体关系。
- Defer提交和对象替换不具备任意异常强回滚，引用仍不能跨结构变化长期保存。
- 没有渲染插值、自动依赖调度或多线程任务系统；当前性能收益没有本次基准数据。

### 19.5 下一步按需求选择

稳定性：输入失败传播、资源RAII、连接退订、运行脚本保护；维护性：集中组件schema与依赖、稳定脚本资源ID；性能：先测数据，再决定最小池驱动查询、增量宽阶段或静态瓦片分块；工具：可撤销编辑命令、加载临时世界后交换。

Archetype把相同组件组合集中存储，有利于匹配查询的连续访问，但增删组件可能迁移整组数据。当前稀疏池利于独立组件增删。不要把“换成Archetype”当作没有测量依据的通用升级。

多线程应先描述各系统读写集，再组织依赖；线程局部结构命令合并、事件发布和Lua状态都要有策略。给每个System开一个线程会立即遇到共享Transform、容器和脚本依赖问题。

<a id="chapter-20"></a>

## 20. 测试证据：如何证明理解而不夸大

### 20.1 本次实际验证记录

2026-10-15 在已有本地依赖配置上执行构建和CTest。构建成功，**9/9测试通过**，本次CTest总时间15.96秒。该时间只描述测试运行，不是游戏性能指标。

```powershell
cmake --build .build/cmake --parallel 4
ctest --test-dir .build/cmake --output-on-failure
```

| CTest项 | 本次结果 | 验证重点 |
|---|---|---|
| ecs_tests | 通过 | 池、句柄、系统成员、延迟命令、标签组、类型上限 |
| physics_tests | 通过 | 几何、积分、冲量、摩擦、宽阶段与固定步 |
| physics_ecs_tests | 通过 | 实际碰撞、过滤、触发、旋转中心与单弹伤害 |
| scene_editor_tests | 通过 | 对象操作、快照、中文、错误拒绝、脚本恢复 |
| scene_editor_ui_tests | 通过 | 真实ImGui按钮、输入、拖动、平移、检查器 |
| game_level_2_smoke | 通过 | 原关卡加载、游戏循环及子弹创建 |
| game_level_3_smoke | 通过 | 物理演示关卡运行 |
| editor_level_2_integration | 通过 | 真实关卡暂停、单步、计时与保存恢复 |
| editor_level_3_integration | 通过 | 物理关卡同流程集成 |

独立测试程序报告：ECS **1,111,120次检查**，物理 **19,935次检查**，编辑器控制/数据 **33次检查**。大量次数来自随机操作后逐项对照，不是设计了一百多万个互不相同的用例。

### 20.2 比单测数量更值得讲的验证方法

Pool用3000次固定种子随机操作对照普通映射，每步比较内容与存在性；Registry用1200次随机生命周期和组件操作对照参考模型，检查系统集合无重复、无漏项。参考模型采取简单直接方式，避免把待测算法原样再写一遍。

物理宽阶段用200个固定种子随机形状，暴力检查所有实际窄阶段命中都在候选里；这验证不漏候选，允许假阳性。30/60/144FPS的一秒恒速测试验证固定步机制；10秒静止测试覆盖一个重力支撑场景，而非所有复杂堆叠。

序列化测试检查中文、引号、反斜杠、换行、BOM、高位mask、错误版本、重复tag、非法数值、无限循环拒绝、缺资源/脚本不替换，以及旧文件保护。它测试的是具体失败边界，不是只看Encode函数能否返回字符串。

### 20.3 UI测试为什么有额外价值

控制器Save正确，不保证按钮会传对路径；Pick正确，不保证面板捕获状态允许它执行。UI测试建立SDL软件渲染和ImGui输入，实际点按钮、输路径、拖对象与字段，检查组件和文件结果，覆盖从输入到业务调用的连接。

dummy驱动不操作用户桌面，但也不等于人眼视觉和手感验收。测试导出截图证明渲染调用成功，不证明字体、遮挡和交互体验完美。本次没有重新进行人工视觉验收或性能基准。

### 20.4 不被测试结论误导

固定种子测试可复现但不能证明所有状态；正常路径通过不覆盖内存不足；预检错误保持对象不代表Spawn异常也能回滚；几何支持负缩放不代表显示支持；原关卡冒烟通过不代表玩法行为完全没变化。

面试时回答“测了什么、怎么构造预期、结果是什么、还有什么没测”，比单独报一个大数字更可信。

<a id="chapter-21"></a>

## 21. 六十个高频问题：先回答，再接追问

答案是口头组织方式，原理见前文。练习时先遮住答案说一分钟，再打开对应源码定位实现；仅背这些短答达不到掌握目标。

### 21.1 项目与架构

**Q01：两分钟介绍项目，重点是什么？**

围绕“实体与数据一致性、固定步模拟、编辑持久化”三条主线介绍，举编辑器删除组件仍能安全运行的具体例子。SDL/Lua/ImGui作为依赖说明即可，不要花大部分时间罗列库名。追问：指出一次组件增删经过的三个阶段。

**Q02：一个小引擎有什么技术难度？**

范围不大，但包含跨模块一致性：旧句柄不能误改新对象，组件引用不能在遍历中失效，暂停必须冻结全部游戏计时，非法快照不能先清场景。每个问题都有数据结构、执行约定和测试，技术价值来自解决这些具体约束。追问：选一个问题画出失败路径和修复路径。

**Q03：为什么不用已有ECS和物理库？**

学习目标是理解其核心机制，手写小规模实现便于观察和验证；生产需求复杂时成熟库更适合降低稳定性和维护成本。不要声称自己代码比成熟库全面更优。追问：列出接入成熟库时需要保留的适配层。

**Q04：引擎与游戏逻辑分开了吗？**

机制已有模块划分，但Movement的玩家边界、Damage的阵营标签、开火使用CameraFollow等仍属玩法耦合。后续可将通用运动和具体地图/阵营规则分开；当前没有完整插件化世界。追问：哪个系统最适合先拆，输入输出是什么？

**Q05：为什么选择ECS而不是继承？**

对象可由移动、绘制、碰撞、脚本等能力组合，避免固定继承层次的组合压力，同类数据也便于集中管理。代价是要维护查询缓存、结构提交、句柄和工具支持；少量固定对象用普通对象模型也可能更省成本。追问：给出一个ECS反而增加复杂度的场景。

**Q06：你实现的是自动调度器吗？**

没有。Registry管理存储和系统成员，Game指定执行顺序。脚本先于积分、碰撞先于自动发射，体现人工维护依赖。追问：调整其中两项顺序会改变什么？

### 21.2 ECS与C++

**Q07：实体到底保存什么？**

保存世界身份、槽位、generation、弱生命周期标记及兼容指针，是访问组件的句柄。不是一个拥有全部游戏数据的GameObject，也不是只有一个int。追问：句柄可复制为何不复制组件？

**Q08：generation能解决什么，不能解决什么？**

能区分同槽位的不同生命，防旧句柄操作复用对象；不能单独阻止Registry已析构后的裸指针访问，还需生命周期token。也不固定组件地址，不提供线程同步。追问：如果只把generation加在Entity却不校验，会怎样？

**Q09：weak_ptr为什么指向token而非Registry？**

Registry由Game唯一拥有，不必变为共享所有权。token提供存在性和身份信息，Entity不延长世界生命，保护区即使持有token也能靠owner=null判断世界已死。追问：为何只检查expired不够？

**Q10：什么是sparse set？**

当前池有稠密组件、稠密实体和实体到下标的稀疏数组；通过sparse[e]取得dense位置，删除用末尾补位。它将定位与连续存储结合。追问：画删除中间实体后每个数组的变化。

**Q11：删除真的是O(1)吗？**

池索引与补位操作是常数次，组件移动/析构成本另计；整个实体删除还遍历系统、池和索引。扩展稀疏数组的新增也不是每次严格常数。追问：最高ID很大怎么办？

**Q12：reserve与resize为什么影响组件设计？**

reserve只预留空间，resize增加实际对象并要求相应构造。当前只存活组件，支持非默认构造和move-only类型，避免把容量误当长度。追问：何时引用会失效？

**Q13：为什么移除成员要unordered_map索引？**

系统保存vector便于遍历，同时用完整Entity到位置的哈希映射，避免删除时全表扫描。补位后修正索引，不保持顺序。追问：哈希与相等没有包含相同身份字段会怎样？

**Q14：bitset32限制什么？**

限制进程内注册组件类型数量，不限制实体数量。CAS编号超过上限受控抛错，Clear世界不重置全局类型计数。追问：存档能不能直接写这些编号？

**Q15：为什么系统注册后冻结签名？**

成员列表是查询条件的缓存，随意改条件会与缓存失配。当前要求移除并重注册系统；后注册会匹配已提交实体。追问：未提交新实体是否立即加入？

**Q16：Defer的保护对象是什么？**

保护组件池结构与系统遍历约定，推迟增删/替换组件和索引操作，数值修改仍立即生效。它不是互斥锁，不处理多线程并发。追问：为何退出作用域后Get新组件仍可能失败？

**Q17：为什么析构不直接Update？**

外层可能还在遍历，提交可能抛异常，析构中隐式提交会带来意外失效和异常安全问题。保护对象只调整深度，明确由调用方在安全点提交。追问：嵌套和异常退出如何处理？

**Q18：commands为何是deque<std::function<void()>>？**

便于按请求顺序从头取命令，不要求持有具体组件类型。std::function进行可调用对象类型擦除；deque提供前端弹出。追问：为何添加组件时还要shared_ptr包装值？

**Q19：命令里的shared_ptr会导致所有组件共享所有权吗？**

不会。它服务于C++17 std::function目标可复制的要求，让lambda能携带move-only值，提交后组件移入唯一拥有的池。代价是额外分配与引用计数。追问：如果换move-only callable容器能去掉什么？

**Q20：先排Add再Kill会如何？**

Update销毁优先，使旧身份失效，Add命令执行前校验完整Entity后跳过。即使槽位之后复用，旧命令也不会写新实体。追问：命令提交抛异常后是回滚还是继续保留？

**Q21：Get返回引用安全吗？**

在满足组件存在且相关结构不变的短阶段内安全；扩容、替换、补位都可能破坏地址或身份对应。长期保存句柄并重查，不能跨Update盲目保留引用。追问：const Entity为何还能改组件？

**Q22：多态为什么不全用shared_ptr？**

多态解决接口派发，智能指针选择解决所有权。Pool由Registry唯一拥有所以unique_ptr合适；System目前shared_ptr但不代表有必要的共享业务关系。追问：哪些基类必须有虚析构？

**Q23：组件池如何支持move-only？**

使用reserve和push/move，不默认构造一百个组件，Set/Remove通过移动操作维护稠密数据。仍需要可移动赋值，且对任意抛异常移动未提供强保证。追问：哪个新增分支有局部回滚？

**Q24：为什么只读Registry与只读Entity不同？**

Registry const表示世界只读查询，返回const组件；Entity const表示句柄值不改，仍能修改所指向世界的数据。与const智能指针不等于const pointee类似。追问：做读写依赖调度时还需要什么接口？

### 21.3 时间与物理

**Q25：固定步长和锁帧是一回事吗？**

不是。渲染仍由真实帧间隔驱动，固定累加器每步推进1/120秒，可每帧执行0到8次；16毫秒等待只是主循环限频。追问：144FPS和30FPS时分别怎么推进？

**Q26：暂停为何需要统一时钟？**

物理停止但动画或寿命继续读取真实ticks会出现“暂停后恢复突然过期”。现在主要游戏逻辑用SimulationClock，暂停只处理输入、GUI和ECS提交。追问：加载为何还要恢复模拟时间和各startTime？

**Q27：8步追赶的代价？**

防止卡顿后补算导致继续卡顿，代价是超载时丢弃模拟时间。0.25秒帧间隔上限不等于能补0.25秒。追问：网络锁步需求能直接这样丢时间吗？

**Q28：为什么半隐式欧拉？**

简单低成本，先受力改速度，再用新速度推进位置。适合当前学习规模，但仍是近似，没有声称任意场景严格能量守恒。追问：写出力、重力和阻尼完整更新式。

**Q29：形状为什么要考虑Sprite中心？**

Transform表示左上角，SDL默认绕精灵中心旋转；若物理绕自己的错误原点，显示和碰撞会分离。当前ShapeOf选择相同pivot，再统一缩放旋转。追问：计算第9章的(141,224)例子。

**Q30：为什么OBB还需要AABB？**

AABB成本低适合粗筛，OBB SAT负责排除旋转后的假阳性。宽阶段允许多选，不能漏掉真实接触。追问：宽阶段测试如何构造参考结果？

**Q31：SAT为什么四轴够？**

二维矩形的可能分离轴取两组边法线，分别是双方x/y方向。每轴计算投影半径和中心距离，有一轴分离就退出。追问：包含关系下交集长度为何不是正确推出距离？

**Q32：圆在矩形里面时怎么办？**

最近点等于圆心，不能归一化零向量。选最近出口面，深度包含到出口距离与圆半径。追问：圆圆中心重合采用什么法线约定？

**Q33：冲量分母为什么是逆质量和？**

相同接触冲量对双方速度变化是J/m，两者相对法向速度变化由wA+wB决定。当前只解平移，所以没有角项。追问：等质量完全弹性碰撞手算结果。

**Q34：摩擦系数如何混合？**

当前取几何平均，切向冲量限制在±mu*j，恢复系数则取双方较小值。这是当前材质规则而非唯一正确规则。追问：为何不能声称有完整静/动摩擦模型？

**Q35：为什么还要修位置？**

速度冲量阻止继续接近，但离散步已产生穿透；按逆质量分配位置修正消除重叠。保留0.01容差、每轮修80%减少激烈拉扯。追问：只反转速度为何可能抖动？

**Q36：六次迭代发六次事件吗？**

不发。迭代只解当前子步接触，事件在所有求解结束后每对最多发一次。但一个渲染帧可能多个子步，持续接触仍可反复通知。追问：若要Enter/Stay/Exit需要什么状态？

**Q37：OBB支持是不是意味着能撞得物体旋转？**

不意味着。几何旋转参与检测，响应没有角速度、惯量和接触点力臂。扩展要增加完整角动力学项，不能只在碰撞时修改rotation。追问：Contact当前缺什么关键数据？

**Q38：120Hz能保证高速弹不穿墙吗？**

不能，仍离散检测。需要扫掠检测和碰撞时间，多个目标要选最早命中，不是候选列表第一个。追问：按min.x排序为何不等于沿任意运动方向的先后碰撞？

### 21.4 脚本、渲染和编辑器

**Q39：C++与Lua怎样双向调用？**

C++取ScriptComponent函数执行；Lua经Sol注册函数访问Entity和组件。VM/栈桥接由Lua与Sol提供，游戏API和调度由项目实现。追问：实体失效后Lua保存的句柄会怎样？

**Q40：如何保存Lua函数？**

当前不存函数本体，只存原关卡索引和启用状态，加载从主Lua的Level表取回函数。闭包/全局状态不保存，依赖基础关卡结构。追问：修改原数组顺序会有什么风险？

**Q41：有热重载吗？**

没有完整流程。已有函数来源恢复和启用开关，热重载还需资源跟踪、临时校验、安全替换和状态迁移。追问：重新定义全局函数为何不自动改变所有旧引用？

**Q42：RenderSystem为什么先复制再排序？**

把绘制所需值集中成渲染列表，排序不会改ECS实体列表，访问过程清楚。代价是副本、字符串和临时数组成本；可考虑轻量句柄/命令缓存，但必须保证提交时引用有效。追问：为什么系统列表const视图不等于完全无复制？

**Q43：图元填充如何实现？**

按扫描线，圆用圆方程算左右范围，矩形求与边交点再画水平线。它复用物理形状，Primitive控制外观，Collider不自动可见。追问：半开边规则解决什么？

**Q44：为什么编辑器修改会暂停？**

避免物理或脚本立即覆盖用户刚编辑的数值，也便于观察。但仍需能禁用脚本，因为恢复运行后它会继续写状态。追问：值改变与结构改变分别什么时候生效？

**Q45：鼠标怎样选择旋转对象？**

先转世界坐标，再将点相对中心投影到局部轴，与矩形half比较；圆比较距离平方。有Sprite时按显示矩形，文字不支持画面拾取。追问：重叠对象如何选择最上层，当前有何不足？

**Q46：拖动为什么不会跳？**

按下记录对象位置减鼠标世界位置的offset，拖动时再加回来，保留抓取点。grid吸附对新位置做round，不强制数值框输入。追问：fixed精灵的相机项为什么是0？

**Q47：为什么去掉Sprite还要去掉其他组件？**

动画依赖图集，当前键盘控制还修改Sprite行；删除依赖对象可能产生无效组合。UI主动清理相关行为，Serializer另作组合验证。追问：为什么这还不是通用组件依赖系统？

**Q48：即时模式GUI没有状态吗？**

不是。每帧声明控件，但ImGui内部和RenderGUI成员仍保存焦点、路径、拖动身份等状态。即时模式影响UI构建方式，不意味着不保存交互数据。追问：保存快捷键如何拿到最新路径？

### 21.5 保存、验证和工程判断

**Q49：场景快照为什么用optional？**

精确表达组件是否存在，避免把默认值误当缺失。值数据脱离运行时ID和指针，可先完整验证再Spawn。追问：哪些状态没有被保存？

**Q50：为什么Load先校验再销毁？**

避免错误文件、缺资源或脚本来源让当前成果消失。所有预期验证通过后才替换对象并保留tiles；内存异常仍没有完整回滚。追问：如何扩展临时世界构建后交换？

**Q51：文件小于8MB就安全吗？**

不是。Lua仍可执行循环或分配内存，当前隔离VM、无标准库和指令hook降低风险，8MB只限制源文本。没有执行内存上限或完备沙箱。追问：为什么需要protected_function和字段检查两层？

**Q52：临时文件为什么比直接写好？**

先写完并检查临时文件，再替换目标，失败时尽量保留旧文件。它不等于整个加载过程事务，也不自动保证多写入者和断电下所有情况。追问：固定.tmp名字有何并发问题？

**Q53：怎么支持中文？**

内容用UTF-8、Quote处理特殊字符，读BOM；路径用u8path，Windows替换用宽字符API；UI建立字体纹理前加载中文字体。内容编码、路径编码和字体字形是三件事。追问：为什么字体文件不需复制系统字体进仓库？

**Q54：怎样证明重构的正确性？**

用池和Registry参考模型随机对照、生命周期回归、真实Damage集成、几何与时间测试、控制器和真实UI输入测试。测试覆盖具体不变量，不只看游戏能打开。追问：百万检查为什么不等于百万独立场景？

**Q55：虚拟UI测试和手动验收是什么关系？**

dummy测试能验证控件到数据/文件的连线，手动验收判断字体、遮挡、操作感和玩法体验，两者不能互相完全替代。追问：指出一个只测控制器可能漏掉的问题。

**Q56：性能优化优先做哪里？**

先测候选扫描、接触求解、Lua、渲染准备、文字纹理和分配，再按瓶颈选择。当前未做本次性能基准，所以报告结构变化和潜在收益，不编造加速倍数。追问：哪些计时应排除SDL_Delay？

**Q57：可以直接多线程吗？**

不可以。类型ID原子分配仅解决编号，世界结构、系统共享Transform、事件和Lua都没有完整同步。先定义读写集与阶段依赖，再局部并行，结构命令统一提交。追问：Script系统为何很难直接独立并行？

**Q58：为什么没有做更多功能？**

当前重点是完整且可验证的最小工作流。角动力学、CCD、撤销历史和资源导入分别增加不同状态和约束，应按使用需求扩展。追问：选一个扩展说明会改哪些模块。

**Q59：项目有AI/教程辅助，你如何证明自己掌握？**

如实说明辅助范围，再现场解释核心实现、分析边界、完成一个小改动并验证。提交历史不能替代理解，背一份介绍也不够；应能独立推演句柄复用或结构提交。追问：如果移除Defer，哪个具体测试先出问题？

**Q60：最值得讲的技术收获是什么？**

可以围绕“维护不变量和阶段边界”展开：池、签名、成员索引、句柄和快照必须共同一致；局部可用不等于全链正确。结合自己实际完成的重构或验证经历讲现象、定位、取舍与证据，不把文档里的建议说成已经亲自解决。

<a id="chapter-22"></a>

## 22. 现场推演与改代码练习

这部分决定你能否从“读懂”进入“能独立做”。建议在新分支或副本中练习，先写预期，再修改和测试；以下都是练习设计，未改动本次引擎实现。

### 22.1 五分钟：手写稀疏池Remove

```cpp
bool Remove(int id) {
    if (!Contains(id)) return false;
    auto i=sparse[id], last=data.size()-1;
    if (i!=last) {
        data[i]=std::move(data[last]);
        int moved=denseEntities[last];
        denseEntities[i]=moved;
        sparse[moved]=i;
    }
    data.pop_back();
    denseEntities.pop_back();
    sparse[id]=Missing;
    return true;
}
```

要求解释：删除末尾、仅一个元素、重复删除；为什么组件移动可能抛异常；哪些引用会失效；为什么id不能用dense位置代替。仅把这段默写出来算一半，能画不变量才算完成。

### 22.2 十分钟：预测Defer行为

```cpp
auto e=registry.CreateEntity();
e.AddComponent<TransformComponent>();
registry.Update();
{
    auto guard=registry.Defer();
    e.AddComponent<RigidBodyComponent>();
    // A：HasComponent<RigidBody>()是什么？
    e.RemoveComponent<TransformComponent>();
    // B：GetComponent<Transform>()还能读吗？
}
// C：保护区结束后组件是否自动提交？
registry.Update();
// D：e应属于哪些系统？
```

答案：A=false；B可以，因为尚未提交；C不会；D含RigidBody但不含Transform，因此不满足Movement。再增加e.Kill，解释销毁优先为何使两条结构命令都不写入旧身份。

### 22.3 十分钟：写系统并串到所有入口

新增LifetimeComponent，使任意对象到模拟截止时间后销毁。需要：组件、LifetimeSystem、Game子步顺序、关卡解析、Inspector、SceneEntityData、Encode/Decode、Capture/Spawn及测试。

重点追问：用绝对模拟截止时间还是剩余时间？保存恢复后如何解释？暂停时是否冻结？运行中移除组件怎样更新系统？你能列全链路，就理解了“ECS加组件”以外的工程工作。

### 22.4 十分钟：质量变化的预期

两个动态体mA=2、mB=1，vA=10、vB=0，e=1，n=+x。

wA=0.5、wB=1，vn=-10，j=20/1.5=13.333…；vA'=3.333…，vB'=13.333…。检验动量：2*3.333+13.333≈20；动能：原100，后约100。再把B变Kinematic且速度-4，分析A响应与B保持指定速度的区别。

### 22.5 十分钟：实现对象渐隐

新增颜色alpha随模拟时间下降的行为，先决定它影响Primitive还是Sprite。Primitive可直接改组件color；Sprite需要绘制时设置纹理alpha且避免共享Texture污染其他实体，必须每次明确设置或保存恢复状态。

这个练习能暴露资源共享与实体状态的边界：同一纹理被许多实体引用，不应把每实体透明度只存在全局纹理对象上而不做隔离。

### 22.6 十五分钟：让静止玩家朝最后方向射击

增加Facing/朝向状态，方向键更新朝向，停止只清速度不清Facing。发射读取Facing而非速度符号，必要时归一化；Lua/编辑器和持久化是否需要支持，由需求决定。

验证：四向移动后停止开火、无移动初始方向、重复按键、斜向、多玩家标记。不要只把direction=0时强设向上，那会隐藏原有朝向。

### 22.7 十五分钟：为事件系统加退订

设计稳定subscription ID与connection；对象销毁可断开。分发时退订应标记失效或延迟回收，避免当前遍历容器被销毁；处理总线先析构的情况，connection不能回调悬空总线。

完成标准：订阅两次可分别取消、回调内取消自己、取消后不再调用、总线先销毁、系统移除不留owner悬空。不要只写erase(owner)而忽略分发中重入。

### 22.8 二十分钟：为Transform增加撤销

记录EditorCommand{稳定对象引用、before、after}，一次鼠标拖动从按下到松开合并成一次命令；撤销应用before，重做应用after。新编辑清redo栈，加载/删除后如何使历史有效必须定义。

当前Entity是运行身份，加载会换代，若撤销历史需要跨重建保留，则需新的持久对象ID；否则明确加载清历史。这是持久身份与运行句柄的区别，不能只多放两个vector就称完整撤销系统。

### 22.9 二十分钟：设计更强的加载失败保护

先完成SceneData和资源脚本预检，再在独立临时Registry构建新对象；成功后交换世界或提交替换。但Registry当前禁止移动，Game/System/EventBus/Editor引用关系也绑定旧世界，不能直接std::swap就结束。

可用unique_ptr<World>聚合Registry和相关状态，外部通过World句柄访问，或者预分配/创建成功后再决定旧对象销毁。两条路线对身份失效、事件owner和资源共享各有影响，应先画依赖再改。

### 22.10 不看代码的全链路口述

描述：按空格创建子弹 → 命令排队 → 提交匹配 → 固定步前进 → 世界形状 → 候选过滤 → Contact → trigger → Damage → pending → 句柄失效 → 画面不再绘制。

再描述：暂停编辑对象 → Capture → Encode/Decode → 临时文件 → 修改对象 → Load预检 → 替换非瓦片 →恢复时间/脚本→系统重匹配。每一环都说出数据结构和失败前提。能稳定讲完这两条链，就是较扎实的项目掌握。

<a id="chapter-23"></a>

## 23. 项目讲述、连续追问与七天掌握计划

### 23.1 三十秒介绍

这是一个基于C++17和SDL2的轻量二维游戏引擎项目。当前重点是ECS生命周期和结构变更、固定步长物理，以及可保存加载的场景编辑器。实体采用世界身份和版本句柄，组件用稀疏集存储，遍历中的结构修改通过命令队列提交；Lua负责关卡和行为，ImGui提供对象编辑与暂停单步。项目有算法、生命周期、编辑器和真实UI集成验证。

这段先说项目实现，不替你声称每行都由个人手写。面试官问贡献时，应按实际负责的代码、设计和验证细分回答。

### 23.2 两分钟介绍的结构

**动机和范围：** 希望理解游戏引擎如何组织数据、模拟和工具工作流，因此做了一个能运行小型二维游戏并在运行中编辑对象的实现。

**第一条主线：** 实体不是裸ID，而包含注册表身份和版本，解决ID复用与跨世界误用；组件采用稠密数组和稀疏索引，系统成员维护独立索引；为遍历中删除组件引入Defer和安全点同步，保证池、签名和系统成员一致。

**第二条主线：** 游戏行为统一按120Hz固定步更新，模拟时钟让动画、发射和寿命随暂停冻结。物理用AABB排序扫描，再对OBB和圆做精确检测，以逆质量分配冲量、摩擦和位置修正；响应只覆盖平移，角动力学和CCD仍是明确边界。

**第三条主线：** 编辑器支持对象选取、拖动、组件编辑和暂停单步。保存用独立值快照，加载先校验格式、组件组合、资源和脚本来源，再替换对象；文件通过临时写入后替换保护上一份内容。

**验证与下一步：** 本地九项测试覆盖ECS参考模型、几何、真实伤害、编辑器控制、UI操作和两关集成。接下来依据实际需求优先完善事件退订、资源失败处理、撤销历史或碰撞能力，并先测量再讨论性能数字。

### 23.3 简历写法：突出可解释的成果

**C++二维游戏引擎与场景编辑器｜C++17、SDL2、Lua/Sol、ImGui**

- **ECS与生命周期：** 采用带注册表身份和版本号的实体句柄、稀疏索引组件池与系统成员索引；通过延迟结构命令和脏实体重匹配，处理遍历中的组件增删、槽位复用及场景重建。
- **物理与模拟：** 集成120Hz固定步长与统一模拟时钟，支持OBB/圆形检测、排序扫描宽阶段、平移冲量、摩擦、穿透修正、触发器和碰撞过滤。
- **编辑与持久化：** 实现对象拾取拖动、组件检查、暂停单步、Lua来源恢复和对象快照；采用加载预检与临时文件替换，支持中文路径和配置往返。
- **验证：** 建立ECS随机参考模型、物理与真实伤害回归、编辑器和UI集成测试，覆盖句柄失效、成员同步、暂停计时及保存加载。

应按实际参与范围选择和调整条目，不强行把全部技术塞进一段；日期保留真实项目时间。如果后续重构发生在原简历日期之后，可如实更新为持续维护时间。没有基准就不写加速倍数，没有独立完成就不写全部独立原创。

### 23.4 五条连续追问路线

| 起点 | 第一层 | 第二层 | 第三层 | 你应能给出的证据 |
|---|---|---|---|---|
| ECS | 为什么组合 | 怎么存组件 | 删除引用怎么办 | 三数组、Defer与随机对照 |
| 句柄 | 为什么generation | Registry析构怎么办 | 清空复用与回绕 | token、Clear和退休槽位 |
| 物理 | OBB怎么检测 | 法线怎么定 | 响应为何不旋转 | SAT推导、Contact和逆质量公式 |
| 编辑器 | 怎么选对象 | 多个重叠怎么选 | 加载后选择为何失效 | Pick、z策略、完整句柄 |
| 场景文件 | 为什么值快照 | 错误文件如何保护 | 任意失败能回滚吗 | Decode/Restore顺序与保证边界 |

不要把每条停在“用了某个模式”；要推进到一个数据结构、一个函数和一个边界场景。

### 23.5 四个可深入讲的工程案例

**案例A：旧句柄改了新对象。** 现象是删除后相同ID复用，旧脚本引用仍操作新实体；原因是存储槽位被当成完整身份。设计加入world和generation，token负责世界寿命；验证旧句柄访问、跨表和清空后复用。代价是句柄更大、检查更多。

**案例B：运行时组件删除破坏遍历。** 原来直接修改池导致引用搬移、系统成员过时；用Defer保护、FIFO命令、dirty同步，销毁优先取消旧命令。验证嵌套、异常退出、move-only值和大量排队。代价是提交前新组件不可见，命令有分配成本。

**案例C：暂停只停物理，动画和子弹还在走。** 原因是时间源分散；用SimulationClock统一业务计时，固定步控制推进，暂停清积压、单步只消费一次。验证真实等待后快照不变，以及发射/动画/寿命冻结。代价是需要迁移所有游戏时间读取，静态单时钟限制多世界。

**案例D：文件错了，当前场景不能先丢。** 区分DTO校验与资源脚本环境校验，全部通过后再替换非瓦片对象；保存先临时写后替换。验证错误版本、非法值、缺纹理、坏脚本索引和失败旧文件保留。边界是后半段分配异常不强回滚。

如果这些是阅读或辅助重构中才理解的，就以“当前设计这样解决”“我在复盘中验证”叙述；只有真实亲历的定位过程才说“当时我发现并定位”。

### 23.6 面对“这不就是调库”的回答

可以承认图形、语言和UI依赖成熟库，再具体说明自己掌握的接口边界。例如：SDL不会替项目处理组件引用稳定性；ImGui不会自动解决加载时旧句柄失效；Lua不会替Registry校验世界身份；数学库不会提供当前游戏的提交和事件语义。

随后挑一个自己能独立解释的实现，现场画结构和失败场景。技术深度通过细节呈现，不靠否认第三方依赖。

### 23.7 七天计划：每一天必须有可检查产出

| 日程 | 阅读范围 | 动手与口头产出 |
|---|---|---|
| 第1天 | 第1—3章，Main/Game/Entity | 画主循环和身份模型，不看稿讲两分钟 |
| 第2天 | 第4—7章，ECS.h/.cpp | 手写Remove，推演Defer和系统匹配，跑ecs_tests |
| 第3天 | 第8—11章，Physics与Clock | 手算SAT/冲量，解释暂停与单步，定位物理测试 |
| 第4天 | 第12—14章，各系统与Lua | 口述子弹闭环、动画和渲染顺序，解释三个边界 |
| 第5天 | 第15—16章，Editor与Serializer | 新建、编辑、保存、加载，画预检与替换流程 |
| 第6天 | 第17—20章 | 选一个小改动练习，写预期和验证结果；准备三个取舍 |
| 第7天 | 第21—23章 | 45分钟模拟面试：介绍5分钟、三主线各10分钟、现场题10分钟 |

只有一天时优先：主循环、句柄、稀疏池、Defer、系统匹配、固定步与时钟、SAT/冲量、编辑保存加载，以及自己贡献。不要用最后几小时背第三方库不同版本的细枝末节。

### 23.8 掌握程度验收

用四级评价：0=没印象；1=能叫出名词；2=结合源码解释；3=能分析新场景并提出验证。以下每条都应至少达到2，三条主线达到3。

- [ ] 画Entity、Slot、RegistryLifetime，解释所有身份判断。
- [ ] 画三数组和系统成员索引，手写补位删除。
- [ ] 区分数值修改、结构修改、逻辑删除和实际清理。
- [ ] 推演保护区嵌套、异常退出、命令取消和提交中断。
- [ ] 解释dirty同步、冻结签名、后注册和Clear。
- [ ] 手算固定步数量、冲量、旋转中心和一个SAT轴。
- [ ] 解释真实时间、模拟时间、动画和子弹寿命。
- [ ] 串起Lua双向调用、函数来源与VM生命周期。
- [ ] 指出绘制与拾取的层级策略和未覆盖边界。
- [ ] 从UI点击说到数据修改、命令提交和文件替换。
- [ ] 区分错误输入预检、原子文件替换和完整事务。
- [ ] 说明每层测试的预期模型、结果和局限。
- [ ] 如实陈述个人贡献、参考和辅助范围。

读完不是终点。能在空白纸上画两条闭环，并独立完成第22章一到两个练习，是更可靠的面试准备标准。

<a id="chapter-24"></a>

## 24. 源码导航和断点建议

以下链接指向本机实际文件。章节标题给出主题，符号名方便搜索；复制到其他电脑后需调整绝对路径。

| 文件 | 建议先找的符号 | 你要回答的问题 |
|---|---|---|
| [Main.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Main.cpp) | main | 参数怎样选择关卡和编辑模式？ |
| [Game.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Game/Game.h) | 成员声明 | 所有权和逆序析构依赖？ |
| [Game.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Game/Game.cpp) | Setup/SimulateStep/Update/Render | 哪些阶段受保护，何时提交？ |
| [ECS.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.h) | Pool/DeferredChanges/AddComponent/AddSystem | 存储、排队和模板接口？ |
| [ECS.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/ECS/ECS.cpp) | IsValid/DestroyEntity/Update/Clear | 身份、销毁与重新匹配？ |
| [EventBus.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/EventBus/EventBus.h) | EventCallBack/EmitEvent | 类型擦除与同步调用？ |
| [SimulationClock.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Time/SimulationClock.h) | Set/Advance/Milliseconds | 暂停如何影响所有计时？ |
| [Physics2D.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/Physics2D.h) | MakeShape/Candidates/BoxBox/ResolveVelocity/FixedStepClock | 公式怎样变成代码？ |
| [EntityShape.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Physics/EntityShape.h) | ShapeOf/BodyOf | ECS如何适配算法？ |
| [ColliderComponent.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Components/ColliderComponent.h) | shape/isTrigger/category/mask | 几何、过滤和事件的区别？ |
| [RigidBodyComponent.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Components/RigidBodyComponent.h) | InverseMass/AddForce/ApplyImpulse | 力与冲量如何作用？ |
| [LevelLoader.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Game/LevelLoader.cpp) | LoadLevel/SceneObjectComponent | 配置如何变对象和脚本来源？ |
| [ScriptSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/ScriptSystem.h) | CreateLuaBindings/Update | Lua与组件怎样互相调用？ |
| [MovementSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/MovementSystem.h) | Update | 积分与游戏边界如何混合？ |
| [CollisionSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/CollisionSystem.h) | Update | 接触何时求解、何时发事件？ |
| [DamageSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/DamageSystem.h) | onCollision | 伤害有哪些组件前提？ |
| [ProjectileEmitSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/ProjectileEmitSystem.h) | OnKeyPressed/Update | 新组件为何必须局部配置？ |
| [ProjectileLifecycleSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/ProjectileLifecycleSystem.h) | Update | 寿命与暂停的关系？ |
| [KeyboardControlSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/KeyboardControlSystem.h) | OnKeyPressed | 事件、速度与精灵行如何关联？ |
| [AnimationSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/AnimationSystem.h) | Update | 循环与非循环如何计算？ |
| [RenderSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/RenderSystem.h) | RenderableEntity/Update | 排序、裁剪、src/dst和开销？ |
| [RenderPrimitiveSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/RenderPrimitiveSystem.h) | Update | 扫描线如何填充形状？ |
| [RenderColliderSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/RenderColliderSystem.h) | Update | 调试形状为何与检测一致？ |
| [CameraMoventSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/CameraMoventSystem.h) | Update | 跟随、边界与编辑平移？ |
| [RenderTextSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/RenderTextSystem.h) | Update | 每帧文本纹理怎样产生？ |
| [RenderHealthBarSystem.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/RenderHealthBarSystem.h) | Update | 生命值如何变成视觉？ |
| [SceneEditor.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.h) | Request/Selected/ConsumeStep | 控制器保存哪些状态？ |
| [SceneEditor.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneEditor.cpp) | Capture/Spawn/Restore/Pick | 编辑与恢复如何贯通？ |
| [SceneData.h](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneData.h) | SceneEntityData | 哪些值可保存、哪些不可？ |
| [SceneSerializer.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Editor/SceneSerializer.cpp) | Quote/Encode/Decode/Save | 校验与替换保证到哪一层？ |
| [RenderGUISystem.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Systems/RenderGUISystem.cpp) | ComponentPanel/Inspector/Viewport | 控件怎样改变世界？ |
| [AssetStore.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/AssetStore/AssetStore.cpp) | AddTexture/ClearAssets/GetTextureIds | 资源存在与生命周期？ |
| [Logger.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/src/Logger/Logger.cpp) | Log/Err | 日志存储和开销？ |
| [CMakeLists.txt](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/CMakeLists.txt) | add_test/ns2d_game_core | 独立算法与集成如何构建？ |
| [ecs_tests.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/tests/ecs_tests.cpp) | LifetimeRegression/DeferredRegression/RegistryStressRegression | 不变量怎样验证？ |
| [physics_tests.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/tests/physics_tests.cpp) | main | 几何和响应的预期怎样构造？ |
| [physics_ecs_tests.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/tests/physics_ecs_tests.cpp) | main | 一颗子弹为什么只伤害一次？ |
| [scene_editor_tests.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/tests/scene_editor_tests.cpp) | main | 快照与错误输入如何测试？ |
| [scene_editor_ui_tests.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/tests/scene_editor_ui_tests.cpp) | click/拖动断言 | UI真实连接是否工作？ |
| [scene_editor_game_tests.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/tests/scene_editor_game_tests.cpp) | main | 暂停、单步和真实关卡恢复？ |
| [physics_game_smoke.cpp](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/tests/physics_game_smoke.cpp) | main | 原关卡运行链如何验证？ |

断点建议按一个对象缩小范围：CreateEntity → AddComponent/commands → Update → AddEntityToSystems → Script → Integrate → Collide → Damage → DestroyEntity。编辑链则从Viewport/ComponentPanel到Request、Restore和Spawn。观察完整句柄、签名、稀疏位置、系统indices和模拟时间，避免只盯画面结果。

补充项目说明：[ECS设计与验证](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/docs/ECS设计与验证.md)、[场景编辑器使用与设计](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/docs/场景编辑器使用与设计.md)、[物理系统说明](C:/Users/28031/Downloads/study/ns2d/ns2dEngine-master/docs/物理系统说明.md)。README部分旧描述仍有“热重载”“纯实体只有ID”等概括，面试以实现为准。
