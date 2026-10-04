// features/FeatureDispatch.cpp — ProcessEvent 功能分发 (C++23 重写版)
//
// 原 Hooking.cpp 的重写要点:
//   - 分发规则具名化: 每条规则一个 handler 函数, 按序匹配, 命中即停
//   - 日志统一走 pal::log (std::format), 删除自建的 CaptureLog/printf 落盘
//   - 删除自带的 SEH 外壳与 oProcessEvent 裸指针 (core::Hooks 统一提供)
//   - 参数偏移访问收口到 Field<T>(), thunk 钩子用模板统一生成
//   - 行为与原版一一对应: 偏移、命中条件、放行/吞掉的顺序均未改动

#include <pch.h>
#include "core/FrameDriver.hpp"
#include "core/Hooks.hpp"
#include "core/Log.hpp"
#include "engine/GameHelper.hpp"
#include "CheatState.hpp"

using namespace SDK;

namespace pal::features {

using pal::log::Level;

namespace {

// ---------------------------------------------------------------------------
// 事件与基础工具
// ---------------------------------------------------------------------------

struct PeEvent {
    SDK::UObject*    obj;
    SDK::UFunction*  func;
    void*            params;
    std::string_view full; // UFunction 全名 (core 层缓存, 调用期间有效)
};

template <typename T>
inline T* Field(void* params, size_t offset) {
    return reinterpret_cast<T*>(static_cast<std::byte*>(params) + offset);
}

inline bool Contains(std::string_view full, std::string_view key) {
    return full.find(key) != std::string_view::npos;
}

// 函数全名匹配: 自动覆盖 "A.B" 与 "A:B" 两种分隔符 (Dumper-7 版本差异)
inline bool IsFunc(std::string_view full, std::string_view dotted) {
    if (Contains(full, dotted)) return true;
    const size_t dot = dotted.find('.');
    if (dot == std::string_view::npos) return false;
    std::string colon(dotted);
    colon[dot] = ':';
    return Contains(full, colon);
}

inline void CallOriginal(const PeEvent& e) {
    if (auto fn = core::GetOriginalProcessEvent()) fn(e.obj, e.func, e.params);
}

template <typename T>
T* FindFirstObjectByFullName(std::string_view fullName) {
    if (!SDK::UObject::GObjects) return nullptr;
    SDK::UObject* found = nullptr;
    // 注意: Dumper-7 里 UObjectArray::Num() 返回 __int32 (编译器内建类型)
    const __int32 count = SDK::UObject::GObjects->Num();
    for (__int32 i = 0; i < count; ++i) {
        SDK::UObject* obj = nullptr;
        if (!Helper::Try([&] { obj = SDK::UObject::GObjects->GetByIndex(i); }) || !obj) continue;
        Helper::Try([&] {
            if (obj->GetFullName() == fullName) found = obj;
        });
        if (found) break;
    }
    return static_cast<T*>(found);
}

// 遍历 GObjects 中所有可读对象; fn 返回 false 提前终止
template <typename F>
void ForEachObject(F&& fn) {
    if (!SDK::UObject::GObjects) return;
    const __int32 count = SDK::UObject::GObjects->Num();
    for (__int32 i = 0; i < count; ++i) {
        SDK::UObject* obj = nullptr;
        if (!Helper::Try([&] { obj = SDK::UObject::GObjects->GetByIndex(i); }) || !obj) continue;
        if (!Helper::IsProbablyValidPtr(obj)) continue;
        if (!fn(obj)) return;
    }
}

// 资源伤害放大互斥标志: RPC 放大期间置 true, 阻止 CalcDamage 兜底二次放大
thread_local bool g_rpcScaling = false;

// ---------------------------------------------------------------------------
// 建造检查覆盖
// ---------------------------------------------------------------------------

bool IsBuildUnlockActive() {
    return cheatState.buildUnlockEnabled || cheatState.buildIgnoreRequirements ||
           cheatState.buildNoConsumeMaterial || cheatState.buildIgnoreGroundPlacement ||
           cheatState.buildAllowOverlapTerminal || cheatState.buildIgnoreNearBoss ||
           cheatState.buildIgnoreOil || cheatState.buildIgnoreBaseLimit ||
           cheatState.buildIgnoreCampLimit || cheatState.buildIgnoreObstacle ||
           cheatState.buildIgnoreOtherGuild || cheatState.buildIgnoreSupport ||
           cheatState.buildIgnoreCeiling || cheatState.buildIgnoreOverlap ||
           cheatState.buildIgnoreGroundContact || cheatState.buildIgnoreNearCamp ||
           cheatState.buildIgnoreUnderSea || cheatState.buildIgnoreHighPlace ||
           cheatState.buildIgnoreSlope || cheatState.buildIgnoreBaseRange ||
           cheatState.buildIgnoreIndoor || cheatState.buildIgnoreConnect ||
           cheatState.buildIgnoreWall;
}

// 精确屏蔽指定的建造失败结果码, 其余失败码保持不变
void IgnoreResult(SDK::EPalMapObjectOperationResult& rv, bool enabled,
                  SDK::EPalMapObjectOperationResult code) {
    if (enabled && rv == code) rv = SDK::EPalMapObjectOperationResult::Success;
}

// 按当前激活的"无视XXX"开关, 只屏蔽对应的建造失败结果码;
// 总开关开启时忽略全部失败码
void ForceBuildCheckSuccess(void* params) {
    if (!params) return;
    auto& rv = *Field<SDK::EPalMapObjectOperationResult>(params, 0);

    if (cheatState.buildUnlockEnabled || cheatState.buildIgnoreRequirements ||
        cheatState.buildNoConsumeMaterial) {
        log::Throttled(Level::Debug, "build", "BUILD_CHECK", 250, "result={} -> Success",
                       static_cast<unsigned>(rv));
        rv = SDK::EPalMapObjectOperationResult::Success;
        return;
    }

    IgnoreResult(rv, cheatState.buildIgnoreGroundPlacement, SDK::EPalMapObjectOperationResult::FailedNotInstallBySurface);
    IgnoreResult(rv, cheatState.buildAllowOverlapTerminal,  SDK::EPalMapObjectOperationResult::FailOverlap);
    IgnoreResult(rv, cheatState.buildIgnoreNearBoss,        SDK::EPalMapObjectOperationResult::FailedCannotInstallInRaidBossArea);
    IgnoreResult(rv, cheatState.buildIgnoreNearBoss,        SDK::EPalMapObjectOperationResult::FailedExceedMaxBuildCountInRaidBossArea);
    IgnoreResult(rv, cheatState.buildIgnoreNearBoss,        SDK::EPalMapObjectOperationResult::FailedCannotInstallOutOfBaseCampInRaidBossArea);
    IgnoreResult(rv, cheatState.buildIgnoreRequirements || cheatState.buildNoConsumeMaterial,
                 SDK::EPalMapObjectOperationResult::FailedNotEnoughMaterials);
    IgnoreResult(rv, cheatState.buildIgnoreOil,             SDK::EPalMapObjectOperationResult::FailedNotExistsItemProviderPlaceOn);
    IgnoreResult(rv, cheatState.buildIgnoreBaseLimit,       SDK::EPalMapObjectOperationResult::FailedExceedMaxNumInBaseCamp);
    IgnoreResult(rv, cheatState.buildIgnoreBaseLimit,       SDK::EPalMapObjectOperationResult::FailedBuildingLimit);
    IgnoreResult(rv, cheatState.buildIgnoreCampLimit,       SDK::EPalMapObjectOperationResult::FailedOverflowBaseCampNumInGuild);
    IgnoreResult(rv, cheatState.buildIgnoreCampLimit,       SDK::EPalMapObjectOperationResult::FailedOverflowBaseCampNumInWorld);
    IgnoreResult(rv, cheatState.buildIgnoreObstacle,        SDK::EPalMapObjectOperationResult::FailedIntersectOtherObject);
    IgnoreResult(rv, cheatState.buildIgnoreOtherGuild,      SDK::EPalMapObjectOperationResult::FailedOtherGuildBaseCampArea);
    IgnoreResult(rv, cheatState.buildIgnoreOtherGuild,      SDK::EPalMapObjectOperationResult::FailedOtherGuildBaseCampAreaPaint);
    IgnoreResult(rv, cheatState.buildIgnoreSupport,         SDK::EPalMapObjectOperationResult::FailLackSupportedLevel);
    IgnoreResult(rv, cheatState.buildIgnoreCeiling,         SDK::EPalMapObjectOperationResult::FailNotAttachToCeil);
    IgnoreResult(rv, cheatState.buildIgnoreOverlap,         SDK::EPalMapObjectOperationResult::FailOverlap);
    IgnoreResult(rv, cheatState.buildIgnoreOverlap,         SDK::EPalMapObjectOperationResult::FailedIntersectOtherObject);
    IgnoreResult(rv, cheatState.buildIgnoreGroundContact,   SDK::EPalMapObjectOperationResult::FailedAccessPointCannotGrounded);
    IgnoreResult(rv, cheatState.buildIgnoreGroundContact,   SDK::EPalMapObjectOperationResult::FailedChestCannotGrounded);
    IgnoreResult(rv, cheatState.buildIgnoreNearCamp,        SDK::EPalMapObjectOperationResult::FailedTooNearOtherBaseCampArea);
    IgnoreResult(rv, cheatState.buildIgnoreUnderSea,        SDK::EPalMapObjectOperationResult::FailedUnderOceanPlane);
    IgnoreResult(rv, cheatState.buildIgnoreHighPlace,       SDK::EPalMapObjectOperationResult::FailedExceedMaxZ);
    IgnoreResult(rv, cheatState.buildIgnoreSlope,           SDK::EPalMapObjectOperationResult::FailedTooSteepSlopeAngle);
    IgnoreResult(rv, cheatState.buildIgnoreBaseRange,       SDK::EPalMapObjectOperationResult::FailedCannotInstallNotOnBase);
    IgnoreResult(rv, cheatState.buildIgnoreIndoor,          SDK::EPalMapObjectOperationResult::FailedCannotInstallNotInDoor);
    IgnoreResult(rv, cheatState.buildIgnoreConnect,         SDK::EPalMapObjectOperationResult::FailNotConnectToOther);
    IgnoreResult(rv, cheatState.buildIgnoreWall,            SDK::EPalMapObjectOperationResult::FailNotAttachToWall);
}

// ---------------------------------------------------------------------------
// 地图 / 传送
// ---------------------------------------------------------------------------

bool HandleMapOnSetup(const PeEvent& e) {
    if (!Contains(e.full, "WBP_Map_Base.WBP_Map_Base_C:OnSetup") &&
        !Contains(e.full, "WBP_Map_Base.WBP_Map_Base_C.OnSetup"))
        return false;

    CallOriginal(e); // 先让 widget 完成 setup, 再补丁刚创建的地图参数
    Helper::Try([] {
        int patched = 0;
        ForEachObject([&patched](SDK::UObject* obj) {
            if (!obj->IsA(SDK::UPalHUDDispatchParameter_WorldMap::StaticClass())) return true;
            auto* p = static_cast<SDK::UPalHUDDispatchParameter_WorldMap*>(obj);
            p->CanFastTravel = cheatState.mapFreeTeleport;
            p->ForRespawn    = false;
            p->IsInitSelect  = false;
            ++patched;
            return true;
        });
        log::Log(Level::Debug, "map", "补丁 {} 个 WorldMap 参数实例", patched);
    });
    return true;
}

// A) mapFreeTeleport: 解除"必须站在传送点旁才能传送" (IsEnableFastTravel -> true)
// B) allFastTravelUnlocked: 所有传送点"已解锁"
bool HandleFastTravel(const PeEvent& e) {
    if (!e.params) return false;

    const bool isEnableFastTravel = IsFunc(e.full, "PalLocationPoint.IsEnableFastTravel");
    const bool isUnlockMapPoint   = IsFunc(e.full, "PalLocationPointFastTravel.IsUnlockMapPoint") ||
                                    IsFunc(e.full, "PalLevelObjectUnlockableFastTravelPoint.IsUnlocked");
    if (!isEnableFastTravel && !isUnlockMapPoint) return false;
    if (!(cheatState.mapFreeTeleport && isEnableFastTravel) &&
        !(cheatState.allFastTravelUnlocked && isUnlockMapPoint))
        return false;

    CallOriginal(e);
    *Field<bool>(e.params, 0) = true;
    return true;
}

// 地图 UI 参数创建/推送时立即打补丁 (第二道保险)
bool HandleWorldMapParams(const PeEvent& e) {
    if (!e.params) return false;

    auto PatchIfWorldMap = [](SDK::UPalHUDDispatchParameterBase* base) {
        if (!base) return;
        const UClass* cls = SDK::UPalHUDDispatchParameter_WorldMap::StaticClass();
        if (!cls || !base->IsA(cls)) return;
        auto* wm = static_cast<SDK::UPalHUDDispatchParameter_WorldMap*>(base);
        if (cheatState.mapFreeTeleport) {
            wm->CanFastTravel = true;
            wm->ForRespawn    = false;
            wm->IsInitSelect  = false;
        } else {
            wm->CanFastTravel = false;
        }
    };

    if (IsFunc(e.full, "PalHUDService.Push")) {
        PatchIfWorldMap(*Field<SDK::UPalHUDDispatchParameterBase*>(e.params, 0x08));
        return false; // 继续原始调用, 保证 HUD 正常入栈
    }

    if (Contains(e.full, "CreateDispatchParameterForK2Node")) {
        CallOriginal(e);
        PatchIfWorldMap(*Field<SDK::UPalHUDDispatchParameterBase*>(e.params, 0x10));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 捕获
// ---------------------------------------------------------------------------

// 纯查询类函数: 先算原始值, 再覆盖返回值
bool CaptureForceResult(const PeEvent& e, size_t offset, bool value) {
    CallOriginal(e);
    *Field<bool>(e.params, offset) = value;
    return true;
}

bool HandleCapture(const PeEvent& e) {
    if (!(cheatState.palCapture100 || cheatState.canCatchTowerBoss) || !e.params) return false;
    const bool capture = cheatState.palCapture100;

    // 捕获等级拉满: 任意判定路径都必然成功 (参数改写后继续原始流程)
    if (capture && Contains(e.full, "SetCaptureLevelForSphere")) {
        *Field<int32>(e.params, 0x10) = 999999;
        return false;
    }
    // 暴击标志拉满
    if (capture && Contains(e.full, "SetCriticalCaptureFlagForSphere")) {
        *Field<bool>(e.params, 0x10) = true;
        return false;
    }

    // 捕获概率计算 -> 100% (ReturnValue 为百分比 0~100)
    if (Contains(e.full, "CalcCaptureRate") &&
        (IsFunc(e.full, "BP_PalGameSetting_C.CalcCaptureRate") ||
         IsFunc(e.full, "PalGameSetting.CalcCaptureRate") ||
         IsFunc(e.full, "PalUtility.CalcCaptureRateByStatus"))) {
        const size_t off = IsFunc(e.full, "PalUtility.CalcCaptureRateByStatus") ? 0x08 : 0x1C;
        CallOriginal(e);
        *Field<float>(e.params, off) = 100.0f;
        return true;
    }

    // 捕捉随机掷骰 (LotteryFloat/LotteryInt) -> 恒成功
    if (capture && (IsFunc(e.full, "PalUtility.LotteryFloat") || IsFunc(e.full, "PalUtility.LotteryInt")))
        return CaptureForceResult(e, 0x04, true);

    // 最终判定 JudgePalCapture -> 恒成功 (排除 TryAllPhase: 它的返回是输出数组)
    if (capture && IsFunc(e.full, "PalUtility.JudgePalCapture") && !Contains(e.full, "TryAllPhase"))
        return CaptureForceResult(e, 0x08, true);

    // 多阶段判定总管: outJudgeFlagArray @0x18 逐阶段全 true
    if (capture && IsFunc(e.full, "PalUtility.JudgePalCapture_TryAllPhase")) {
        CallOriginal(e);
        if (auto* flags = Field<SDK::TArray<bool>>(e.params, 0x18); flags && flags->GetDataPtr())
            for (int32 i = 0; i < flags->Num(); ++i) (*flags)[i] = true;
        return true;
    }

    // 官方调试开关查询: IsCaptureSuccessAlways -> true
    if (capture && IsFunc(e.full, "PalCheatManager.IsCaptureSuccessAlways"))
        return CaptureForceResult(e, 0x00, true);

    // 权威兜底: ChallengeCapture 的 capturePower 拉满
    if (capture && IsFunc(e.full, "PalCaptureJudgeObject.ChallengeCapture")) {
        *Field<float>(e.params, 0x08) = 999999.0f;
        CallOriginal(e);
        return true;
    }

    // 捕捉失败事件转成功 (核心): BP_PalCaptureJudgeObject_C.OnFailedByTest / OnFailedByMP
    // native 概率计算不经 ProcessEvent, 真正经过 PE 的判定出口是这两个蓝图失败事件。
    if (capture && e.obj && Contains(e.full, "CaptureJudgeObject") &&
        (Contains(e.full, "OnFailedByTest") || Contains(e.full, "OnFailedByMP"))) {
        log::Log(Level::Debug, "cap", "拦截失败事件: {}", e.full);

        // FCaptureResult @ +0x08: IsSuccess / TestSuccessCount / FailedCaptureType
        if (std::byte* result = Field<std::byte>(e.params, 0x08); Helper::IsProbablyValidPtr(result)) {
            *reinterpret_cast<bool*>(result)      = true; // IsSuccess
            *reinterpret_cast<int32*>(result + 4) = 3;    // TestSuccessCount >= 1
            *(result + 8)                         = std::byte{0}; // FailedCaptureType = None
        }

        // 跳过失败流程, 以补丁后的参数直接调 OnCaptureSuccess
        UFunction* onSuccess = nullptr;
        Helper::Try([&] {
            onSuccess = e.obj->Class->GetFunction("BP_PalCaptureJudgeObject_C", "OnCaptureSuccess");
            if (!onSuccess) onSuccess = e.obj->Class->GetFunction("PalCaptureJudgeObject", "OnCaptureSuccess");
        });
        if (onSuccess) {
            if (auto fn = core::GetOriginalProcessEvent()) fn(e.obj, onSuccess, e.params);
            return true;
        }
        log::Warn("cap: OnCaptureSuccess 未找到, 回退原始失败流程");
        return false;
    }

    // 兜底: CaptureResult_ToALL 的最终结果 IsSuccess 强制 true
    if (capture && Contains(e.full, "CaptureResult_ToALL")) {
        *Field<bool>(e.params, 0x08) = true;
        return false;
    }

    // ---- 抓塔主 ----
    if (cheatState.canCatchTowerBoss && IsFunc(e.full, "PalUtility.IsUncapturable"))
        return CaptureForceResult(e, 0x00, false);
    if (cheatState.canCatchTowerBoss && IsFunc(e.full, "PalUtility.IsForceCapturable"))
        return CaptureForceResult(e, 0x00, true);
    if (cheatState.canCatchTowerBoss && IsFunc(e.full, "PalUtility.SetUncapturable")) {
        *Field<bool>(e.params, 0x00) = false;
        CallOriginal(e);
        return true;
    }
    if (cheatState.canCatchTowerBoss && IsFunc(e.full, "PalUtility.SetForceCapturable")) {
        *Field<bool>(e.params, 0x00) = true;
        CallOriginal(e);
        return true;
    }
    return false;
}

// 球体对象内部状态兜底: 蓝图 setter 可能用原始值回写实例字段, 调原 setter 后覆写
//   CaptureLevel@0x2D0 / isSneakBonus@0x2F8 / IsCriticalCapture@0x320
bool HandleCaptureBodySetters(const PeEvent& e) {
    if (!cheatState.palCapture100 || !e.obj || !e.params) return false;
    if (!Contains(e.full, "BP_PalCaptureBodyBase_C.")) return false;

    const bool isLevel    = Contains(e.full, "SetCaptureLevelInternal")    || Contains(e.full, "SetCaptureLevelToALL");
    const bool isCritical = Contains(e.full, "SetCriticalCaptureFlagInternal") || Contains(e.full, "SetCriticalFlag");
    const bool isSneak    = Contains(e.full, "SetSneakBonusFlagInternal")  || Contains(e.full, "SetSneakBonusFlagToALL");
    if (!isLevel && !isCritical && !isSneak) return false;

    CallOriginal(e);
    if (std::byte* body = reinterpret_cast<std::byte*>(e.obj); Helper::IsProbablyValidPtr(body)) {
        if (isLevel)    *reinterpret_cast<int32*>(body + 0x2D0) = 999999;
        if (isCritical) *reinterpret_cast<bool*>(body + 0x320)  = true;
        if (isSneak)    *reinterpret_cast<bool*>(body + 0x2F8)  = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 隐身
// ---------------------------------------------------------------------------

bool HandleInvisible(const PeEvent& e) {
    if (!cheatState.invisible || !e.params) return false;

    // 仇恨累积/主动增加 -> 吞掉, 被攻击目标不反击
    if (IsFunc(e.full, "PalHate.DamageEvent") || IsFunc(e.full, "PalHate.ChangeHate"))
        return true;

    // 犯罪判定 -> 原始逻辑照常, 罪犯识别由仇恨阻断兜底
    if (IsFunc(e.full, "PalWorldSecurityUtility.ResolveCriminalActor") ||
        Contains(e.full, "PalWorldSecurityLawTrigger_CharacterDamaged.Condition")) {
        CallOriginal(e);
        return true;
    }

    // AI 目标查询 -> 玩家恒不可选
    if (Contains(e.full, "CanTargetFromAI")) {
        CallOriginal(e);
        *Field<bool>(e.params, 0) = false;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 资源伤害 (树木/岩石/建筑等非角色目标)
// ---------------------------------------------------------------------------

bool HandleResourceDamage(const PeEvent& e) {
    if (cheatState.weaponDamage <= 1 || !e.params) return false;

    // 本地伤害结算兜底: PalUtility.CalcDamage 返回最终伤害 (int32 @0x140)。
    // 单机下 RPC 层已放大的话再放大一次会变成倍率平方, 用互斥标志防重入。
    if (!g_rpcScaling && IsFunc(e.full, "PalUtility.CalcDamage") &&
        !Contains(e.full, "CalcDamageCharacter")) {
        AActor* defender = nullptr;
        Helper::Try([&] { defender = *Field<AActor*>(e.params, 0x130); });

        bool isCharacter = false;
        if (defender && Helper::IsProbablyValidPtr(defender))
            Helper::Try([&] { isCharacter = defender->IsA(SDK::APalCharacter::StaticClass()); });

        if (!isCharacter) {
            CallOriginal(e);
            auto& dmg = *Field<int32>(e.params, 0x140);
            dmg = static_cast<int32>(static_cast<int64>(dmg) * cheatState.weaponDamage);
            return true;
        }
        return false; // 角色目标不放大, 继续原始流程
    }

    // 网络层 RPC: 在序列化前放大伤害值 (DamageInfo 基址随 RPC 不同)
    struct RpcInfo { std::string_view name; size_t infoOffset; };
    static constexpr std::array kRpcs{
        RpcInfo{"PalNetworkMapObjectComponent.RequestDamageMapObject_ToServer",        0x10},
        RpcInfo{"PalNetworkMapObjectComponent.RequestDamageFoliage_ToServer",          0x30},
        RpcInfo{"PalNetworkMapObjectComponent.RequestDamageMapObjectSpawner_ToServer", 0x08},
    };

    for (const auto& rpc : kRpcs) {
        if (!IsFunc(e.full, rpc.name)) continue;

        auto* nativeVal = Field<int32>(e.params, rpc.infoOffset);
        auto* basePower = Field<int32>(e.params, rpc.infoOffset + 0x4);

        g_rpcScaling = true;
        if (*nativeVal > 0) *nativeVal = static_cast<int32>(static_cast<int64>(*nativeVal) * cheatState.weaponDamage);
        if (*basePower > 0) *basePower = static_cast<int32>(static_cast<int64>(*basePower) * cheatState.weaponDamage);
        CallOriginal(e);
        g_rpcScaling = false;
        return true;
    }
    return false;
}

// 采集掉落放大: 服务器权威结算入口的 CollectionObjectDamageRate
//   原本 >0 -> 乘以倍率; 原本 =0 -> 设为倍率 (启用采集掉落路径)
bool HandleDropScaling(const PeEvent& e) {
    if (cheatState.weaponDamage <= 1 || !e.params) return false;
    if (!IsFunc(e.full, "PalMapObjectItemDropOnDamagModel.OnDamage_ServerInternal")) return false;

    auto* rate = Field<float>(e.params, 0x08 + 0xD0);
    if (*rate > 0.0f) *rate *= static_cast<float>(cheatState.weaponDamage);
    else              *rate  = static_cast<float>(cheatState.weaponDamage);

    CallOriginal(e);
    return true;
}

// ---------------------------------------------------------------------------
// 科技 / 建造解锁
// ---------------------------------------------------------------------------

bool HandleTechUnlock(const PeEvent& e) {
    if (!e.params) return false;
    if (!(cheatState.buildUnlockEnabled || cheatState.buildIgnoreRequirements ||
          cheatState.buildNoConsumeMaterial))
        return false;

    static constexpr std::array deniedNames{
        "PalTechnologyData.IsDeniedBuildObject", "PalTechnologyData.IsDeniedRecipe",
        "PalTechnologyData.IsDeniedTechnology",
    };
    static constexpr std::array unlockNames{
        "PalTechnologyData.IsUnlockBuildObject", "PalTechnologyData.IsUnlockCraftRecipe",
        "PalTechnologyData.IsUnlockRecipeTechnology", "PalTechnologyData.IsUnlockableRecipeTechnology",
    };

    const bool denied = std::any_of(deniedNames.begin(), deniedNames.end(),
                                    [&](std::string_view n) { return IsFunc(e.full, n); });
    const bool unlock = std::any_of(unlockNames.begin(), unlockNames.end(),
                                    [&](std::string_view n) { return IsFunc(e.full, n); });
    if (!denied && !unlock) return false;

    CallOriginal(e);
    *Field<bool>(e.params, 0x08) = unlock; // denied -> false, unlock -> true
    return true;
}

bool HandleBuildUnlock(const PeEvent& e) {
    if (!e.params) return false;
    if (!(IsBuildUnlockActive() || cheatState.buildNoDismantle)) return false;
    const bool noConsume = cheatState.buildNoConsumeMaterial;

    // 建造请求: 官方调试参数 FPalBuildRequestDebugParameter.bNotConsumeMaterials @0x50
    if (noConsume && IsFunc(e.full, "PalNetworkPlayerComponent.RequestBuild_ToServer")) {
        *Field<bool>(e.params, 0x50) = true;
        log::Throttled(Level::Debug, "build", "REQ_BUILD", 1000, "RequestBuild_ToServer 命中");
        return false;
    }

    // 制作消耗入口: 只兜底不跳过 —— 原函数还负责推进生产 (减 RemainProductNum/产出到容器)
    if (noConsume && IsFunc(e.full, "PalWorkProgressMultiType.CheckAndConsumeForProduction")) {
        CallOriginal(e);
        return true;
    }

    // 容器/UI 材料检查查询: 强制"有材料"
    if (noConsume && IsFunc(e.full, "PalItemContainerMultiHelper.IsExistItems")) {
        CallOriginal(e);
        *Field<bool>(e.params, 0x10) = true;
        return true;
    }
    if (noConsume && IsFunc(e.full, "PalUIInventoryModel.IsExistItems")) {
        CallOriginal(e);
        *Field<bool>(e.params, 0x10) = true;
        return true;
    }
    // 可制作数量: 材料不足时返回 0 会锁死数量框
    if (noConsume && IsFunc(e.full, "PalUIProductSettingModel.CalcMaxProductableNum")) {
        CallOriginal(e);
        *Field<int32>(e.params, 0) = 9999;
        return true;
    }
    // 开始制作可用性: 强制 Enable
    if (noConsume && IsFunc(e.full, "PalUIConvertItemModel.CanStartProduction")) {
        CallOriginal(e);
        *Field<uint8>(e.params, 0) = static_cast<uint8>(SDK::EPalUIConvertItemRequestStartResponse::Enable);
        return true;
    }
    // 物品数量检查 (64 位必须先于 32 位匹配, 否则 GetItemStackCount 前缀误匹配)
    if (noConsume && IsFunc(e.full, "PalItemContainer.GetItemStackCount64")) {
        CallOriginal(e);
        *Field<int64>(e.params, 0x08) = 9999;
        return true;
    }
    if (noConsume && IsFunc(e.full, "PalItemContainer.GetItemStackCount")) {
        CallOriginal(e);
        *Field<int32>(e.params, 0x08) = 9999;
        return true;
    }

    // Builder 放置/涂装/拆卸查询
    if (const bool isBuilder = Contains(e.full, "PalBuilderComponent.") ||
                               Contains(e.full, "PalBuilderComponent:"); isBuilder) {
        const bool buildCheck = Contains(e.full, "IsEnableBuild") || Contains(e.full, "CanRequestBuild") ||
                                Contains(e.full, "CheckBuild")    || Contains(e.full, "BuildCheck") ||
                                Contains(e.full, "CanRequestPaint") || Contains(e.full, "IsEnablePaint");
        if (buildCheck) {
            CallOriginal(e);
            ForceBuildCheckSuccess(e.params);
            return true;
        }
        if (noConsume && IsFunc(e.full, "PalBuilderComponent.IsExistsMaterialForBuildObject")) {
            CallOriginal(e);
            *Field<bool>(e.params, 0x98) = true;
            return true;
        }
        const bool dismantleCheck = IsFunc(e.full, "PalBuilderComponent.CanRequestDismantle") ||
                                    IsFunc(e.full, "PalBuilderComponent.IsEnableDismantle");
        if (dismantleCheck && cheatState.buildNoDismantle) {
            CallOriginal(e);
            if (Contains(e.full, "IsEnableDismantle"))
                *Field<bool>(e.params, 0) = false;
            else
                *Field<SDK::EPalMapObjectOperationResult>(e.params, 0) =
                    SDK::EPalMapObjectOperationResult::FailedCannotDismantleGuildSecurityRestricted;
            return true;
        }
    }
    return false;
}

// 建造最终结果诊断 + 请求参数双保险写入
bool HandleBuildDiagnostics(const PeEvent& e) {
    if (!e.params) return false;

    if (Contains(e.full, "PalPlayerState.ReceiveBuildResult_ToRequestClient")) {
        CallOriginal(e);
        log::Log(Level::Debug, "build", "ReceiveBuildResult result={}",
                 static_cast<unsigned>(*Field<SDK::EPalMapObjectOperationResult>(e.params, 0)));
        return true;
    }
    if (cheatState.buildNoConsumeMaterial &&
        IsFunc(e.full, "PalNetworkPlayerComponent.RequestBuild_ToServer")) {
        *Field<bool>(e.params, 0x50) = true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 分发主流程 (规则按序匹配, 命中即停)
// ---------------------------------------------------------------------------

void Dispatch(const PeEvent& e) {
    if (HandleMapOnSetup(e))         return;
    if (HandleFastTravel(e))         return;
    if (HandleWorldMapParams(e))     return;
    if (HandleCapture(e))            return;
    if (HandleCaptureBodySetters(e)) return;
    if (HandleInvisible(e))          return;
    if (HandleResourceDamage(e))     return;
    if (HandleDropScaling(e))        return;
    if (HandleTechUnlock(e))         return;
    if (HandleBuildUnlock(e))        return;
    if (HandleBuildDiagnostics(e))   return;
    CallOriginal(e);
}

void ProcessEventListener(SDK::UObject* obj, SDK::UFunction* func, void* params,
                          const std::string& full) {
    if (!func) {
        if (auto fn = core::GetOriginalProcessEvent()) fn(obj, func, params);
        return;
    }

    // HUD 帧锚点: ReceiveDrawHUD 每帧携带画布, 驱动 Shadow-Gui 整帧
    if (full.starts_with("Function Engine.HUD.ReceiveDrawHUD")) {
        static unsigned long long s_hudHits = 0;
        if (++s_hudHits <= 3)
            log::Stage(std::format("收到 HUD 帧事件 第 {} 次: {}", s_hudHits, full));

        if (auto fn = core::GetOriginalProcessEvent()) fn(obj, func, params);
        core::FrameDriver::OnFrame(reinterpret_cast<SDK::AHUD*>(obj));
        return;
    }

    Dispatch({obj, func, params, full});
}

// ---------------------------------------------------------------------------
// Native 钩子: 完全绕过 ProcessEvent 的 C++ 直调函数
// ---------------------------------------------------------------------------

// 在 thunk 前搜索 call rel32 (E8) 指令, 返回真实实现地址
void* FindNativeImplFromThunk(void* thunk, int searchBytes = 0x100) {
    if (!thunk) return nullptr;
    auto* p = static_cast<uint8_t*>(thunk);
    for (int i = 0; i < searchBytes; ++i) {
        if (p[i] != 0xE8) continue;
        const int32_t rel = *reinterpret_cast<int32_t*>(p + i + 1);
        void* target = p + i + 5 + rel;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(target, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
            return target;
    }
    return nullptr;
}

void* ExecFunctionOf(SDK::UFunction* fn) {
    return fn ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(fn) + 0xD8) : nullptr;
}

// ---- 制作/建造材料检查 thunk 钩子 ----
// 说明: 实测 impl 定位对多个函数返回同一共享 stub (GetItemStackCount/64 同址、
// IsExistItems/IsExistsMaterial 同址), 用 C 签名 detour 挂共享 stub 会导致签名
// 不匹配卡死。而每个函数的 ExecFunction (thunk) 地址独立、签名统一为
// void(Context, TheStack, Result) —— 统一模板: 先调原 thunk, 再覆写结果, 安全。
using tNativeExec = void (*)(void* context, void* theStack, void* result);

struct OverrideAlwaysTrue   { static void Apply(void* r) { *static_cast<bool*>(r)   = true;  } };
struct OverrideCount32      { static void Apply(void* r) { *static_cast<int32*>(r)  = 9999; } };
struct OverrideCount64      { static void Apply(void* r) { *static_cast<int64*>(r)  = 9999; } };
struct OverrideStartEnabled { static void Apply(void* r) {
    *static_cast<uint8*>(r) = static_cast<uint8>(SDK::EPalUIConvertItemRequestStartResponse::Enable);
} };

template <typename Override>
struct ThunkHook {
    static inline tNativeExec orig = nullptr;

    static void Detour(void* ctx, void* stack, void* result) {
        if (orig) orig(ctx, stack, result); // 保持栈约定
        if (cheatState.buildNoConsumeMaterial && result) Override::Apply(result);
    }

    static bool Install(SDK::UFunction* fn) {
        void* exec = ExecFunctionOf(fn);
        return exec && core::hooks::CreateHook(exec, &Detour, reinterpret_cast<void**>(&orig));
    }
};

// CheckAndConsumeForProduction: 不能跳过原函数 —— 它同时推进生产
// (减 RemainProductNum / 产出到容器)。"不消耗材料"由官方调试标志负责
// (ApplyBuildConsumeFlags 每帧置位), 本钩子只在材料不足时兜底放行。
struct CheckAndConsumeHook {
    static inline bool (*orig)(SDK::UPalWorkProgressMultiType*) = nullptr;

    static bool Detour(SDK::UPalWorkProgressMultiType* self) {
        // 原函数读官方标志决定是否扣料, 并推进生产
        if (orig && orig(self)) return true;
        return cheatState.buildNoConsumeMaterial;
    }
};

void InstallNativeCraftHooks() {
    const bool ok = [&] {
        UFunction* fn = FindFirstObjectByFullName<SDK::UFunction>(
            "Function Pal.PalWorkProgressMultiType.CheckAndConsumeForProduction");
        void* impl = FindNativeImplFromThunk(ExecFunctionOf(fn));
        if (!impl) {
            log::Warn("craft: CheckAndConsumeForProduction 实现未定位");
            return false;
        }
        return core::hooks::CreateHook(impl, &CheckAndConsumeHook::Detour,
                                       reinterpret_cast<void**>(&CheckAndConsumeHook::orig));
    }();
    log::Log(ok ? Level::Info : Level::Warn, "craft",
             "CheckAndConsumeForProduction {}", ok ? "已挂钩" : "挂钩失败");

    // 其余材料检查统一走 thunk 钩子 (只 hook ExecFunction, 绝不 hook 共享 impl)
    using CraftThunk = ThunkHook<OverrideAlwaysTrue>;
    struct CraftEntry {
        std::string_view funcName;
        bool (*install)(SDK::UFunction*);
    };

    static constexpr std::array craftHooks{
        CraftEntry{"Function Pal.PalItemContainerMultiHelper.IsExistItems",   &CraftThunk::Install},
        CraftEntry{"Function Pal.PalItemContainer.GetItemStackCount64",       &ThunkHook<OverrideCount64>::Install},
        CraftEntry{"Function Pal.PalItemContainer.GetItemStackCount",         &ThunkHook<OverrideCount32>::Install},
        CraftEntry{"Function Pal.PalBuilderComponent.IsExistsMaterialForBuildObject", &CraftThunk::Install},
        CraftEntry{"Function Pal.PalUIProductSettingModel.CalcMaxProductableNum",     &ThunkHook<OverrideCount32>::Install},
        CraftEntry{"Function Pal.PalUIConvertItemModel.CanStartProduction",           &ThunkHook<OverrideStartEnabled>::Install},
        CraftEntry{"Function Pal.PalUIInventoryModel.IsExistItems",           &CraftThunk::Install},
    };

    for (const auto& entry : craftHooks) {
        UFunction* fn = FindFirstObjectByFullName<SDK::UFunction>(entry.funcName);
        const bool installed = fn && entry.install(fn);
        log::Log(installed ? Level::Debug : Level::Warn, "craft",
                 "{} {}", entry.funcName, installed ? "thunk 已挂钩" : "挂钩失败");
    }
}

// ---------------------------------------------------------------------------
// 官方调试标志同步 (每帧从 FrameDriver tick 调用, 不在钩子栈内)
// ---------------------------------------------------------------------------

// 收集全部 UPalDebugSetting 实例 (仅首次; 实例生命周期内稳定, 之后只写标志位)
std::vector<SDK::UPalDebugSetting*>& DebugSettings() {
    static std::vector<SDK::UPalDebugSetting*> s_instances;
    static bool s_collected = false;
    if (!s_collected) {
        s_collected = true;
        ForEachObject([](SDK::UObject* obj) {
            if (bool isDS = false; Helper::Try([&] { isDS = obj->IsA(SDK::UPalDebugSetting::StaticClass()); }) && isDS)
                s_instances.push_back(static_cast<SDK::UPalDebugSetting*>(obj));
            return true;
        });
        log::Log(Level::Debug, "flag", "收集到 {} 个 UPalDebugSetting 实例", s_instances.size());
    }
    return s_instances;
}

// UPalDebugSetting 调试标志偏移 (Dumper-7 实测)
struct DebugFlagOffsets {
    size_t noConsumeBuild    = 0x18D;
    size_t noConsumeCraft    = 0x234;
    size_t noConsumeRepair   = 0x165;
    size_t allRecipeUnlock   = 0x1A1;
    size_t selectableNoMats  = 0x236;
    size_t captureAlways     = 0x171;
    size_t captureFailBounce = 0x172;
};
constexpr DebugFlagOffsets kFlagOff{};

// 建造/制作不消耗材料: 每帧同步 (游戏加载时调试设置可能被重置)
void ApplyBuildConsumeFlags() {
    if (!cheatState.buildNoConsumeMaterial) return;

    // 官方 NetServer RPC 同步 (每秒最多 1 次, 低频)
    static unsigned long long s_lastRpc = 0;
    if (const auto now = GetTickCount64(); now - s_lastRpc >= 1000) {
        s_lastRpc = now;
        if (auto* pc = Helper::GetPalPlayerController(); pc && Helper::IsProbablyValidPtr(pc))
            Helper::Try([&] {
                pc->Debug_NotConsumeMaterialsInBuild();
                pc->Debug_NotConsumeMaterialsInCraft();
            });
    }

    for (SDK::UPalDebugSetting* ds : DebugSettings()) {
        if (!ds || !Helper::IsProbablyValidPtr(ds)) continue;
        auto* base = reinterpret_cast<std::byte*>(ds);
        *reinterpret_cast<bool*>(base + kFlagOff.noConsumeBuild)   = true;
        *reinterpret_cast<bool*>(base + kFlagOff.noConsumeCraft)   = true;
        *reinterpret_cast<bool*>(base + kFlagOff.noConsumeRepair)  = true;
        *reinterpret_cast<bool*>(base + kFlagOff.allRecipeUnlock)  = true;
        *reinterpret_cast<bool*>(base + kFlagOff.selectableNoMats) = true;
    }
}

// 捕获必中: 官方 CaptureSuccessAlways 调试标志
void ApplyCaptureAlwaysFlag() {
    if (!cheatState.palCapture100) return;

    static SDK::UPalDebugSetting* s_cached = nullptr;
    if (!s_cached || !Helper::IsProbablyValidPtr(s_cached)) {
        Helper::Try([&] { s_cached = SDK::UPalUtility::GetPalDebugSetting(); });
        if (!s_cached && !DebugSettings().empty()) s_cached = DebugSettings().front();
    }
    if (!s_cached || !Helper::IsProbablyValidPtr(s_cached)) return;

    auto* base = reinterpret_cast<std::byte*>(s_cached);
    *reinterpret_cast<bool*>(base + kFlagOff.captureAlways)     = true;
    *reinterpret_cast<bool*>(base + kFlagOff.captureFailBounce) = false;
}

// 官方作弊指令 CaptureSuccessAlways: 经 PalCheatManager 调用,
// 服务端判定读同一开关 (每帧调用, 无一次性守卫)
bool TryInvokeCheatManagerCaptureSuccessAlways() {
    SDK::UPalCheatManager* mgr = nullptr;
    Helper::Try([&] {
        if (SDK::UWorld* w = SDK::UWorld::GetWorld()) mgr = SDK::UPalUtility::GetPalCheatManager(w);
    });
    if (!mgr || !Helper::IsProbablyValidPtr(mgr)) return false;
    Helper::Try([&] { mgr->CaptureSuccessAlways(); });
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// 帧锚点: UGameViewportClient 虚表 (运行时自证, 不猜槽位)
//
// 前面几版逐条被日志否掉:
//   1) ProcessEvent 等 "Function Engine.HUD.ReceiveDrawHUD" —— Palworld 里该
//      事件不经过 ProcessEvent, 一次都没命中;
//   2) AHUD::ReceiveDrawHUD 的 thunk —— 能命中, 但 UE 的 PostRender 是调用完
//      ReceiveDrawHUD 才写 AHUD::Canvas, 回调期间读到的是野指针 → 0xC0000005;
//   3) 照搬 UE4.23 参考工程的 v[0x63] —— 引擎大版本不同, 槽位不可移植; 实测
//      本 build 的 v[0x3E] 与 v[0x3F] 指向同一地址且多线程高频调用, 参数也不是
//      UCanvas (ClipX/ClipY 读到 75x90、90x90 这类默认字号量级)。
//
// 因此改为运行时自证: 扫描虚表, 只挂落在 .text 内的槽, 用"调用频率 + 首参是否
// 是合理尺寸的 UCanvas"两个客观条件筛出真正的渲染回调。
//
// ---------------------------------------------------------------------------
// 定位结果已确认 (PostRenderScanner 实测, 2026-10-04):
//   PostRender = vtable[0x6D] (index 109), 模块偏移 0x52D6740
//   函数序言: 48 8B 01 48 FF A0 70 03 00 00 CC*6 40 53 48 83 EC ?? 48 8B D9 48 83
//   (mov rax,[rcx]; jmp qword ptr [rax+0x370]; int3 填充; 函数体开头)
// 现在的安装顺序: 已知槽位 + 序言特征码确认 → 直连挂钩;
// 特征码对不上 (版本变化) 才回退到下方的启发式全扫描。
// ---------------------------------------------------------------------------
namespace {

using tVtFn = void(__thiscall*)(void* self, void* arg0, void* arg1, void* arg2);

// 扫描范围: 只看 UGameViewportClient 自身的虚函数区。
//   v[0x00..0x37] 是 UObject 基类的槽 (析构/GetName/序列化/GC 等), 既不可能携带
//   画布, 也是误判与崩溃风险最高的地方 —— 上一版从 0x00 全扫, 注入瞬间就崩。
constexpr size_t kVtScanBegin = 0x38;
constexpr size_t kVtScanEnd   = 0x80;
constexpr size_t kMaxProbes   = kVtScanEnd - kVtScanBegin;
constexpr size_t kProbeCap    = 24;   // 探针数量上限: 命中画布后其余立即卸载

// 已知定位结果: PostRender 在 vtable[0x6D] (index 109, PostRenderScanner 实测)。
constexpr size_t kPostRenderSlot = 0x6D;

// 该槽位函数的序言特征码 ('?' 为通配符)。挂直连钩之前先校验, 防止版本
// 更新导致槽位漂移时把别的函数当成 PostRender。
constexpr unsigned char kPostRenderSig[] = {
    0x48, 0x8B, 0x01, 0x48, 0xFF, 0xA0,          // mov rax,[rcx]; jmp [rax+disp32]
    0x00, 0x00, 0x00, 0x00,                      //   disp32 (通配)
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,          // int3 填充
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x00,          // push rbx; sub rsp, imm8 (通配)
    0x48, 0x8B, 0xD9, 0x48, 0x83,                // mov rbx,rcx; ...
};
constexpr bool kPostRenderSigWildcard[] = {
    false, false, false, false, false, false,
    true,  true,  true,  true,
    false, false, false, false, false, false,
    false, false, false, false, false, true,
    false, false, false, false, false,
};

// 特征码校验: 目标函数前 sizeof(kPostRenderSig) 字节是否与已知序言一致。
bool PostRenderPrologueMatches(const void* p) {
    if (!p) return false;
    __try {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < sizeof(kPostRenderSig); ++i) {
            if (!kPostRenderSigWildcard[i] && b[i] != kPostRenderSig[i]) return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 判断 p 是否指向"可执行的已提交内存"。
// 这是唯一可靠的"这个地址是函数"判据 —— 必须同时满足:
//   - VirtualQuery 成功
//   - State == MEM_COMMIT
//   - Protect 含 PAGE_EXECUTE / PAGE_EXECUTE_READ / PAGE_EXECUTE_READWRITE /
//     PAGE_EXECUTE_WRITECOPY
// 上一版用"主模块基址 + 硬编码 .text 偏移"判断, 一旦主模块基址不是 exe 的
// 加载基址, 就会把 .rdata/.pdata 里的数据指针当成函数, MinHook 去写只读页 ->
// EXCEPTION_ACCESS_VIOLATION **writing**。
bool IsExecutableAddress(const void* p) {
    if (!p) return false;
    const auto a = reinterpret_cast<uintptr_t>(p);
    if (a < 0x10000 || a > 0x00007FFFFFFF0000ull) return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;

    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & exec) != 0;
}

// 函数序言粗检: 全 0 或全 0xCC (未初始化/调试填充) 的地址不是真函数。
bool LooksLikeFunction(const void* p) {
    if (!IsExecutableAddress(p)) return false;
    __try {
        const auto* b = static_cast<const unsigned char*>(p);
        const bool allZero = (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0);
        const bool allInt3 = (b[0] == 0xCC && b[1] == 0xCC && b[2] == 0xCC && b[3] == 0xCC);
        if (allZero || allInt3) return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// 试探写权限: 交给 MinHook 之前先确认目标页可写。
// 只读页直接跳过, 避免 MinHook 写 5 字节 jmp 时触发写入违例。
bool CanBePatched(void* p) {
    DWORD old = 0;
    if (!VirtualProtect(p, 8, PAGE_EXECUTE_READWRITE, &old)) return false;
    VirtualProtect(p, 8, old, &old); // 立刻还原, 真正改写交给 MinHook
    return true;
}

// 画布校验: UCanvas::ClipX@0x30 / ClipY@0x34 是本帧屏幕尺寸。
// 收紧到 >= 320x240, 杜绝上一次那种 75x90 / 90x90 的假画布。
bool LooksLikeCanvas(const void* p) {
    if (!p || !Helper::IsProbablyValidPtr(p)) return false;
    bool ok = false;
    Helper::Try([&] {
        const auto* canvas = static_cast<const SDK::UCanvas*>(p);
        ok = canvas->ClipX >= 320.0f && canvas->ClipX <= 16384.0f &&
             canvas->ClipY >= 240.0f && canvas->ClipY <= 16384.0f;
    });
    return ok;
}

// 每个虚表槽的探针统计
struct ProbeStats {
    size_t      slot = 0;
    const void* address = nullptr;   // 原始虚表槽地址 (去重/展示用)
    void*       original = nullptr;  // MinHook trampoline
    unsigned long long calls = 0;
    unsigned long long canvasHits = 0;
    float lastClipX = 0.f;
    float lastClipY = 0.f;
    const void* lastArg0 = nullptr;
};

ProbeStats g_probes[kMaxProbes];
size_t     g_probeCount = 0;
bool       g_probeInstalled = false;

// 通用探测器: 记录画布校验结果, 并把命中画布的调用转给帧驱动。
// 参数映射同 DirectPostRenderDetour: canvas = RDX (第 2 个 detour 参数)。
// PostRender 只有两个参数 (this, Canvas), R8/R9 是残留值, 不碰。
void __fastcall ProbeDetour(void* self, void* canvas, void* a1, void* a2, void* a3) {
    (void)self; (void)a1; (void)a2; (void)a3;
    (void)canvas;
}

struct ViewportHooks {
    // 已确认承载画布的槽 (只允许一个, 避免同一帧被驱动多次)
    static bool canvasSlotFound;

    // 命中画布: 交帧给驱动, 并把其余探针全部卸载。
    // 找到目标之后, 剩下的探针没有任何价值, 留着只会扩大崩溃面。
    static void FeedAndRetire(void* canvas) {
        if (!LooksLikeCanvas(canvas)) return;

        if (!canvasSlotFound) {
            canvasSlotFound = true;
            log::Stage("已确认渲染回调槽, 卸载其余探针");
        }

        core::FrameDriver::OnFrameWithCanvas(static_cast<SDK::UCanvas*>(canvas));
    }

    // 每个槽一个独立 detour (MinHook 要求 detour 地址各不相同)
    template <size_t Slot>
    static void __fastcall Detour(void* self, void* canvas, void* a1, void* a2, void* a3) {
        ProbeStats& st = g_probes[Slot];
        ++st.calls;
        st.lastArg0 = canvas;

        const bool hit = LooksLikeCanvas(canvas);
        if (hit) {
            ++st.canvasHits;
            Helper::Try([&] {
                auto* c = static_cast<const SDK::UCanvas*>(canvas);
                st.lastClipX = c->ClipX;
                st.lastClipY = c->ClipY;
            });
        }

        // 透传原函数 —— 必须先透传再绘制, 保证游戏渲染不被我们的调用影响
        auto orig = reinterpret_cast<tVtFn>(st.original);
        if (orig) orig(self, canvas, a1, a2);

        // 命中画布 = 这个槽就是渲染回调: 交帧给驱动, 并停掉其余探针
        if (hit) FeedAndRetire(canvas);
    }
};

bool ViewportHooks::canvasSlotFound = false;

// 直连模式的 detour: 只挂在已确认的 PostRender 槽 (v[0x6D]) 上。
//
// 参数映射 (x64 __fastcall: RCX, RDX, R8, R9, [rsp+0x28]):
//   self   = RCX = UGameViewportClient* this
//   canvas = RDX = UCanvas* (PostRender 的第 2 个参数) ← 画布在这里!
//   a1/a2/a3 = R8/R9/栈 (PostRender 没有更多参数, 是残留值)
// 上一版把 a0 (R8) 当画布校验, 每帧都失败, 帧驱动永远收不到帧。
void __fastcall DirectPostRenderDetour(void* self, void* canvas, void* a1, void* a2, void* a3) {
    ProbeStats& st = g_probes[0];
    ++st.calls;
    st.lastArg0 = canvas;

    const bool hit = LooksLikeCanvas(canvas);
    if (hit) ++st.canvasHits;

    // 诊断: 确认 detour 是否被调用 / 画布校验是否命中 (各打一次 + 降频)
    static unsigned long long s_diagCount = 0;
    if (++s_diagCount == 1 || s_diagCount == 1000 || s_diagCount % 20000 == 0) {
        float cx = -1.f, cy = -1.f;
        Helper::Try([&] {
            auto* c = static_cast<const SDK::UCanvas*>(canvas);
            cx = c->ClipX; cy = c->ClipY;
        });
        log::Stage(std::format("直连 detour 命中: calls={} 画布命中={} canvas={:#x} clip={:.0f}x{:.0f}",
                               s_diagCount, hit ? "是" : "否",
                               reinterpret_cast<uintptr_t>(canvas), cx, cy));
    }

    // 透传原函数 —— 画布必须放回第 2 个参数位 (RDX), 原函数才能收到正确值
    auto orig = reinterpret_cast<tVtFn>(st.original);
    if (orig) orig(self, canvas, a1, a2);

    if (hit) ViewportHooks::FeedAndRetire(canvas);
}

// 生成 0..kMaxProbes-1 的 detour 表: 用模板递归展开, 避免手写 160 个函数。
template <size_t N>
struct DetourTable {
    static void Fill(std::array<void*, kMaxProbes>& table) {
        DetourTable<N - 1>::Fill(table);
        table[N - 1] = reinterpret_cast<void*>(&ViewportHooks::Detour<N - 1>);
    }
};
template <>
struct DetourTable<0> {
    static void Fill(std::array<void*, kMaxProbes>&) {}
};

const std::array<void*, kMaxProbes>& Detours() {
    static const std::array<void*, kMaxProbes> table = [] {
        std::array<void*, kMaxProbes> t{};
        DetourTable<kMaxProbes>::Fill(t);
        return t;
    }();
    return table;
}

// 找到一个可用的 UGameViewportClient 实例 (用于读虚表)。
SDK::UGameViewportClient* FindViewportClient() {
    SDK::UGameViewportClient* vp = nullptr;
    Helper::Try([&] {
        if (SDK::UEngine* engine = SDK::UEngine::GetEngine())
            vp = engine->GameViewport;
    });
    if (vp && Helper::IsProbablyValidPtr(vp)) return vp;

    vp = nullptr;
    Helper::Try([&] {
        SDK::UWorld* world = SDK::UWorld::GetWorld();
        if (!world || !world->OwningGameInstance) return;
        auto& locals = world->OwningGameInstance->LocalPlayers;
        if (locals.Num() > 0 && locals[0]) vp = locals[0]->ViewportClient;
    });
    return (vp && Helper::IsProbablyValidPtr(vp)) ? vp : nullptr;
}

} // namespace

void DumpViewportProbes() {
    if (!g_probeInstalled) return;
    std::string report;
    for (size_t i = 0; i < g_probeCount; ++i) {
        const ProbeStats& st = g_probes[i];
        if (st.calls == 0) continue;
        report += std::format("  v[0x{:02X}] calls={} 画布命中={} clip={:.0f}x{:.0f} arg0={}\r\n",
                              st.slot, st.calls, st.canvasHits, st.lastClipX, st.lastClipY,
                              st.lastArg0);
    }
    if (!report.empty())
        log::Stage(std::string("虚表探针统计:\r\n") + report);
}

void InstallViewportHook() {
    SDK::UGameViewportClient* vp = FindViewportClient();
    if (!vp) {
        log::Stage("视口钩子: 未找到 UGameViewportClient");
        log::Warn("[frame] 未找到 UGameViewportClient, 帧驱动未安装");
        return;
    }

    void** vt = *reinterpret_cast<void***>(vp);
    if (!vt || !Helper::IsProbablyValidPtr(vt)) {
        log::Stage("视口钩子: 虚表指针无效");
        return;
    }

    log::Stage(std::format("虚表扫描: vtable={} 范围 v[0x{:02X}..0x{:02X}]",
                           static_cast<const void*>(vt), kVtScanBegin, kVtScanEnd));

    // 0) 已知结果直连: PostRenderScanner 实测 v[0x6D] 就是 PostRender。
    //    槽位函数序言必须匹配已知特征码 (版本变了就自动走回退扫描)。
    if (kPostRenderSlot < kVtScanEnd) {
        void* fn = const_cast<void*>(vt[kPostRenderSlot]);
        if (IsExecutableAddress(fn) && PostRenderPrologueMatches(fn) && CanBePatched(fn)) {
            ProbeStats& st = g_probes[0];
            st = ProbeStats{};
            st.slot = kPostRenderSlot;
            st.address = fn;

            void* orig = fn;
            if (core::hooks::CreateHook(fn, &DirectPostRenderDetour, &orig)) {
                st.original = orig;
                g_probeCount = 1;
                g_probeInstalled = true;
                log::Stage(std::format("PostRender 直连: v[0x{:02X}] = {} (序言特征码确认)",
                                       kPostRenderSlot, fn));
                log::Log(Level::Info, "frame", "PostRender 直接挂钩成功 v[0x{:02X}]", kPostRenderSlot);
                return;
            }
            log::Warn("[frame] PostRender 直连挂钩失败, 回退虚表启发式扫描");
        } else {
            log::Warn("[frame] v[0x6D] 序言与已知特征码不符 (版本可能已更新), 回退虚表启发式扫描");
        }
    }

    // 1) dump 槽位: 只记录"确实指向可执行内存"的槽
    std::string dump;
    size_t execSlots = 0;
    for (size_t i = kVtScanBegin; i < kVtScanEnd; ++i) {
        const void* fn = vt[i];
        const bool exec = IsExecutableAddress(fn);
        if (exec) ++execSlots;
        dump += std::format("  v[0x{:02X}] = {} {}\r\n", i, fn,
                            exec ? "[可执行]" : "[非代码页, 跳过]");
    }
    log::Stage(std::string("虚表槽位:\r\n") + dump);
    log::Stage(std::format("虚表扫描结果: {} 个槽中 {} 个落在可执行页", 
                           kVtScanEnd - kVtScanBegin, execSlots));

    // 2) 逐槽挂探针 (只挂真正可执行 + 序言正常 + 可改写的)
    const auto& detours = Detours();
    size_t installed = 0, skippedNotExec = 0, skippedPrologue = 0, skippedRO = 0, skippedDup = 0;
    g_probeCount = 0;
    for (size_t i = kVtScanBegin; i < kVtScanEnd && g_probeCount < kProbeCap; ++i) {
        void* fn = const_cast<void*>(vt[i]);

        if (!IsExecutableAddress(fn)) { ++skippedNotExec; continue; }
        if (!LooksLikeFunction(fn))   { ++skippedPrologue; continue; }

        // 同一地址只挂一次 (上一版日志里 v[0x3E]==v[0x3F] 就是这个坑)
        bool dup = false;
        for (size_t j = 0; j < g_probeCount; ++j) {
            if (g_probes[j].address == fn) { dup = true; break; }
        }
        if (dup) { ++skippedDup; continue; }

        // 交给 MinHook 之前先确认目标页可写, 否则跳过 —— 绝不让它去撞只读页
        if (!CanBePatched(fn)) { ++skippedRO; continue; }

        ProbeStats& st = g_probes[g_probeCount];
        st.slot = i;
        st.address = fn;

        void* orig = fn; // MinHook 需要可写的 void*& 作为 original 输出
        if (core::hooks::CreateHook(fn, detours[g_probeCount], &orig)) {
            st.original = orig; // trampoline, detour 里靠它透传
            ++installed;
            ++g_probeCount;
        }
    }

    g_probeInstalled = (installed > 0);
    log::Stage(std::format("虚表探针: 安装 {} 个 (跳过: 非代码页={} 序言异常={} 只读={} 重复={})",
                           installed, skippedNotExec, skippedPrologue, skippedRO, skippedDup));
    log::Log(installed ? Level::Info : Level::Warn, "frame",
             "UGameViewportClient 虚表探针 {}", installed ? "已安装" : "安装失败");
}

// ---------------------------------------------------------------------------
// 公共接口 (FeatureHost.hpp)
// ---------------------------------------------------------------------------

void InstallEventDispatch() {
    core::AddProcessEventListener(&ProcessEventListener);
    InstallNativeCraftHooks();
    InstallViewportHook();
    log::Info("[dispatch] 功能分发已就绪");
}

void TickDispatchFlags() {
    ApplyBuildConsumeFlags();
    if (cheatState.palCapture100) {
        ApplyCaptureAlwaysFlag();
        TryInvokeCheatManagerCaptureSuccessAlways();
    }
}

} // namespace pal::features
