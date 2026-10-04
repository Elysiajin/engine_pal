// ============================================================
// pathfinding.cpp — 寻路系统核心实现
// 包括 NavMesh 寻路（主） + 网格 A* 回退（备）
// ============================================================
#include <pch.h>
#include "Pathfinding.hpp"
#include "CheatState.hpp"
#include "engine/GameHelper.hpp"
#include <cfloat>
#include <climits>
#include <queue>
#include <algorithm>
#include <cmath>
#include <unordered_set>

// 需要 NavigationSystem 的 Params 结构体（手动构造 ProcessEvent 参数）
#include <SDK/NavigationSystem_parameters.hpp>

// 取消 Windows.h 宏，避免与 std::min/std::max 冲突
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace SDK;
using namespace Helper;

// ------------------------- 常量定义 -------------------------

// 最大寻路距离（厘米），超过此距离直接返回 Invalid
static constexpr float kMaxPathDistanceCm = 1000000.0f; // 10km

// 单条路径最大路径点数量
static constexpr int kMaxWaypointsPerPath = 1024;

// 路径完成后超过此时间（秒）自动清理
static constexpr float kPathExpirySeconds = 30.0f;

// 零长度路径阈值（厘米）
static constexpr float kZeroLengthThreshold = 1.0f;

// 检查是否已有路径到同一目标的距离阈值（厘米）
static constexpr float kDuplicateTargetThreshold = 100.0f; // 1m

// 网格采样时离玩家超过此距离需要重新采样（厘米）
static constexpr float kGridResampleThreshold = 400.0f; // cellSize * 2

// A* 搜索最大迭代次数（防止无限循环）
static constexpr int kMaxAStarIterations = 50000;

// 路径点之间最小距离（厘米），小于此距离的相邻点会被合并
static constexpr float kMinWaypointDistance = 50.0f;

// ============================================================
// 工具函数
// ============================================================

float PathManager::VecDist(const SDK::FVector& a, const SDK::FVector& b)
{
    const float dx = a.X - b.X;
    const float dy = a.Y - b.Y;
    const float dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float PathManager::VecDist2D(const SDK::FVector& a, const SDK::FVector& b)
{
    const float dx = a.X - b.X;
    const float dy = a.Y - b.Y;
    return std::sqrt(dx * dx + dy * dy);
}

SDK::FVector PathManager::VecLerp(const SDK::FVector& a, const SDK::FVector& b, float t)
{
    return SDK::FVector(
        a.X + (b.X - a.X) * t,
        a.Y + (b.Y - a.Y) * t,
        a.Z + (b.Z - a.Z) * t
    );
}

// 生成目标的显示名称（路径终点绘制 / UI 列表复用）
// 取 Class 干净短名（去掉 BP_ 前缀 / _C 后缀 / 末尾数字），失败返回 "目标"
std::string PathManager::GetActorDisplayName(SDK::AActor* actor)
{
    if (!actor || !IsProbablyValidPtr(actor) || !actor->Class || !IsProbablyValidPtr(actor->Class))
        return "目标";

    std::string name;
    if (!Helper::Try([&] { name = actor->Class->GetName(); }))
        return "目标";
    if (name.empty())
        return "目标";

    auto stripSuffix = [&](const std::string& suffix)
    {
        if (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            name = name.substr(0, name.size() - suffix.size());
    };
    // 去掉 BP_ 前缀
    if (name.rfind("BP_", 0) == 0)
        name = name.substr(3);
    // 去掉 _C 结尾
    stripSuffix("_C");
    // 去掉末尾数字（实例后缀）
    while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())))
        name.pop_back();
    while (!name.empty() && name.back() == '_')
        name.pop_back();
    if (name.empty())
        return "目标";
    return name;
}

// ============================================================
// 单例 & 生命周期
// ============================================================

PathManager::PathManager()
{
}

PathManager::~PathManager()
{
    CancelAllPaths();
}

PathManager& PathManager::Get()
{
    static PathManager instance;
    return instance;
}

// ============================================================
// 静态工具方法
// ============================================================

SDK::UWorld* PathManager::GetWorld()
{
    return SDK::UWorld::GetWorld();
}

SDK::UNavigationSystemV1* PathManager::GetNavSys()
{
    SDK::UWorld* world = GetWorld();
    if (!world || !IsProbablyValidPtr(world))
        return nullptr;

    SDK::UNavigationSystemV1* navSys = nullptr;
    Helper::Try([&] {
        navSys = SDK::UNavigationSystemV1::GetNavigationSystem(world);
    });
    return navSys;
}

bool PathManager::ProjectToNavMesh(const SDK::FVector& point, SDK::FVector& outProjected,
                                    const SDK::FVector& queryExtent)
{
    SDK::UWorld* world = GetWorld();
    if (!world || !IsProbablyValidPtr(world))
        return false;

    bool success = false;
    SDK::FVector projected{};
    Helper::Try([&] {
        success = SDK::UNavigationSystemV1::K2_ProjectPointToNavigation(
            world,
            point,
            &projected,
            nullptr,                     // NavData
            TSubclassOf<SDK::UNavigationQueryFilter>(),
            queryExtent
        );
    });

    if (success)
        outProjected = projected;
    return success;
}

bool PathManager::IsNavigableBetween(const SDK::FVector& from, const SDK::FVector& to)
{
    SDK::UWorld* world = GetWorld();
    if (!world || !IsProbablyValidPtr(world))
        return false;

    SDK::FVector hitLocation{};
    bool blocked = false;
    Helper::Try([&] {
        blocked = SDK::UNavigationSystemV1::NavigationRaycast(
            world,
            from,
            to,
            &hitLocation,
            TSubclassOf<SDK::UNavigationQueryFilter>(),
            nullptr // Querier
        );
    });

    // NavigationRaycast 返回 true 表示有阻挡（不可通行）
    return !blocked;
}

// ============================================================
// 寻路请求（公有接口）
// ============================================================

uint64_t PathManager::FindPathToLocation(const SDK::FVector& start, const SDK::FVector& end,
                                          PathfindingMode mode)
{
    SDK::UWorld* world = GetWorld();
    if (!world || !IsProbablyValidPtr(world))
        return 0;

    // 检查距离限制
    const float dist = VecDist(start, end);
    if (dist > kMaxPathDistanceCm)
        return 0; // 目标过远

    const auto now = std::chrono::steady_clock::now();

    // 同目标已有路径 → 取消旧路径后重建（刷新语义，不再静默拒绝。
    // 旧实现直接 return 0 是“点击没反应”的来源之一）
    if (HasExistingPathToTarget(nullptr, end))
        CancelPathsToTarget(nullptr, end);

    // 检查路径数量上限
    if (static_cast<int>(paths_.size()) >= maxActivePaths_)
    {
        EvictOldestPath();
        if (static_cast<int>(paths_.size()) >= maxActivePaths_)
            return 0; // 仍超限
    }

    const uint64_t pathId = nextPathId_++;

    PathResult result;
    result.id = pathId;
    result.startLocation = start;
    result.endLocation = end;
    result.mode = mode;
    result.timestamp = now;

    // 零长度路径快速返回
    if (dist < kZeroLengthThreshold)
    {
        result.status = PathStatus::Complete;
        result.isValid = true;
        result.lengthCm = 0.f;
        result.cost = 0.f;
        paths_[pathId] = result;
        pathLru_[pathId] = now;
        return pathId;
    }

    // 按模式计算。注意：不再有“直线兜底”和 A* 回退 ——
    // 直线兜底会画出穿墙穿石的假路线；A* 网格精度差且数据陈旧。
    // NavMesh 失败就诚实返回失败，部分路径（Partial）保留。
    if (mode == PathfindingMode::GridOnly)
        result = ComputeGridPath(start, end);
    else
        result = ComputeNavMeshPath(start, end, nullptr, 0.f);

    // 计算失败：不存无效路径占坑（旧实现存 Invalid 路径且 Tick
    // 不清理它们，占满上限后新寻路全部静默失败）
    if (!result.isValid && result.status != PathStatus::Partial)
        return 0;

    result.id = pathId;
    result.startLocation = start;
    result.endLocation = end;
    result.mode = mode;
    result.timestamp = now;

    paths_[pathId] = result;
    pathLru_[pathId] = now;
    return pathId;
}

uint64_t PathManager::FindPathToActor(SDK::AActor* targetActor, PathfindingMode mode)
{
    if (!targetActor || !IsProbablyValidPtr(targetActor))
        return 0;

    SDK::UWorld* world = GetWorld();
    if (!world || !IsProbablyValidPtr(world))
        return 0;

    // 获取玩家位置作为起点
    SDK::APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !IsProbablyValidPtr(player))
        return 0;

    // 生成目标显示名（界面/路径终点绘制用）
    std::string label = GetActorDisplayName(targetActor);

    SDK::FVector start{};
    SDK::FVector end{};
    if (!Helper::Try([&] {
        start = player->K2_GetActorLocation();
        end = targetActor->K2_GetActorLocation();
    }))
    {
        return 0;
    }

    // 检查距离限制
    const float dist = VecDist(start, end);
    if (dist > kMaxPathDistanceCm)
        return 0;

    const auto now = std::chrono::steady_clock::now();

    // 同目标已有路径 → 取消旧路径后重建（刷新语义）
    if (HasExistingPathToTarget(targetActor, end))
        CancelPathsToTarget(targetActor, end);

    // 检查路径数量上限
    if (static_cast<int>(paths_.size()) >= maxActivePaths_)
    {
        EvictOldestPath();
        if (static_cast<int>(paths_.size()) >= maxActivePaths_)
            return 0;
    }

    const uint64_t pathId = nextPathId_++;

    PathResult result;
    result.id = pathId;
    result.startLocation = start;
    result.endLocation = end;
    result.targetActor = targetActor;
    result.targetLabel = label;
    result.mode = mode;
    result.timestamp = now;

    // 零长度路径
    if (dist < kZeroLengthThreshold)
    {
        result.status = PathStatus::Complete;
        result.isValid = true;
        result.lengthCm = 0.f;
        result.cost = 0.f;
        paths_[pathId] = result;
        pathLru_[pathId] = now;
        return pathId;
    }

    // 按模式计算（无直线兜底：穿墙假路线不如诚实的部分路径/失败）
    if (mode == PathfindingMode::GridOnly)
        result = ComputeGridPath(start, end);
    else
        result = ComputeNavMeshPath(start, end, targetActor, dist);

    if (!result.isValid && result.status != PathStatus::Partial)
        return 0;

    result.id = pathId;
    result.startLocation = start;
    result.endLocation = end;
    result.targetActor = targetActor;
    result.targetLabel = label;
    result.mode = mode;
    result.timestamp = now;

    paths_[pathId] = result;
    pathLru_[pathId] = now;
    return pathId;
}

uint64_t PathManager::FindPathToNearbyObject(SDK::AActor* targetActor, float distanceMeters)
{
    // 距离太远则不寻路（超过 10km）
    if (distanceMeters > 10000.0f)
        return 0;

    // 根据 global cheatState 设置选择寻路模式（cheat_state.h 提供 inline 定义）
    PathfindingMode mode = PathfindingMode::Auto;
    if (cheatState.pathNavMeshOnly)
        mode = PathfindingMode::NavMeshOnly;

    return FindPathToActor(targetActor, mode);
}

// ============================================================
// 路径管理
// ============================================================

void PathManager::CancelPath(uint64_t pathId)
{
    auto it = paths_.find(pathId);
    if (it != paths_.end())
    {
        it->second.status = PathStatus::Cancelled;
        it->second.isValid = false;
    }
    pathLru_.erase(pathId);
}

void PathManager::CancelAllPaths()
{
    for (auto& [id, path] : paths_)
    {
        path.status = PathStatus::Cancelled;
        path.isValid = false;
    }
    paths_.clear();
    pathLru_.clear();
}

void PathManager::RecalculatePath(uint64_t pathId)
{
    auto it = paths_.find(pathId);
    if (it == paths_.end())
        return;

    PathResult& old = it->second;
    const auto now = std::chrono::steady_clock::now();

    // 防止无限重算：仅对“目标移动”触发的重算计数（跟随重算不受限）
    if (old.recalcCount >= 3)
    {
        old.status = PathStatus::Stale;
        old.isValid = false;
        return;
    }

    // 如果目标 actor 已失效（被销毁/GC），直接标记失效，不再重算
    if (old.targetActor && !IsProbablyValidPtr(old.targetActor))
    {
        old.status = PathStatus::Stale;
        old.isValid = false;
        return;
    }

    // 获取当前起终点位置
    SDK::FVector start = old.startLocation;
    SDK::FVector end = old.endLocation;

    // 如果目标 actor 存在，获取其最新位置
    if (old.targetActor)
    {
        Helper::Try([&] {
            end = old.targetActor->K2_GetActorLocation();
        });
    }

    // 获取玩家最新位置作为起点
    SDK::APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (player && IsProbablyValidPtr(player))
    {
        Helper::Try([&] {
            start = player->K2_GetActorLocation();
        });
    }

    const float dist = VecDist(start, end);
    if (dist > kMaxPathDistanceCm)
    {
        old.status = PathStatus::Invalid;
        old.isValid = false;
        return;
    }

    // 重新计算（沿用原模式）
    PathResult newResult;
    if (old.mode == PathfindingMode::GridOnly)
        newResult = ComputeGridPath(start, end);
    else
        newResult = ComputeNavMeshPath(start, end, old.targetActor, dist);

    if (!newResult.isValid && newResult.status != PathStatus::Partial)
    {
        // 重算失败：保留旧路径（路线不闪断），只更新节流时间戳，
        // 跟随场景下 500ms 后自动重试；目标移动场景 recalcCount
        // 已在上面判断，超限后由 Tick 清理。
        old.lastRecalcTime = now;
        return;
    }

    // 成功：保留原 ID / 目标 / 模式，重置重算计数（目标已稳定）
    newResult.id = pathId;
    newResult.startLocation = start;
    newResult.endLocation = end;
    newResult.targetActor = old.targetActor;
    newResult.targetLabel = old.targetLabel;
    newResult.mode = old.mode;
    newResult.timestamp = now;
    newResult.lastRecalcTime = now;
    newResult.recalcCount = 0;
    newResult.needsFollowUpdate = false;

    paths_[pathId] = newResult;
    pathLru_[pathId] = now;
}

void PathManager::InvalidatePath(uint64_t pathId)
{
    auto it = paths_.find(pathId);
    if (it != paths_.end())
    {
        it->second.status = PathStatus::Stale;
        it->second.isValid = false;
    }
}

bool PathManager::IsPathValid(uint64_t pathId) const
{
    auto it = paths_.find(pathId);
    if (it == paths_.end())
        return false;
    return it->second.isValid && it->second.status == PathStatus::Complete;
}

const PathResult* PathManager::GetPath(uint64_t pathId) const
{
    auto it = paths_.find(pathId);
    if (it == paths_.end())
        return nullptr;
    return &it->second;
}

PathResult* PathManager::GetPathMutable(uint64_t pathId)
{
    auto it = paths_.find(pathId);
    if (it == paths_.end())
        return nullptr;
    return &it->second;
}

int PathManager::GetActivePathCount() const
{
    int count = 0;
    for (const auto& [id, path] : paths_)
    {
        if (path.status == PathStatus::Complete || path.status == PathStatus::Partial)
            count++;
    }
    return count;
}

void PathManager::SetMaxActivePaths(int maxPaths)
{
    maxActivePaths_ = std::max(1, maxPaths);
}

void PathManager::SetGridSamplerConfig(const GridSamplerConfig& cfg)
{
    gridSampler_.config = cfg;
    gridSampler_.dirty = true;
}

void PathManager::ForceResampleGrid()
{
    gridSampler_.dirty = true;
    gridSampler_.sampling = false;
}

void PathManager::SetRecalcThresholdMeters(float meters)
{
    recalcThresholdMeters_ = std::max(0.1f, meters);
}

void PathManager::SetThrottleMs(int ms)
{
    throttleMs_ = std::max(10, ms);
}

// ============================================================
// Tick 主循环
// ============================================================

void PathManager::Tick(float deltaTime)
{
    const auto now = std::chrono::steady_clock::now();

    // ---- 节流：每 throttleMs_ 毫秒才执行一次 Tick 完整逻辑 ----
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastComputeTime_).count();
    if (elapsed < throttleMs_)
        return;

    // 取一次玩家位置（多条路径共享，避免每条路径重复 ProcessEvent）
    SDK::FVector playerPos{};
    bool havePlayer = false;
    SDK::APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (player && IsProbablyValidPtr(player))
        havePlayer = Helper::Try([&] { playerPos = player->K2_GetActorLocation(); });

    // 1. 检查目标 actor 是否仍有效
    for (auto& [id, path] : paths_)
    {
        if (path.targetActor && !IsProbablyValidPtr(path.targetActor))
        {
            path.status = PathStatus::Stale;
            path.isValid = false;
        }
    }

    // 2. 判断重算需求 + 到达检测
    if (cheatState.pathAutoRecalc && havePlayer)
    {
        for (auto& [id, path] : paths_)
        {
            if (!path.isValid)
                continue;

            // 玩家移动 → 跟随重算（不限次数：绘制端有动态起点兜底，
            // 旧实现 3 次上限导致“走 15 米路线直接消失”）
            if (VecDist(path.startLocation, playerPos) > recalcThresholdMeters_ * 100.0f)
                path.needsFollowUpdate = true;

            // 目标移动 → 有限次重算（防目标乱跑无限重算）
            if (path.targetActor && IsProbablyValidPtr(path.targetActor))
            {
                SDK::FVector cur{};
                if (Helper::Try([&] { cur = path.targetActor->K2_GetActorLocation(); }))
                {
                    if (VecDist(path.endLocation, cur) > recalcThresholdMeters_ * 100.0f)
                        path.status = PathStatus::Stale;
                }
            }
        }
    }

    // 3. 执行重算（每条路径 500ms 节流，避免渲染线程连续同步 NavMesh 查询）
    if (cheatState.pathAutoRecalc)
    {
        for (auto& [id, path] : paths_)
        {
            const bool staleRecalc = (path.status == PathStatus::Stale && path.recalcCount < 3);
            const bool followRecalc = path.needsFollowUpdate;
            if (!staleRecalc && !followRecalc)
                continue;
            const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - path.lastRecalcTime).count();
            if (since < 500)
                continue;
            RecalculatePath(id);
        }
    }

    // 4. 清理路径
    std::vector<uint64_t> toRemove;
    for (const auto& [id, path] : paths_)
    {
        if (path.status == PathStatus::Complete || path.status == PathStatus::Partial)
        {
            // 到达检测：玩家距终点 < 3m 视为导航完成
            if (havePlayer && VecDist(path.endLocation, playerPos) < 300.0f)
            {
                toRemove.push_back(id);
                continue;
            }
            // 30 秒过期（跟随重算会刷新 timestamp，跟随时不会过期）
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - path.timestamp).count();
            if (age > static_cast<long long>(kPathExpirySeconds))
                toRemove.push_back(id);
        }
        else if (path.status == PathStatus::Cancelled || path.status == PathStatus::Invalid)
        {
            // 旧实现不清理 Invalid 路径 → 永久占坑占满上限 → 新寻路全部静默失败
            toRemove.push_back(id);
        }
        else if (path.status == PathStatus::Stale)
        {
            // Stale 且无重算机会（超限）才删；还有机会的留给重算
            if (path.recalcCount >= 3)
                toRemove.push_back(id);
        }
    }
    for (uint64_t id : toRemove)
    {
        paths_.erase(id);
        pathLru_.erase(id);
    }

    // 5. 网格采样：仅当存在 GridOnly 模式的活跃路径时才需要网格。
    // NavMesh 模式下采样是纯浪费（每 Tick 16 条 LineTrace 的
    // ProcessEvent + 物理查询负载）。
    bool anyGridPath = false;
    for (const auto& [id, path] : paths_)
    {
        if (path.mode == PathfindingMode::GridOnly &&
            (path.status == PathStatus::Complete || path.status == PathStatus::Partial))
        {
            anyGridPath = true;
            break;
        }
    }

    if (anyGridPath)
    {
        if (gridSampler_.dirty && !gridSampler_.sampling)
        {
            SampleGridAroundPlayer();
        }
        else if (!gridSampler_.sampling && !gridSampler_.dirty && havePlayer)
        {
            const float dist = VecDist2D(playerPos, gridSampler_.center);
            if (dist > kGridResampleThreshold)
            {
                gridSampler_.dirty = true;
                SampleGridAroundPlayer();
            }
        }
    }
}

// ============================================================
// NavMesh 路径计算
// ============================================================

PathResult PathManager::ComputeNavMeshPath(const SDK::FVector& start, const SDK::FVector& end,
                                            SDK::AActor* targetActor, float tetherDistance)
{
    PathResult result;
    result.status = PathStatus::Invalid;
    result.isValid = false;

    SDK::UWorld* world = GetWorld();
    if (!world || !IsProbablyValidPtr(world))
        return result;

    // ============================================================
    // 手动 NavMesh 寻路（不用 SDK 生成的静态函数）
    //
    // SDK 生成的 FindPathToLocationSynchronously 内部用
    //     GetDefaultObj()->ProcessEvent(Func, &Parms)
    // 在"类默认对象(CDO)"上调用。Palworld 定制了 UPalNavigationSystemV1
    // 与 APalRecastNavMesh，真实寻路状态都在世界实例上，CDO 上调用
    // 必然静默失败（返回 null），导致永远走直线兜底。
    //
    // 正确做法：取世界真实 NavigationSystem 实例，手动构造 Params，
    // 在真实实例上 ProcessEvent。
    // ============================================================

    auto findPathOnInstance = [&](SDK::UNavigationSystemV1* navSys,
                                  const SDK::FVector& s, const SDK::FVector& e) -> SDK::UNavigationPath* {
        if (!navSys || !IsProbablyValidPtr(navSys))
            return nullptr;

        SDK::UNavigationPath* outPath = nullptr;
        Helper::Try([&] {
            // 取 UFunction（在 NavigationSystemV1 类上，静态 UFunction）
            SDK::UFunction* func = SDK::UNavigationSystemV1::StaticClass()->GetFunction(
                "NavigationSystemV1", "FindPathToLocationSynchronously");
            if (!func)
                return;

            // 构造参数
            SDK::Params::NavigationSystemV1_FindPathToLocationSynchronously parms{};
            parms.WorldContextObject = world;
            parms.PathStart = s;
            parms.PathEnd = e;
            parms.PathfindingContext = nullptr;
            parms.FilterClass = TSubclassOf<SDK::UNavigationQueryFilter>();

            // 设 FUNC_BlueprintCallable 标志（与 SDK 生成代码一致）
            const uint32 flags = func->FunctionFlags;
            func->FunctionFlags |= 0x400;

            // 在真实实例上调用
            navSys->ProcessEvent(func, &parms);

            func->FunctionFlags = flags;

            outPath = parms.ReturnValue;
        });
        return (outPath && IsProbablyValidPtr(outPath)) ? outPath : nullptr;
    };

    // 获取世界真实 NavigationSystem 实例
    SDK::UNavigationSystemV1* navSys = nullptr;
    Helper::Try([&] {
        navSys = SDK::UNavigationSystemV1::GetNavigationSystem(world);
    });
    // 如果拿不到或不是 Pal 定制类，尝试 Cast 到 UPalNavigationSystemV1
    if (!navSys || !IsProbablyValidPtr(navSys))
    {
        Helper::Try([&] {
            navSys = static_cast<SDK::UNavigationSystemV1*>(SDK::UPalNavigationSystemV1::GetDefaultObj());
        });
    }
    if (!navSys || !IsProbablyValidPtr(navSys))
        return result;

    auto pathUsable = [](SDK::UNavigationPath* p) -> bool {
        return p && IsProbablyValidPtr(p) && Helper::Try([&] { return p->IsValid(); });
    };

    // 1) 直接寻路
    SDK::UNavigationPath* navPath = findPathOnInstance(navSys, start, end);

    // 2) 失败 → 投影到最近可行走面后重试
    if (!pathUsable(navPath))
    {
        SDK::FVector startP = start;
        SDK::FVector endP = end;
        const bool sOk = ProjectToNavMesh(start, startP, SDK::FVector(300, 300, 300));
        const bool eOk = ProjectToNavMesh(end, endP, SDK::FVector(500, 500, 500));
        if (sOk || eOk)
            navPath = findPathOnInstance(navSys, startP, endP);
    }

    // 3) 若直连失败且两者不连通，做多锚点探测：
    //    起点周围的可行走面 → 终点周围的可行走面，逐一尝试
    //    解决"悬崖下 → 悬崖上"这种 NavMesh 不相邻但可达的情况
    if (!pathUsable(navPath))
    {
        const SDK::FVector anchors[] = {
            SDK::FVector(start.X + 200, start.Y, start.Z),
            SDK::FVector(start.X - 200, start.Y, start.Z),
            SDK::FVector(start.X, start.Y + 200, start.Z),
            SDK::FVector(start.X, start.Y - 200, start.Z),
            SDK::FVector(start.X + 200, start.Y + 200, start.Z),
            SDK::FVector(start.X - 200, start.Y - 200, start.Z),
        };
        const SDK::FVector targetAnchors[] = {
            SDK::FVector(end.X + 200, end.Y, end.Z),
            SDK::FVector(end.X - 200, end.Y, end.Z),
            SDK::FVector(end.X, end.Y + 200, end.Z),
            SDK::FVector(end.X, end.Y - 200, end.Z),
        };
        for (const auto& sa : anchors)
        {
            if (pathUsable(navPath)) break;
            for (const auto& ta : targetAnchors)
            {
                navPath = findPathOnInstance(navSys, sa, ta);
                if (pathUsable(navPath))
                    break;
            }
        }
    }

    if (!pathUsable(navPath))
        return result;

    // 检查路径质量
    bool pathPartial = false;
    bool pathHasPoints = false;
    Helper::Try([&] {
        pathPartial = navPath->IsPartial();
        pathHasPoints = navPath->PathPoints.Num() > 0;
    });

    // 提取路径点
    std::vector<PathWaypoint> waypoints;
    float accumulatedCost = 0.f;

    Helper::Try([&] {
        const int32 numPoints = navPath->PathPoints.Num();
        if (numPoints <= 0)
            return;

        const int32 maxPoints = std::min(numPoints, static_cast<int32>(kMaxWaypointsPerPath));

        for (int32 i = 0; i < maxPoints; ++i)
        {
            SDK::FVector pt = navPath->PathPoints[i];
            // 跳过与上一个点重复的点（NavMesh 可能返回连续相同坐标）
            if (!waypoints.empty())
            {
                const double d = pt.GetDistanceTo(waypoints.back().location);
                if (d < 1.0)
                    continue;
            }
            PathWaypoint wp;
            wp.location = pt;
            wp.flags = PathWaypointFlags::OnNavMesh;

            if (!waypoints.empty())
                accumulatedCost += VecDist(waypoints.back().location, pt);
            wp.costFromStart = accumulatedCost;

            waypoints.push_back(wp);
        }
    });

    if (waypoints.empty())
    {
        // 寻路成功但返回空点 → 零长度路径
        result.status = PathStatus::Complete;
        result.isValid = true;
        result.isPartial = false;
        result.lengthCm = 0.f;
        result.cost = 0.f;
        return result;
    }

    // 路径精简 + 平滑
    SimplifyPath(waypoints);

    float pathLength = 0.f;
    float pathCost = 0.f;
    Helper::Try([&] {
        pathLength = navPath->GetPathLength();
        pathCost = navPath->GetPathCost();
    });

    result.waypoints = waypoints;
    result.lengthCm = pathLength;
    result.cost = pathCost;
    result.isPartial = pathPartial; // 引擎返回的 partial 标记保真

    if (pathPartial)
        result.status = PathStatus::Partial;
    else
        result.status = PathStatus::Complete;

    result.isValid = true;
    return result;
}

// ============================================================
// 网格采样
// ============================================================

void PathManager::SampleGridAroundPlayer(bool incremental)
{
    SDK::APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !IsProbablyValidPtr(player))
        return;

    SDK::FVector playerPos{};
    if (!Helper::Try([&] { playerPos = player->K2_GetActorLocation(); }))
        return;

    SDK::UWorld* world = GetWorld();
    if (!world || !IsProbablyValidPtr(world))
        return;

    GridSamplerConfig& cfg = gridSampler_.config;
    const int totalCells = cfg.gridWidth * cfg.gridHeight;

    // 初始化网格
    if (gridSampler_.cells.size() != static_cast<size_t>(totalCells))
    {
        gridSampler_.cells.resize(totalCells);
    }

    // 首次采样或玩家移动超过阈值时重置采样游标
    if (gridSampler_.dirty)
    {
        gridSampler_.sampleCursorX = 0;
        gridSampler_.sampleCursorY = 0;
        gridSampler_.center = playerPos;
        gridSampler_.dirty = false;
        gridSampler_.sampling = true;
    }

    // 如果采样已完成，直接返回
    if (!gridSampler_.sampling)
        return;

    // 更新中心位置（采样过程中玩家可能继续移动）
    gridSampler_.center = playerPos;

    // 分帧采样：每帧最多采样 maxSamplePerFrame 格
    int sampledThisFrame = 0;
    const int maxPerFrame = incremental ? cfg.maxSamplePerFrame : totalCells;

    for (int y = gridSampler_.sampleCursorY; y < cfg.gridHeight && sampledThisFrame < maxPerFrame; ++y)
    {
        const int startX = (y == gridSampler_.sampleCursorY) ? gridSampler_.sampleCursorX : 0;
        for (int x = startX; x < cfg.gridWidth && sampledThisFrame < maxPerFrame; ++x)
        {
            const int idx = y * cfg.gridWidth + x;
            GridCell& cell = gridSampler_.cells[idx];

            // 跳过已采样格子
            if (cell.sampled)
            {
                sampledThisFrame++;
                continue;
            }

            // 计算格子中心世界坐标
            const float worldX = playerPos.X + (x - cfg.gridWidth / 2.0f) * cfg.cellSize;
            const float worldY = playerPos.Y + (y - cfg.gridHeight / 2.0f) * cfg.cellSize;

            // 从高处向低处发射射线
            const SDK::FVector traceStart(worldX, worldY, playerPos.Z + 1000.0f);
            const SDK::FVector traceEnd(worldX, worldY, playerPos.Z - 1000.0f);

            SDK::FHitResult hit{};
            SDK::TArray<SDK::AActor*> ignoreActors;
            bool traceHit = false;

            Helper::Try([&] {
                traceHit = SDK::UKismetSystemLibrary::LineTraceSingle(
                    world,
                    traceStart,
                    traceEnd,
                    SDK::ETraceTypeQuery::TraceTypeQuery1,
                    true,
                    ignoreActors,
                    SDK::EDrawDebugTrace::None,
                    &hit,
                    true,
                    SDK::FLinearColor(0, 0, 0, 0),
                    SDK::FLinearColor(0, 0, 0, 0),
                    0.0f
                );
            });

            if (traceHit && hit.bBlockingHit)
            {
                cell.height = hit.Location.Z;
                cell.walkable = true;
                cell.sampled = true;

                // 坡度检查
                if (x > 0 && y > 0)
                {
                    const int prevIdx = (y - 1) * cfg.gridWidth + (x - 1);
                    const GridCell& prevCell = gridSampler_.cells[prevIdx];
                    if (prevCell.sampled)
                    {
                        const float heightDiff = std::abs(cell.height - prevCell.height);
                        if (heightDiff > cfg.maxStepHeight)
                            cell.walkable = false;
                    }
                }

                if (x > 0)
                {
                    const int leftIdx = y * cfg.gridWidth + (x - 1);
                    const GridCell& leftCell = gridSampler_.cells[leftIdx];
                    if (leftCell.sampled)
                    {
                        const float heightDiff = std::abs(cell.height - leftCell.height);
                        if (heightDiff > 30.0f && heightDiff <= cfg.maxStepHeight)
                            cell.costModifier = 150;
                    }
                }
            }
            else
            {
                cell.walkable = false;
                cell.sampled = true;
                cell.height = playerPos.Z;
                cell.costModifier = 255;
            }

            sampledThisFrame++;

            // 更新游标
            gridSampler_.sampleCursorX = x + 1;
        }
        gridSampler_.sampleCursorY = y;
        gridSampler_.sampleCursorX = 0;
    }

    // 检查采样是否全部完成
    if (gridSampler_.sampleCursorY >= cfg.gridHeight)
    {
        gridSampler_.sampling = false;
        gridSampler_.lastCenterZ = playerPos.Z;
        gridSampler_.sampleCursorX = 0;
        gridSampler_.sampleCursorY = 0;
    }
}

// ============================================================
// A* 网格路径计算
// ============================================================

struct AStarNode {
    int x = 0, y = 0;
    float g = FLT_MAX; // 起点到当前点的实际成本
    float h = 0.f;     // 当前点到终点的启发式估计
    float f() const { return g + h; }

    // 用于优先队列比较（小顶堆）
    bool operator>(const AStarNode& other) const {
        return f() > other.f();
    }
};

// 自定义哈希用于 A* 的 closed set
struct PairHash {
    size_t operator()(const std::pair<int, int>& p) const {
        return static_cast<size_t>(p.first) ^ (static_cast<size_t>(p.second) << 16);
    }
};

PathResult PathManager::ComputeGridPath(const SDK::FVector& start, const SDK::FVector& end)
{
    PathResult result;
    result.status = PathStatus::Invalid;
    result.isValid = false;

    // 网格已采样就直接用，采样中就用已有数据，全新脏网格也用已有数据
    // 不强制同步采样，避免卡顿 1-2 帧
    const GridSamplerConfig& cfg = gridSampler_.config;
    const int gw = cfg.gridWidth;
    const int gh = cfg.gridHeight;

    if (gridSampler_.cells.size() != static_cast<size_t>(gw * gh))
        return result;

    const SDK::FVector& center = gridSampler_.center;

    // 将世界坐标转换为网格坐标（返回未裁剪的原始坐标，供越界判断）
    auto worldToGridRaw = [&](const SDK::FVector& worldPos) -> std::pair<int, int> {
        const int gx = static_cast<int>(std::round((worldPos.X - center.X) / cfg.cellSize + gw / 2.0f));
        const int gy = static_cast<int>(std::round((worldPos.Y - center.Y) / cfg.cellSize + gh / 2.0f));
        return { gx, gy };
    };

    auto gridToWorld = [&](int gx, int gy) -> SDK::FVector {
        const float wx = center.X + (gx - gw / 2.0f) * cfg.cellSize;
        const float wy = center.Y + (gy - gh / 2.0f) * cfg.cellSize;
        const int idx = gy * gw + gx;
        const float wz = (idx >= 0 && idx < static_cast<int>(gridSampler_.cells.size()))
            ? gridSampler_.cells[idx].height
            : center.Z;
        return SDK::FVector(wx, wy, wz);
    };

    auto [startGx, startGy] = worldToGridRaw(start);
    auto [endGx, endGy] = worldToGridRaw(end);

    // 起点必须在网格范围内
    if (startGx < 0 || startGx >= gw || startGy < 0 || startGy >= gh)
        return result;

    // 如果终点在网格外，使用边界最近点并标记为部分路径
    if (endGx < 0 || endGx >= gw || endGy < 0 || endGy >= gh)
    {
        endGx = std::clamp(endGx, 0, gw - 1);
        endGy = std::clamp(endGy, 0, gh - 1);
        result.isPartial = true;
    }

    // 检查起点是否可行走
    const int startIdx = startGy * gw + startGx;
    if (startIdx >= 0 && startIdx < static_cast<int>(gridSampler_.cells.size()))
    {
        if (!gridSampler_.cells[startIdx].walkable)
        {
            // 起点不可行走，找最近的可行走格子
            int bestDist = INT_MAX;
            int bestGx = startGx, bestGy = startGy;
            for (int dy = -5; dy <= 5; ++dy)
            {
                for (int dx = -5; dx <= 5; ++dx)
                {
                    const int nx = startGx + dx;
                    const int ny = startGy + dy;
                    if (nx < 0 || nx >= gw || ny < 0 || ny >= gh)
                        continue;
                    const int nidx = ny * gw + nx;
                    if (gridSampler_.cells[nidx].walkable)
                    {
                        const int d = dx * dx + dy * dy;
                        if (d < bestDist)
                        {
                            bestDist = d;
                            bestGx = nx;
                            bestGy = ny;
                        }
                    }
                }
            }
            startGx = bestGx;
            startGy = bestGy;
        }
    }

    // A* 算法
    // 8 方向移动：dx, dy, cost
    static const int kDir[8][3] = {
        {1, 0, 100},   // 右
        {-1, 0, 100},  // 左
        {0, 1, 100},   // 下
        {0, -1, 100},  // 上
        {1, 1, 141},   // 右下（对角线 ≈ √2 * 100）
        {1, -1, 141},  // 右上
        {-1, 1, 141},  // 左下
        {-1, -1, 141}  // 左上
    };

    // 优先队列
    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;

    // 最佳成本
    std::vector<float> bestG(gw * gh, FLT_MAX);

    // 父节点回溯
    std::vector<int> parent(gw * gh, -1);

    // 已关闭集合
    std::unordered_set<std::pair<int, int>, PairHash> closedSet;

    // 目标格
    const int endIdx = endGy * gw + endGx;

    // 初始化起点
    AStarNode startNode;
    startNode.x = startGx;
    startNode.y = startGy;
    startNode.g = 0.f;

    // 启发式：曼哈顿 + 对角线距离
    const float dx = static_cast<float>(std::abs(endGx - startGx));
    const float dy = static_cast<float>(std::abs(endGy - startGy));
    startNode.h = (dx + dy) * 100.f + (141.f - 2.f * 100.f) * std::min(dx, dy);

    openSet.push(startNode);
    bestG[startGy * gw + startGx] = 0.f;

    bool found = false;
    int iterCount = 0;

    while (!openSet.empty() && iterCount < kMaxAStarIterations)
    {
        AStarNode current = openSet.top();
        openSet.pop();

        const int curIdx = current.y * gw + current.x;

        // 跳过已关闭的节点
        if (closedSet.count({current.x, current.y}))
            continue;

        // 如果当前节点成本不是最优，跳过
        if (current.g > bestG[curIdx])
            continue;

        closedSet.insert({current.x, current.y});

        // 到达目标
        if (current.x == endGx && current.y == endGy)
        {
            found = true;
            break;
        }

        // 探索邻居
        for (int di = 0; di < 8; ++di)
        {
            const int nx = current.x + kDir[di][0];
            const int ny = current.y + kDir[di][1];

            if (nx < 0 || nx >= gw || ny < 0 || ny >= gh)
                continue;

            const int nIdx = ny * gw + nx;

            // 检查格子是否可行走
            if (!gridSampler_.cells[nIdx].walkable)
                continue;

            // 检查是否已关闭
            if (closedSet.count({nx, ny}))
                continue;

            // 计算移动成本
            float moveCost = static_cast<float>(kDir[di][2]);

            // 应用成本修正
            const uint8_t costMod = gridSampler_.cells[nIdx].costModifier;
            if (costMod != 100)
            {
                moveCost = moveCost * costMod / 100.0f;
            }

            const float tentativeG = current.g + moveCost;

            if (tentativeG >= bestG[nIdx])
                continue;

            bestG[nIdx] = tentativeG;
            parent[nIdx] = curIdx;

            AStarNode neighbor;
            neighbor.x = nx;
            neighbor.y = ny;
            neighbor.g = tentativeG;

            // 启发式：曼哈顿 + 对角线
            const float hdx = static_cast<float>(std::abs(endGx - nx));
            const float hdy = static_cast<float>(std::abs(endGy - ny));
            neighbor.h = (hdx + hdy) * 100.f + (141.f - 2.f * 100.f) * std::min(hdx, hdy);

            openSet.push(neighbor);
        }

        iterCount++;
    }

    if (!found && !result.isPartial)
    {
        // 未找到路径
        return result;
    }

    // 回溯路径
    std::vector<PathWaypoint> waypoints;
    float totalLength = 0.f;

    if (found)
    {
        // 从目标回溯到起点
        std::vector<std::pair<int, int>> gridPath;
        int cur = endIdx;
        while (cur != -1)
        {
            const int gx = cur % gw;
            const int gy = cur / gw;
            gridPath.push_back({gx, gy});
            cur = parent[cur];
        }
        std::reverse(gridPath.begin(), gridPath.end());

        // 转换为世界坐标
        SDK::FVector prevWorldPos = gridToWorld(gridPath[0].first, gridPath[0].second);
        float accumulatedCost = 0.f;

        for (size_t i = 1; i < gridPath.size(); ++i)
        {
            const auto [gx, gy] = gridPath[i];
            SDK::FVector worldPos = gridToWorld(gx, gy);

            // 抬高 Z 到地面高度
            // 使用网格高度
            const int idx = gy * gw + gx;
            if (idx >= 0 && idx < static_cast<int>(gridSampler_.cells.size()))
            {
                worldPos.Z = gridSampler_.cells[idx].height;
            }

            PathWaypoint wp;
            wp.location = worldPos;
            accumulatedCost += VecDist(prevWorldPos, worldPos);
            wp.costFromStart = accumulatedCost;
            wp.flags = PathWaypointFlags::Interpolated;
            waypoints.push_back(wp);

            prevWorldPos = worldPos;
        }

        if (!waypoints.empty())
            totalLength = waypoints.back().costFromStart;
    }
    else
    {
        // 部分路径：找到最接近目标的已探索节点
        int bestNode = -1;
        float bestDist = FLT_MAX;
        for (int y = 0; y < gh; ++y)
        {
            for (int x = 0; x < gw; ++x)
            {
                const int idx = y * gw + x;
                if (parent[idx] != -1)
                {
                    const float dx = static_cast<float>(x - endGx);
                    const float dy = static_cast<float>(y - endGy);
                    const float d = dx * dx + dy * dy;
                    if (d < bestDist)
                    {
                        bestDist = d;
                        bestNode = idx;
                    }
                }
            }
        }

        if (bestNode != -1)
        {
            // 回溯
            std::vector<std::pair<int, int>> gridPath;
            int cur = bestNode;
            while (cur != -1)
            {
                const int gx = cur % gw;
                const int gy = cur / gw;
                gridPath.push_back({gx, gy});
                cur = parent[cur];
            }
            std::reverse(gridPath.begin(), gridPath.end());

            SDK::FVector prevWorldPos = gridToWorld(gridPath[0].first, gridPath[0].second);
            float accumulatedCost = 0.f;

            for (size_t i = 1; i < gridPath.size(); ++i)
            {
                const auto [gx, gy] = gridPath[i];
                SDK::FVector worldPos = gridToWorld(gx, gy);

                PathWaypoint wp;
                wp.location = worldPos;
                accumulatedCost += VecDist(prevWorldPos, worldPos);
                wp.costFromStart = accumulatedCost;
                wp.flags = PathWaypointFlags::Interpolated;
                waypoints.push_back(wp);

                prevWorldPos = worldPos;
            }

            if (!waypoints.empty())
                totalLength = waypoints.back().costFromStart;
        }
    }

    if (waypoints.empty())
        return result;

    // 路径精简
    SimplifyPath(waypoints);

    result.waypoints = waypoints;
    result.lengthCm = totalLength;
    result.cost = totalLength;
    result.isPartial = result.isPartial || !found;
    result.status = result.isPartial ? PathStatus::Partial : PathStatus::Complete;
    result.isValid = true;

    return result;
}

// ============================================================
// 路径验证
// ============================================================

bool PathManager::ValidatePathWalkability(const PathResult& path)
{
    if (path.waypoints.size() < 2)
        return path.waypoints.empty(); // 零长度路径有效

    // 检查相邻路径点是否可达
    for (size_t i = 1; i < path.waypoints.size(); ++i)
    {
        if (!CheckWaypointReachable(path.waypoints[i - 1].location, path.waypoints[i].location))
            return false;
    }

    return true;
}

bool PathManager::CheckWaypointReachable(const SDK::FVector& from, const SDK::FVector& to)
{
    const float dist = VecDist2D(from, to);
    if (dist < kZeroLengthThreshold)
        return true;

    // 使用 NavMesh raycast 检查
    return IsNavigableBetween(from, to);
}

// ============================================================
// 路径精简（去除冗余共线点，Douglas-Peucker 简化）
// ============================================================

void PathManager::SimplifyPath(std::vector<PathWaypoint>& waypoints)
{
    if (waypoints.size() <= 2)
        return;

    // 第一阶段：去除共线点
    std::vector<PathWaypoint> simplified;
    simplified.push_back(waypoints.front());

    for (size_t i = 1; i + 1 < waypoints.size(); ++i)
    {
        const SDK::FVector& prev = simplified.back().location;
        const SDK::FVector& curr = waypoints[i].location;
        const SDK::FVector& next = waypoints[i + 1].location;

        // 计算从 prev 到 next 的向量
        const SDK::FVector dir(next.X - prev.X, next.Y - prev.Y, 0.f);
        const float dirLen = std::sqrt(dir.X * dir.X + dir.Y * dir.Y);

        if (dirLen < 1.0f)
        {
            simplified.push_back(waypoints[i]);
            continue;
        }

        // 计算 curr 到 prev->next 线段的垂直距离
        const float t = ((curr.X - prev.X) * dir.X + (curr.Y - prev.Y) * dir.Y) / (dirLen * dirLen);
        const float clampedT = std::clamp(t, 0.0f, 1.0f);
        const SDK::FVector proj(prev.X + dir.X * clampedT, prev.Y + dir.Y * clampedT, 0.f);
        const float perpDist = std::sqrt(
            (curr.X - proj.X) * (curr.X - proj.X) +
            (curr.Y - proj.Y) * (curr.Y - proj.Y)
        );

        // 如果垂直距离过大，保留该点
        if (perpDist > kMinWaypointDistance * 0.5f)
        {
            simplified.push_back(waypoints[i]);
        }
    }

    // 始终保留终点
    if (simplified.back().location.X != waypoints.back().location.X ||
        simplified.back().location.Y != waypoints.back().location.Y ||
        simplified.back().location.Z != waypoints.back().location.Z)
    {
        simplified.push_back(waypoints.back());
    }

    // 第二阶段：合并过近的点
    if (simplified.size() > 2)
    {
        std::vector<PathWaypoint> merged;
        merged.push_back(simplified.front());

        for (size_t i = 1; i < simplified.size(); ++i)
        {
            const float dist = VecDist(merged.back().location, simplified[i].location);
            if (dist >= kMinWaypointDistance)
            {
                merged.push_back(simplified[i]);
            }
        }

        // 确保终点存在
        if (merged.size() > 1)
        {
            const float lastDist = VecDist(merged.back().location, simplified.back().location);
            if (lastDist > 1.0f)
            {
                merged.back() = simplified.back();
            }
        }

        waypoints = merged;
    }
    else
    {
        waypoints = simplified;
    }

    // 限制最大路径点数量
    if (waypoints.size() > static_cast<size_t>(kMaxWaypointsPerPath))
    {
        waypoints.resize(kMaxWaypointsPerPath);
    }
}

// ============================================================
// 辅助方法
// ============================================================

bool PathManager::ShouldRecalculate(const PathResult& path, const SDK::FVector& playerPos, bool havePlayer)
{
    if (!path.isValid)
        return false;

    // 检查是否超过重算次数上限（仅约束目标移动场景）
    if (path.recalcCount >= 3)
        return false;

    // 检查目标 actor 是否移动
    if (path.targetActor && IsProbablyValidPtr(path.targetActor))
    {
        SDK::FVector currentTargetPos{};
        if (Helper::Try([&] { currentTargetPos = path.targetActor->K2_GetActorLocation(); }))
        {
            const float targetDist = VecDist(path.endLocation, currentTargetPos);
            if (targetDist > recalcThresholdMeters_ * 100.0f)
                return true;
        }
    }

    // 检查玩家是否移动
    if (havePlayer)
    {
        const float playerDist = VecDist(path.startLocation, playerPos);
        if (playerDist > recalcThresholdMeters_ * 100.0f)
            return true;
    }

    return false;
}

bool PathManager::HasExistingPathToTarget(SDK::AActor* targetActor, const SDK::FVector& targetLocation) const
{
    for (const auto& [id, path] : paths_)
    {
        if (!path.isValid)
            continue;

        // 检查 actor 目标
        if (targetActor && path.targetActor == targetActor)
            return true;

        // 检查位置目标（距离 < 1m）
        if (!targetActor && !path.targetActor)
        {
            const float dist = VecDist(path.endLocation, targetLocation);
            if (dist < kDuplicateTargetThreshold)
                return true;
        }
    }
    return false;
}

void PathManager::CancelPathsToTarget(SDK::AActor* targetActor, const SDK::FVector& targetLocation)
{
    std::vector<uint64_t> toRemove;
    for (const auto& [id, path] : paths_)
    {
        if (targetActor && path.targetActor == targetActor)
        {
            toRemove.push_back(id);
            continue;
        }
        if (!targetActor && !path.targetActor &&
            VecDist(path.endLocation, targetLocation) < kDuplicateTargetThreshold)
        {
            toRemove.push_back(id);
        }
    }
    for (uint64_t id : toRemove)
    {
        paths_.erase(id);
        pathLru_.erase(id);
    }
}

void PathManager::EvictOldestPath()
{
    if (pathLru_.empty())
        return;

    // 找到最旧的路径
    uint64_t oldestId = 0;
    auto oldestTime = std::chrono::steady_clock::time_point::max();

    for (const auto& [id, time] : pathLru_)
    {
        if (time < oldestTime)
        {
            oldestTime = time;
            oldestId = id;
        }
    }

    if (oldestId > 0)
    {
        paths_.erase(oldestId);
        pathLru_.erase(oldestId);
    }
}