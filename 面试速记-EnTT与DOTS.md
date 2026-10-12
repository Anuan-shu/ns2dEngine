   # 速记卡：EnTT 与 DOTS（配合 面试复习-ns2dEngine.md 第 2.7 节）

## 一图定位

- 我的引擎 = 稀疏集简化版：`vector` + 双向 `unordered_map`，遍历系统实体列表逐实体随机取组件
- EnTT = 稀疏集工业版：**平行数组 + 分页 sparse 表 + view 顺序扫最小组**
- DOTS = **archetype**：按组件组合分 16KB chunk（chunk 内 SoA），迭代全顺序

## EnTT 五个关键词

1. **dense 与 components 平行**：下标 i = 第 i 个实体 = 它的组件 → 顺序扫 dense = 实体和组件同序流过内存
2. **分页 sparse 替代哈希**：`sparse[id/4096][id%4096]`，一次下标访问；页按需分配，空间换时间
3. **view 最小池驱动**：多组件查询选元素最少的池顺序扫，其他池 sparse 判存在 + 按需随机取
4. **group 已弃用**：曾把多池重排成同序实现全顺序迭代，维护成本太高被砍 → "次要池随机访问实践中够用"的工程权衡案例
5. **id+version 打包**：32 位 = 序号 + 版本（默认约 20+12 位）。id 复用时 version+1，旧句柄访问判无效
   → 我的 `freeIds` deque 没版本号，**悬空句柄可能静默操作到复用后的新实体**（自曝短板题的标准答案）

## DOTS 五个关键词

1. **Archetype = 组件组合**；相同组合的实体同住
2. **Chunk 16KB，chunk 内 SoA**：查什么组件只流过什么数组，无关数据不进缓存
3. **EntityQuery 迭代**：匹配 archetype → 逐 chunk → chunk 内顺序扫，全程线性可 SIMD
4. **加组件 = 搬家**：拷贝到目标 archetype 的 chunk，原位 swap-remove 补坑 → 加删贵、迭代快，适合组合稳定的海量实体
5. **EntityCommandBuffer**：迭代中禁结构变更，录进 ECB 帧末统一回放
   → **金句：我的 entitiesToBeAdded/Killed 帧末统一处理 = 手写简化版 ECB**
6. 并行三件套：**ECS（布局）+ Job System（读写依赖声明→自动并行）+ Burst（SIMD 编译）**
7. 软肋：**archetype 碎片化**（组合太多→chunk 半满）；补丁：SharedComponentData（同 shared 值同 chunk，渲染合批）、Enableable（开关不算结构变更）

## 选型一句话

> sparse set 加删组件 O(1)、迭代有间接开销；archetype 迭代全顺序、加删要搬家。
> EnTT 选 sparse set 为通用灵活；Unity 选 archetype 因目标场景（十万级实体、组合稳定）迭代性能压倒一切。

## 30 秒口径

> "我实现的是稀疏集流派，EnTT 同款思路的简化版。差在两处：EnTT 用分页稀疏数组做实体映射（一次下标访问替代我的哈希），view 直接顺序扫最小组的 dense 数组（我是遍历实体列表随机取）。再往上是 DOTS 的 archetype 流派：按组件组合分 chunk、chunk 内 SoA，迭代全顺序可 SIMD，代价是加删组件跨 archetype 搬家。我帧末统一处理实体增删，本质上就是手写版的 EntityCommandBuffer。"
