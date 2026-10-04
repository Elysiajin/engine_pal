// ============================================================
// pathfinding.h — 寻路系统核心接口
// 依赖：SDK (UNavigationSystemV1, UNavigationPath, FVector, AActor)
//       GameHelper.h (Helper::Try, IsProbablyValidPtr)
// ============================================================
#pragma once
#include <pch.h>
#include <vector>
#include <unordered_map>
#include <queue>
#include <chrono>
#include <cstdint>
#include <cmath>

// SDK 基础类型 (uint8/int32/...) 位于 SDK 命名空间内, 头文件里显式引入别名
using SDK::uint8;
using SDK::uint16;
using SDK::uint32;
using SDK::uint64;
using SDK::int32;
using SDK::int64;

// ------------------------- 基础类型 -------------------------

// 路径点状态
enum class PathWaypointFlags : uint8 {
    None         = 0,
    Water        = 1 << 0,  // 该点位于水中
    OnNavMesh    = 1 << 1,  // 该点有 NavMesh 覆盖
    Jump         = 1 << 2,  // 需要跳跃通过
    Interpolated = 1 << 3   // A* 网格角落插值点
};

// 路径中的单个航点
struct PathWaypoint {
    SDK::FVector            location;             // 世界坐标（厘米）
    float                   costFromStart = 0.f;  // 到达该点的累计成本
    PathWaypointFlags       flags = PathWaypointFlags::None;
};

// 路径计算状态
enum class PathStatus : uint8 {
    Invalid,    // 未初始化 / 失败
    Computing,  // 正在计算中（预留，当前版本同步计算）
    Complete,   // 计算成功
    Partial,    // 部分路径（目标不可达，返回最近可达点）
    Cancelled,  // 被用户取消
    Stale       // 路径已过期（玩家或目标移动超过阈值）
};

// 寻路模式
enum class PathfindingMode : uint8 {
    Auto = 0,       // 自动选择：优先 NavMesh（含起终点投影重试），失败不画假路线
    NavMeshOnly,    // 强制仅 NavMesh
    GridOnly        // 强制仅网格 A*
};

// 一次完整路径结果
struct PathResult {
    uint64_t                    id = 0;          // 全局递增路径 ID
    PathStatus                  status = PathStatus::Invalid;
    SDK::FVector                startLocation;   // 起点的世界坐标（发起计算时的快照）
    SDK::FVector                endLocation;     // 目标的世界坐标（发起计算时的快照）
    SDK::AActor*                targetActor = nullptr; // 目标 actor（仅 FindPathToActor 时有效）
    std::string                 targetLabel;     // 目标名称（界面显示用，如"帕鲁"/"矿石"/"宝箱"）
    std::vector<PathWaypoint>   waypoints;       // 路径点序列（不含起点，第一个是起点后的下一步）
    float                       lengthCm = 0.f;  // 路径总长度（厘米）
    float                       cost = 0.f;      // 路径总成本
    bool                        isPartial = false;
    bool                        isValid = false;
    std::chrono::steady_clock::time_point timestamp;
    int                         recalcCount = 0; // 目标移动触发的重算次数（防目标乱跑无限重算）
    PathfindingMode             mode = PathfindingMode::Auto;   // 创建时的寻路模式（重算沿用）
    bool                        needsFollowUpdate = false;      // 玩家移动触发跟随重算（不限次数）
    std::chrono::steady_clock::time_point lastRecalcTime;       // 上次重算时间（重算节流）
};

// ------------------------- A* 回退网格 -------------------------

// 网格采样模式
enum class GridSamplerMode : uint8 {
    NavMeshOnly,  // 仅使用 NavMesh 寻路，不启用 A* 回退
    Hybrid,       // NavMesh 优先，失败时回退 A*
    GridOnly      // 强制仅使用网格 A*
};

// 网格单元
struct GridCell {
    float   height = 0.f;          // 该单元的地面高度（世界坐标 Z）
    bool    walkable = false;      // 该单元是否可行走（通过 LineTrace 判断）
    bool    sampled = false;       // 是否已采样
    uint8_t costModifier = 100;    // 100=标准, >100=高成本(如水域), <100=低成本
};

// 2.5D 网格采样器（在玩家周围动态采样）
struct GridSamplerConfig {
    float   cellSize = 300.f;          // 每格 3 米（厘米）
    int     gridWidth = 25;            // 总宽度 25 格 → 75 米
    int     gridHeight = 25;           // 总高度 25 格 → 75 米
    float   maxSlopeAngle = 45.f;      // 最大可行走坡度（度）
    float   maxStepHeight = 50.f;      // 最大可行走台阶高度（厘米）
    float   heightSampleOffset = 100.f;// 从地面往上射线的偏移
    int     maxSamplePerFrame = 16;    // 每 Tick 最多采样格数（分帧进行）。
                                       // 注意：每格一次 LineTraceSingle(ProcessEvent+物理查询)，
                                       // 取值过大会造成周期性突发尖峰（旧值 100 → 每 100ms
                                       // 突发 100 条射线，是寻路卡顿的次要来源）。
};

// 网格坐标系：以玩家位置为中心，gridWidth × gridHeight，每个格子 cellSize
// 索引 (x, y) 对应世界坐标：
//   worldX = centerX + (x - gridWidth/2) * cellSize
//   worldY = centerY + (y - gridHeight/2) * cellSize
struct GridSamplerState {
    GridSamplerConfig   config;
    std::vector<GridCell> cells;    // 一维数组，索引 = y * gridWidth + x
    SDK::FVector        center;     // 采样中心（上次采样时的玩家位置）
    float               lastCenterZ = 0.f;  // 上次采样时的中心 Z
    bool                dirty = true;       // 玩家移动超过阈值则需要重新采样
    bool                sampling = false;   // 正在异步采样中
    int                 sampleCursorX = 0;  // 异步采样游标 X（当前采样进度）
    int                 sampleCursorY = 0;  // 异步采样游标 Y（当前采样进度）
};

// ------------------------- PathManager 类 -------------------------

// 寻路请求参数
struct PathRequest {
    SDK::FVector        start;
    SDK::FVector        end;
    SDK::AActor*        targetActor = nullptr;  // 如果是对 actor 寻路
    PathfindingMode     mode = PathfindingMode::Auto;
    bool                allowPartial = true;     // 是否允许部分路径
    float               maxPathCost = 0.f;       // 0 = 不限制
    int                 maxRecalcCount = 3;       // 最大重算次数
};

// 路径管理器核心类
class PathManager {
public:
    // ---- 生命周期 ----
    PathManager();
    ~PathManager();

    // 必须每帧调用（在 Menu::Draw 或 Menu::Loops 中）
    void Tick(float deltaTime);

    // ---- 寻路请求 ----
    // 返回分配的路径 ID（0 表示失败）
    uint64_t FindPathToLocation(const SDK::FVector& start, const SDK::FVector& end,
                                PathfindingMode mode = PathfindingMode::Auto);
    uint64_t FindPathToActor(SDK::AActor* targetActor,
                             PathfindingMode mode = PathfindingMode::Auto);
    // 对周围对象发起寻路（AActor 版本，distanceMeters 仅用于距离阈值判断）
    uint64_t FindPathToNearbyObject(SDK::AActor* targetActor, float distanceMeters);

    // ---- 路径管理 ----
    void CancelPath(uint64_t pathId);
    void CancelAllPaths();
    void RecalculatePath(uint64_t pathId);       // 重新计算（使用当前起终点位置）
    void InvalidatePath(uint64_t pathId);        // 标记为失效（下次 Tick 自动重算或丢弃）
    bool IsPathValid(uint64_t pathId) const;
    const PathResult* GetPath(uint64_t pathId) const;
    PathResult* GetPathMutable(uint64_t pathId);

    // ---- 统计 ----
    int GetActivePathCount() const;
    int GetMaxActivePaths() const { return maxActivePaths_; }
    void SetMaxActivePaths(int maxPaths);

    // 遍历所有活跃路径（供可视化等模块高效迭代，避免按 ID 扫描）
    // 回调接收 (pathId, PathResult&)；返回 true 表示继续，false 提前停止
    template <typename Fn>
    void ForEachActivePath(Fn&& callback) const
    {
        for (const auto& [id, path] : paths_)
        {
            if (path.status == PathStatus::Complete || path.status == PathStatus::Partial)
            {
                if (!callback(id, path))
                    return;
            }
        }
    }
    // 非 const 版本（允许修改路径）
    template <typename Fn>
    void ForEachActivePathMutable(Fn&& callback)
    {
        for (auto& [id, path] : paths_)
        {
            if (path.status == PathStatus::Complete || path.status == PathStatus::Partial)
            {
                if (!callback(id, path))
                    return;
            }
        }
    }

    // ---- 网格采样器 ----
    GridSamplerState* GetGridSampler() { return &gridSampler_; }
    const GridSamplerConfig& GetGridSamplerConfig() const { return gridSampler_.config; }
    void SetGridSamplerConfig(const GridSamplerConfig& cfg);
    void ForceResampleGrid();  // 强制重新采样网格

    // ---- 配置 ----
    void SetRecalcThresholdMeters(float meters);
    float GetRecalcThresholdMeters() const { return recalcThresholdMeters_; }
    void SetThrottleMs(int ms);
    int GetThrottleMs() const { return throttleMs_; }

    // ---- 工具方法 ----
    // 在 NavMesh 上投影点
    static bool ProjectToNavMesh(const SDK::FVector& point, SDK::FVector& outProjected,
                                 const SDK::FVector& queryExtent = SDK::FVector(500, 500, 500));
    // 检查两点之间 NavMesh 是否连通
    static bool IsNavigableBetween(const SDK::FVector& from, const SDK::FVector& to);
    // 获取当前 NavMesh 系统
    static SDK::UNavigationSystemV1* GetNavSys();
    // 获取当前世界对象
    static SDK::UWorld* GetWorld();

    // 单例访问
    static PathManager& Get();

private:
    // ---- 内部实现 ----
    uint64_t nextPathId_ = 1;
    std::unordered_map<uint64_t, PathResult> paths_;
    // LRU 顺序跟踪（按路径 ID 记录最近使用时间戳）
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> pathLru_;
    int maxActivePaths_ = 10;
    int throttleMs_ = 100;               // 默认每 100ms 最多一次计算
    float recalcThresholdMeters_ = 5.f;  // 默认移动 5 米触发重算
    std::chrono::steady_clock::time_point lastComputeTime_;

    GridSamplerState gridSampler_;

    // NavMesh 路径计算
    PathResult ComputeNavMeshPath(const SDK::FVector& start, const SDK::FVector& end,
                                  SDK::AActor* targetActor = nullptr,
                                  float tetherDistance = 0.f);
    // A* 网格路径计算
    PathResult ComputeGridPath(const SDK::FVector& start, const SDK::FVector& end);

    // 网格采样
    // incremental=true 时每帧最多采样 maxSamplePerFrame 格（分帧进行，避免卡顿）；
    // incremental=false 时同步全量采样（用户发起寻路时保证网格完整）。
    void SampleGridAroundPlayer(bool incremental = true);

    // 路径验证
    bool ValidatePathWalkability(const PathResult& path);
    bool CheckWaypointReachable(const SDK::FVector& from, const SDK::FVector& to);

    // 路径精简（去除冗余共线点）
    void SimplifyPath(std::vector<PathWaypoint>& waypoints);

    // 检查路径是否需要重算（玩家位置由调用方传入，避免每条路径重复 ProcessEvent）
    bool ShouldRecalculate(const PathResult& path, const SDK::FVector& playerPos, bool havePlayer);

    // 检查路径是否已存在（相同目标 actor 或终点距离 < 1m）
    bool HasExistingPathToTarget(SDK::AActor* targetActor, const SDK::FVector& targetLocation) const;

    // 取消并移除所有到指定目标的路径（同目标重新寻路时刷新用）
    void CancelPathsToTarget(SDK::AActor* targetActor, const SDK::FVector& targetLocation);

    // 清理最旧的路径（LRU 淘汰）
    void EvictOldestPath();

    // FVector 工具函数
    static float VecDist(const SDK::FVector& a, const SDK::FVector& b);
    static float VecDist2D(const SDK::FVector& a, const SDK::FVector& b);
    // 生成目标的显示名称（路径终点绘制 / UI 列表复用）
    static std::string GetActorDisplayName(SDK::AActor* actor);
    static SDK::FVector VecLerp(const SDK::FVector& a, const SDK::FVector& b, float t);
};