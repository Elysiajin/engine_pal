#pragma once
#include <pch.h>
#include <string>

// ============================================================================
// 自由乘骑（Free Ride）——光标指向目标快速乘骑系统
//
// 绕过游戏本地的乘骑条件限制（鞍具要求、体型限制、驯服状态等），
// 通过 UPalUtility::RideTo / UPalRiderComponent::Ride 强制进入乘骑状态。
//
// 功能：
//   - 光标目标识别（屏幕空间最近目标）
//   - 乘骑状态强制切换（绕过条件限制）
//   - 骑乘控制权限获取（通过服务器 RPC AttachRiderNoAnimation_ToServer）
//   - 单人/联机环境均可用（尽量走服务器 RPC 保证同步）
// ============================================================================

struct FreeRideState
{
    bool    enabled = false;            // 总开关
    int     rideHotkey = VK_MBUTTON;    // 默认鼠标中键乘骑
    bool    skipAnimation = true;       // 跳过动画（快速乘骑）

    // 当前目标
    SDK::APalCharacter* currentTarget = nullptr;
    std::string         targetName;
    bool                isRiding = false;
};

inline FreeRideState freeRide;

// ============================================================================
// 公开接口
// ============================================================================

// 每帧更新 (必须在游戏线程帧内调用)
void TickFreeRide();

// ---- UI 层入口 (原 DrawFreeRidePanel 的操作部分, 面板由 ui/tabs 重写) ----
void RideTargetAtMouse();  // 立即乘骑光标下的目标
void ForceGetOff();        // 强制下马