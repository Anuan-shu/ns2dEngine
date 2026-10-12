# ns2dEngine 面试复习提纲

> 基于实际代码梳理，所有结论都能在源码里找到。文件路径都标了，明早面试前可快速翻代码对证。

---

## 0. 30 秒项目介绍（背熟）

> "我用 C++17 和 SDL2 从零写了一个**数据驱动**的 2D 游戏引擎。核心是手写的 **ECS 架构**：组件用稀疏集（sparse set）池存储、bitset 签名做系统匹配、实体增删延迟到帧末统一处理。各系统之间通过**类型擦除的事件总线**解耦——比如碰撞系统只检测 AABB 并抛事件，伤害系统订阅事件再扣血。关卡、实体、组件参数全部由 **Lua 脚本配置**（通过 sol2 绑定），资源由 AssetStore 统一池化管理，还集成了 **ImGui** 做运行时调试（可视化碰撞盒、查实体属性）。"

技术栈一句话：C++17 / SDL2(image+ttf) / Lua 5.3 + Sol2 / GLM / Dear ImGui。

---

## 1. 主循环与帧率控制（src/Game/Game.cpp）

```
Run() → Setup() → while(isRunning) { ProcessInput → Update → Render } → Destroy
```

- **三大核心模块**都是 `std::unique_ptr` 成员：`Registry`（ECS 注册表）、`AssetStore`（资源池）、`EventBus`（事件总线），外加一个 `sol::state lua`。
- **帧率上限 60**：`MILLISECS_PER_FRAME = 1000/60`。每帧开头算 `timeToWait = 帧时长 - 上一帧以来耗时`，`SDL_Delay(timeToWait)` 挂起等待。
- **deltaTime**：`(SDL_GetTicks() - millisecsPreviousFrame) / 1000.0`，随速度乘进去（`position += velocity * deltaTime`），保证不同帧率下移动速度一致。
- ⚠️ 注意一个细节（可能被追问）：deltaTime 是在 Delay **之后**算的，所以它包含了等待时间，每帧约恒定 16.67ms。这是"锁帧步进"的简化做法；更严谨的是 **fixed timestep + accumulator（累积器）** 模式：物理用固定步长多次步进，渲染做插值。
- Update 内部顺序（背下来）：`eventBus->Reset()` → 相关系统订阅事件 → `registry->Update()`（处理延迟的实体增删）→ 各逻辑 System 按序 Update → Render 阶段各渲染 System。

---

## 2. ECS 核心（最重要，src/ECS/ECS.h / ECS.cpp）

### 2.1 三元组职责
| 概念 | 本质 | 代码 |
|---|---|---|
| Entity | **只有一个 int id + Registry 反向指针**，不含任何数据和逻辑，值语义可随意拷贝 | `class Entity` |
| Component | **纯数据结构**（Transform/RigidBody/Sprite/BoxCollider/Health/Animation/ScriptComponent...） | `src/Components/` |
| System | 纯逻辑，声明自己关心哪些组件（签名），只处理满足签名的实体 | `src/Systems/` |

**为什么用 ECS 不用 OOP 继承**（必考）：
1. 组合优于继承——传统"玩家类、敌人类都继承 GameObject"会导致类爆炸（会飞的和会射的组合爆炸）；
2. 继承树难以动态增删能力，ECS 里"给实体加个组件"就换了行为；
3. 数据和逻辑分离，同类组件在内存中连续，**缓存命中率高**（数据导向设计）。

### 2.2 组件 ID 与签名
- `Signature = std::bitset<32>`（MAX_COMPONENTS=32，每个实体/系统一个 bitset）。
- 组件 ID 用**函数内 static** 分配：`Component<T>::GetId()` 里 `static auto id = nextId++;` —— 首次实例化该模板时分配，全局唯一。
- ⚠️ 可被追问的点：C++11 起函数内 static 初始化是线程安全的（magic static），但 `nextId++` 本身**不是原子操作**，多线程并发首次调用会竞态。改进：编译期注册或 `std::atomic`。

### 2.3 系统匹配规则（必背一行）
```cpp
bool isInterested = (entitySignature & systemSignature) == systemSignature;
```
即：**实体拥有的组件必须是系统要求的超集**（系统要求 Transform+RigidBody，实体有 Transform+RigidBody+Sprite 也算）。这是位运算做子集判断。

### 2.4 组件池：稀疏集 Sparse Set（手写核心，重点复习）
`Pool<T>` 用双向映射 + 紧凑数组：
- `data: vector<T>` —— 紧凑存储，缓存友好
- `entityIdToIndex: unordered_map<int,int>` —— 实体号→数组下标（稀疏数组）
- `indexToEntityId: unordered_map<int,int>` —— 反向映射

**删除是 O(1) 的交换删除**（必会手写）：
```cpp
void Remove(int entityId) {
    int indexOfRemoved = entityIdToIndex[entityId];
    int indexOfLast = size - 1;
    data[indexOfRemoved] = data[indexOfLast];   // 尾元素补位，保持紧凑
    // 更新两个方向的映射，删掉两个 map 里的旧条目，size--
}
```
不用 vector::erase（O(n) 搬移），这是稀疏集的精髓。

### 2.5 延迟实体增删（设计亮点，必考"为什么"）
- `CreateEntity()` / `KillEntity()` 只把实体塞进 `entitiesToBeAdded` / `entitiesToBeKilled` 两个 `set<Entity>`，**不立即生效**；
- 每帧 `Registry::Update()` 里统一：新增的实体加入各系统、销毁的实体移出系统、清签名、清组件池、回收 id、清 tag/group。
- **为什么**：系统正在迭代实体列表时，如果事件回调里直接增删实体（如 DamageSystem 处理碰撞时 Kill 弹丸、ProjectileEmitSystem 迭代时生成新弹丸），会导致**迭代器失效 / 帧内状态不一致**。延迟处理保证一帧内数据结构稳定。
- **id 回收**：`freeIds`（deque，O(1) pop_front）复用被销毁的 id，防无限增长。

### 2.6 Tag 与 Group
- Tag 一对一（`player`），Group 一对多（`enemies` / `projectiles` / `obstacles` / `tiles`），都基于 unordered_map。用于伤害判定和移动逻辑里的阵营判断。

### 2.6.1 为什么我的引擎"结构连续但访问不缓存友好"（白话版，防止被问懵）

**核心：缓存友好看的是访问顺序，不是存放位置。"连续存放"是前提，"顺序访问"才是兑现。**

- CPU 按 64 字节 cache line 整块搬内存；预取器只认"地址等步长递增"（顺序扫数组）。
- 顺序访问 ≈ 一直命中（~4 周期）；随机访问 ≈ 一直 miss（~200 周期，差 50 倍）。
- 我的系统这样迭代：`for (entity : GetSystemEntities())` → 每次 `GetComponent` = **unordered_map 哈希（随机 miss 一次）+ data[随机下标]（再 miss 一次）**。data 数组连续，但访问是 412→37→880→105 地乱跳，预取器失效，搬进缓存的邻居数据全浪费。
- 顺序错位的三个原因：
  1. 系统实体列表用 remove_if+erase 删除（**保序**），池用 swap-remove（**主动乱序**），两套机制无同步，弹丸生死几秒后就是随机置换；
  2. 一个实体要摸多个池（Transform + RigidBody），各池成员不同、乱序各异，**结构上不可能都对齐**；
  3. 哈希间接层是每次访问的固定税，即使前两条碰巧躲过也躲不过它。
- 没白做的部分：工作集小（只加载用到的池，不拖无关字段=SoA 收益）、紧凑无洞。
- 正确姿势：**循环入口从"实体列表"换成"池的 dense 数组本身"**——`for i in 0..size` 顺序扫 data，这就是 EnTT view 的本质。
- 一句话："存储结构没问题，问题在循环的驱动入口选错了——从实体列表进就是随机访问，从池数组进就是顺序访问。"

### 2.7 加分项：ECS 两大流派（能讲出=深刻理解）
- **Sparse set 型**（你的实现、EnTT 同款）：每种组件一个池，同类型组件内存连续；加删组件 O(1)，但系统迭代时要跳跃访问多个池。
- **Archetype 型**（Unity DOTS / flecs）：按组件**组合**分块存储，同组合实体完全连续，迭代最快，但增删组件要搬实体到别的块。
- 一句话：读多写少选 archetype，组件组合变化频繁选 sparse set。

---

## 3. 事件总线（src/EventBus/EventBus.h）

**发布-订阅 + 类型擦除**，本质是手写了一个 `std::function`：

- 接口基类 `IEventCallBack`：虚函数 `Call(Event&)`；
- 模板子类 `EventCallBack<TOwner, TEvent>`：持有 **成员函数指针** `void (TOwner::*)(TEvent&)` + 对象指针，`Call` 里 `static_cast<Event&>(e)` 向下转型成具体事件再 `std::invoke`；
- 存储：`map<type_index, list<unique_ptr<IEventCallBack>>>` —— **事件类型作 key，该事件的回调链表作 value**。

调用链（以碰撞为例）：
```
CollisionSystem 检测到 AABB 重叠
  → eventBus->EmitEvent<CollisionEvent>(a, b)
    → 遍历该事件的 handler 链表逐个同步执行
DamageSystem::onCollision   （订阅者1：扣血/击杀）
MovementSystem::OnCollision （订阅者2：敌人撞障碍物反弹+翻转贴图）
```
**价值**：CollisionSystem 完全不知道伤害系统的存在，新增行为只需新系统订阅，**开闭原则**。

⚠️ 两个可讲的取舍（面试官问"有什么不足"时用）：
1. `Update()` 里每帧 `eventBus->Reset()` 清空订阅再让 4 个系统重新 Subscribe——防悬挂指针的偷懒做法，代价是每帧重复注册。改进：Setup 时订阅一次，系统析构时退订。
2. `EmitEvent` 是**同步立即派发**且派发中直接改 handler 链表不安全（handler 里再 Emit 同类事件 = 边迭代边插入）。改进：事件先进队列，帧内固定点统一 flush。

---

## 4. 碰撞检测（src/Systems/CollisionSystem.h）

- **AABB 判定**（要能默写）：
```cpp
aX < bX + bW && aX + aW > bX &&
aY < bY + bH && aY + aH > bY
```
即 x 区间和 y 区间都重叠。碰撞盒位置 = `transform.position + collider.offset`。
- 双重循环 **内层 j 从 i 起步**：避免 (a,b) 和 (b,a) 重复检测一对。
- 复杂度 **O(n²)**——必被追问怎么优化：
  1. **空间划分**：均匀网格（space hashing）/ 四叉树，只和邻近实体做窄相；
  2. **扫掠剪枝** Sweep & Prune：按 x 排序后只比较区间重叠的；
  3. 概念上讲 **broad phase（粗筛）+ narrow phase（精测）** 两阶段。

---

## 5. 渲染管线（src/Systems/Render*System.h）

- **相机**就是一个 `SDL_Rect`（世界坐标的观察窗口）。**世界坐标→屏幕坐标 = position - camera.x/y**；`isFixed` 的 UI 精灵不减相机偏移（固定屏幕位置）。
- RenderSystem 流程：收集 → **视锥剔除**（实体整体在相机外则跳过，isFixed 除外）→ **按 zIndex 排序**（std::sort）→ `SDL_RenderCopyEx`（srcRect 从精灵图集取帧、dstRect 定位缩放、支持旋转和翻转）。
- 图层顺序：清屏(深灰) → 精灵 → 文字 → 血条 → [debug: 碰撞盒 + ImGui 面板] → `SDL_RenderPresent`（**双缓冲**，后台画完一次性呈现，防撕裂）。
- 动画（AnimationSystem）：`currentFrame = (now - startTime) * frameSpeedRate / 1000 % numFrames`，然后 `srcRect.x = currentFrame * frameWidth`——**帧动画的本质就是移动源矩形在精灵图集上的位置**。
- 相机跟随（CameraMovementSystem）：`camera = 玩家位置 - 半屏`，再 clamp 到 `[0, 地图边界]`。
  ⚠️ README 写了"平滑跟随"但这版是硬对齐——主动讲出改法反而加分：`camera.x += (targetX - camera.x) * lerp系数 * deltaTime`（线性插值平滑）。
- ImGui：按 D 切换 debug 模式，实时画 AABB 绿框、列出实体组件。

---

## 6. Lua 脚本集成（src/Game/LevelLoader.cpp + src/Systems/ScriptSystem.h）

- `sol::state` + `open_libraries(base, math, os)`；LevelLoader 先 `load_file` 校验脚本合法性，再 `script_file` 执行，然后读全局表 `Level`。
- **数据驱动关卡**（level2.lua 结构）：
  ```lua
  Level = {
    assets = { {type="texture", id="tank-texture", file="...png"}, ... },
    tilemap = { map_file, texture_asset_id, tile_size, scale, num_cols, num_rows },
    entities = { { tag="player", group="...", components = { transform={...}, sprite={...} } } }
  }
  ```
  每个字段用 `sol::optional<sol::table>` 判存在、`get_or(默认值)` 容错——**加组件不用重编译 C++，改 lua 即改关卡**。
- Tilemap：读 `.map` 文本文件，每格 2 个数字字符索引 tileset 图集，为每格生成一个 tile 实体（分组 `tiles`）。
- **逻辑脚本化**：`ScriptComponent` 持有 `sol::function`，ScriptSystem 每帧调 `func(entity, deltaTime, elapsedTime)`；
- **C++→Lua 绑定 API**：`new_usertype<Entity>("entity", "get_id", ..., "destroy", &Entity::Kill, ...)` + `set_function("set_position", ...)` 等，且每个函数都先 `HasComponent<T>()` 防御性检查再操作。
- **价值**：数据与逻辑分离、策划可直接改行为、迭代无需重编译（改脚本重启即生效）。

---

## 7. 资源管理（src/AssetStore/AssetStore.cpp）

- 两个 `unordered_map<string, SDL_Texture*> / <string, TTF_Font*>`，以字符串 assetId 为 key，**同一资源只加载一次、统一释放**（防重复加载与泄漏）。
- AddTexture：`IMG_Load`(surface) → `SDL_CreateTextureFromSurface` → `SDL_FreeSurface`（中间 surface 及时释放）。
- 析构里 `ClearAssets()` 调 `SDL_DestroyTexture` / `TTF_CloseFont` —— **RAII**，Game 的 unique_ptr 链保证退出时自动走这条路径。

---

## 8. 项目里体现的 C++ 知识点（面试官借项目考语言）

| 知识点 | 项目里的位置 |
|---|---|
| `unique_ptr` 独占所有权 | Game 的 Registry/AssetStore/EventBus |
| `shared_ptr` + `static_pointer_cast` | 类型擦除容器里的 componentPools / systems（先存成基类接口指针，用时安全转回具体类型） |
| 变参模板 + 完美转发 | `AddComponent(TArgs&&... args)` + `std::forward`，组件构造参数原样转发 |
| 函数内 static（magic static） | `Component<T>::GetId()` 的 `static auto id = nextId++` |
| 纯虚基类 + 虚析构 | `IPool` / `IEventCallBack`（基类接口必须虚析构，否则经基类指针 delete 是 UB） |
| `type_index` / `typeid` 作 map key | systems 表、EventBus subscribers 表 |
| 类型擦除（手写 std::function 思想） | IEventCallBack + EventCallBack<TOwner,TEvent> |
| lambda + erase-remove 惯用法 | `System::RemoveEntityFromSystem` 里 `remove_if` + `erase` |
| `deque` | freeIds，头 pop O(1) |
| `bitset` 位运算 | 签名子集判断 `(a & b) == b` |
| 运算符重载 | Entity 的 `== != < >`（set<Entity> 排序需要） |

---

## 9. "如果重新设计/有什么不足"（必问，主动说显得诚实+有深度）

1. **碰撞 O(n²)** → 空间哈希网格/四叉树做 broad phase；
2. **事件总线每帧清空重订阅** → 一次订阅 + 析构退订；事件改为**入队、帧内固定点统一派发**（还解决"回调中再发事件"的重入问题）；
3. **deltaTime 含 Delay 时间** → fixed timestep + accumulator，物理稳定；
4. **Lua 调用无保护**：`script.func(...)` 不是 `sol::protected_function`，脚本运行时错误会直接崩引擎 → 换 protected 调用 + 错误日志；
5. **单线程** → ECS 天然适合并行：System 间按读写组件声明依赖，无依赖的系统跑 job 系统/线程池（只读组件可分片并行迭代）；
6. **渲染无合批**：逐实体 `SDL_RenderCopyEx` 是逐次 draw call → 纹理图集 + 合批；相同 zIndex 用 `std::sort` 是非稳定排序会闪 → `stable_sort` 或加二级排序键；
7. **RenderSystem 每帧把组件拷贝进临时 vector** → 存引用/索引，避免每帧分配；
8. **组件 32 上限 + 运行时分配 ID** → 编译期注册、上限提高或改动态签名；
9. **AssetStore 细节**：`GetTexture` 用 `operator[]`，查不到会插入 nullptr（应改 `find` 且报错）；无引用计数、无异步加载。

---

## 10. 高频追问速答（考前过一遍）

- **Q: Entity 是什么？** 一个 int id 包装 + 指回 Registry 的指针，轻量值类型；组件数据都存在 Registry 的池里。
- **Q: 系统怎么知道该处理哪些实体？** 每个系统有组件签名的 bitset；实体加组件时签名置位；`(实体签名 & 系统签名) == 系统签名` 即匹配，加入系统的实体列表。
- **Q: 为什么实体销毁要延迟？** 避免系统迭代中修改容器导致迭代器失效，同时保证一帧内逻辑一致性；统一在 Registry::Update 处理。
- **Q: 组件怎么存的？** 每种组件一个 Pool（稀疏集）：紧凑 vector + 实体id↔下标双向 map；删除用尾部交换 O(1)。
- **Q: 事件总线怎么做到泛型？** 类型擦除：虚基类接口 + 模板子类存"对象指针+成员函数指针"，以 `type_index` 为 key 分桶；派发时向下转型回具体事件类型。
- **Q: deltaTime 为什么要乘？** 帧率无关性：速度按"每秒"定义，乘上帧间隔得到本帧位移。
- **Q: Lua 那层解决什么问题？** 数据驱动（关卡/参数热改）+ 逻辑脚本化（行为不重编译）；C++ 通过 sol2 暴露受控 API。
- **Q: 资源怎么防重复加载/泄漏？** AssetStore 以 assetId 为 key 的 map 保证单份；RAII 析构统一释放 SDL 资源。
- **Q: 不用系统实体列表驱动，怎么知道池里哪些下标要处理？**（讲缓存优化后必被反问）
  > 稀疏集一物两用：dense 数组本身就是成员名单（既是索引又是紧凑数组）。单组件系统直接 0..size 顺序扫；多组件系统（需要 A+B）取**最小的池**当驱动顺序扫，其他池用 sparse 索引探测成员资格（一次数组访问，不命中不花钱），命中的才随机取数据 = "主导全顺序 + 次要按需随机"。所以 EnTT 的系统不持有实体列表，view 每次从池结构现场推导交集——顺带消掉了"系统列表与池状态同步"的问题。对应到我的代码：`indexToEntityId` 哈希表换成平行 vector<int> 就是 dense 实体数组，`System::entities` 整个可以删掉。

## 11. 可能被要求手写的代码

1. AABB 碰撞判断（4 个不等式）；
2. 稀疏集交换删除 `Remove(entityId)`；
3. 签名匹配 `(a & b) == b`；
4. EventBus 的 Subscribe/Emit 骨架（成员函数指针 + std::invoke）；
5. 用 unique_ptr 写一个 RAII 资源管理类。

---

## 12. 风险提示（务必注意）

这套架构（Registry/AssetStore/EventBus/LevelLoader/sol2 + 坦克素材）与国外知名的 2D 引擎教学项目结构高度相似，面试官若认出来，可能会刻意深挖细节验证是否真正吃透。所以复习重点是**每一处"为什么这样设计"**（本文档第 2、3、9 节），而不只是"是什么"。答得越深，越不会被质疑。
