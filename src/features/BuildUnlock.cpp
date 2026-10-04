#include <pch.h>
#include "core/Log.hpp"
#include "CheatState.hpp"
#include "engine/GameHelper.hpp"


using namespace SDK;
using namespace Helper;

// ------------------------------------------------------------------
// 建造/制作解锁的每帧逻辑 (临时生效, 重启游戏自动还原)
// ------------------------------------------------------------------

// 获取玩家建造组件
static UPalBuilderComponent* GetPlayerBuilderComponent()
{
    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player) return nullptr;
    return player->BuilderComponent;
}

// 清除当前建造目标的重叠检测数组 -> 无视"与其他物体重叠"
static void ClearOverlapChecks(UPalBuilderComponent* builder)
{
    if (!builder) return;
    APalBuildObjectInstallChecker* checker = builder->InstallChecker;
    if (!checker) return;

    UPalBuildObjectOverlapChecker* overlap = checker->OverlapChecker;
    if (!overlap) return;

    // 清空被重叠的建造物 / 其他物体，使重叠检测通过
    overlap->OverlapBuildObjects.Clear();
    overlap->OverlapOtherObjects.Clear();
    overlap->OverlappedActor = nullptr;
}

// 把单个工作进度补满（建造 / 制作通用）
static void FastForwardWorkProgress(UPalWorkProgress* work)
{
    if (!work) return;
    if (work->RequiredWorkAmount > 0.f && work->CurrentWorkAmount < work->RequiredWorkAmount)
    {
        work->CurrentWorkAmount = work->RequiredWorkAmount;

        // 取消当前 tick 的节流: 让游戏下一次 tick 立即处理"完成事件",
        // 否则完成一件后要等 TickProcessMinInterval(通常 ~1s) 才结算/开下一件,
        // 表现为"做完一个停 1 秒再做下一个"。
        work->ProgressTimeSinceLastTick = work->TickProcessMinInterval;
    }
}

// 遍历世界上所有建造工作进度并补满 -> 快速建造/制作
// 除建筑/设施的"建造进度"外，也补满工作台等 WorkeeModule 的"制作进度"，
// 使"快速制作"同样生效。
// 注意：该函数在后台线程中运行，且游戏世界可能尚未完全加载。
// 只遍历 PersistentLevel（常驻关卡，游戏开始后一定存在），不做 world->Levels
// 全量遍历，避免访问未初始化的 TArray 数据而触发 CRT abort。
static void FastForwardAllBuildWork()
{
    UWorld* world = UWorld::GetWorld();
    if (!world) return;

    // 世界未就绪：没有 OwningGameInstance 或 PersistentLevel 未设置时直接返回
    if (!world->OwningGameInstance || !Helper::IsProbablyValidPtr(world->OwningGameInstance))
        return;
    ULevel* level = world->PersistentLevel;
    if (!level || !Helper::IsProbablyValidPtr(level))
        return;

    Helper::Try([&]
    {
        const auto& actors = level->Actors;
        if (actors.Num() <= 0 || actors.GetDataPtr() == nullptr)
            return;

        for (int32 ai = 0; ai < actors.Num(); ++ai)
        {
            AActor* actor = actors[ai];
            if (!actor || !Helper::IsProbablyValidPtr(actor)) continue;
            if (!actor->IsA(APalBuildObject::StaticClass())) continue;

            APalBuildObject* buildObj = static_cast<APalBuildObject*>(actor);
            UPalMapObjectModel* model = buildObj->MapObjectModel;
            if (!model) continue;

            // 建造进度（建造/放置中的建筑本体）
            UPalBuildProcess* proc = model->BuildProcess;
            if (proc)
                FastForwardWorkProgress(proc->BuildWork);

            // 制作进度（工作台/设施内正在进行的制作）
            UPalMapObjectConcreteModelBase* concrete = model->GetConcreteModel(false);
            if (!concrete) continue;
            UPalMapObjectWorkeeModule* workee = concrete->GetWorkeeModule();
            if (!workee) continue;

            // 单类型工作台 (烹饪锅/篝火/熔炉等)：补满 CurrentWorkAmount
            UPalWorkProgress* wp = workee->GetWorkProgress();
            FastForwardWorkProgress(wp);

            // 多类型工作台 (装配线等, 一次多工人同时推进多个子进度):
            // 把所有子进度条的 CurrentProgress 补到 MaxProgress。
            UPalWorkProgressMultiType* multi = workee->GetWorkProgressMultiType();
            if (multi)
            {
                for (auto& entry : multi->ProgressEntries)
                {
                    if (entry.MaxProgress > 0.f && entry.CurrentProgress < entry.MaxProgress)
                        entry.CurrentProgress = entry.MaxProgress;
                }
            }
        }
    });
}

// 每帧调用 (带节流, 避免每帧全量遍历)
void TickBuildUnlock()
{
    if (!(cheatState.buildUnlockEnabled ||
          cheatState.buildIgnoreOverlap ||
          cheatState.buildFastBuild ||
          cheatState.buildIgnoreRequirements ||
          cheatState.buildNoConsumeMaterial))
        return;

    // 无视重叠: 每帧清空重叠检测
    if (cheatState.buildUnlockEnabled || cheatState.buildIgnoreOverlap || cheatState.buildNoConsumeMaterial)
    {
        UPalBuilderComponent* builder = GetPlayerBuilderComponent();
        if (builder) ClearOverlapChecks(builder);
    }

    // 快速建造/制作: 高频补满工作进度。原 500ms 节流导致"每完成一件物品
    // 要等约半秒才补下一件"——用户感知为"做完一个停 1 秒再做下一个"。
    // 改为 ~30ms (每帧级别), 使连续制作的每一件都瞬间完成, 整批无停顿。
    if (cheatState.buildFastBuild || cheatState.buildUnlockEnabled || cheatState.buildNoConsumeMaterial)
    {
        static ULONGLONG last = 0;
        ULONGLONG now = GetTickCount64();
        if (now - last > 30)
        {
            last = now;
            FastForwardAllBuildWork();
        }
    }

    // 解锁全建造/制作样式: 节流清空拒绝列表 (一次性但幂等)
    if (cheatState.buildUnlockEnabled || cheatState.buildIgnoreRequirements || cheatState.buildNoConsumeMaterial)
    {
        static ULONGLONG lastUnlock = 0;
        ULONGLONG now = GetTickCount64();
        if (now - lastUnlock > 1000)
        {
            lastUnlock = now;
            ForceBuildUnlock();
        }
    }
}

// 一次性调用: 解锁全建造和制作样式 (清空拒绝列表 + 解锁全部科技)
void ForceBuildUnlock()
{
    if (!cheatState.buildUnlockEnabled && !cheatState.buildIgnoreRequirements && !cheatState.buildNoConsumeMaterial)
        return;

    APalPlayerState* playerState = GetPalPlayerState();
    if (!playerState || !Helper::IsProbablyValidPtr(playerState))
        return;

    UPalTechnologyData* tech = playerState->TechnologyData;
    if (!tech || !Helper::IsProbablyValidPtr(tech))
        return;

    Helper::Try([&]
    {
        // 1) 清空拒绝列表 => 不被拒绝
        tech->DenyBuildObjectList.Clear();
        tech->DenyRecipeList.Clear();
        tech->DefaultLockRecipeNameArray.Clear();
        tech->DefaultLockBuildObjectNameArray.Clear();

        // 2) 解锁全部科技: 把每个可解锁科技名加入已解锁数组。
        //    "解锁全建造和制作样式"在 UI 上除了要看 IsDenied* (已由
        //    Hooking 查询层强制), 有些界面还按 UnlockedTechnologyNameArray
        //    判断是否显示, 这里直接填满双保险。
        const TArray<FName> rows = tech->GetRecipeTechnologyRowNameArray(false);
        for (const FName& row : rows)
        {
            if (row.IsNone()) continue;
            // TArray 无 Find: 手写线性查找避免重复
            bool bAlready = false;
            for (const FName& u : tech->UnlockedTechnologyNameArray)
            {
                if (u == row) { bAlready = true; break; }
            }
            if (!bAlready)
                tech->UnlockedTechnologyNameArray.Add(row);
        }
        // 触发 RepNotify, 让 UI / 存档立即刷新
        tech->OnRep_UnlockedTechnologyNameArray();

        pal::log::Log(pal::log::Level::Debug, "build", "[BuildUnlock] ForceBuildUnlock: denied cleared, {} technologies unlocked.\n", rows.Num());
    });
}
