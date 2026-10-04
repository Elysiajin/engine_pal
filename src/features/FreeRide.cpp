#include <pch.h>
#include "core/Log.hpp"
#include "FreeRide.hpp"
#include "core/Gfx.hpp"
#include "core/Input.hpp"
#include "engine/GameHelper.hpp"

using namespace SDK;
using namespace Helper;

namespace gfx = pal::gfx;   // 覆盖层绘制门面 (本文件在全局命名空间, 需要别名)

// ============================================================================
// 目标识别：屏幕空间最近目标
// 游戏内（菜单关闭）用屏幕中心（准星），菜单打开时用鼠标位置。
// ============================================================================
static bool GetCursorScreenPos(gfx::Vec2& out)
{
    APalPlayerController* pc = GetPalPlayerController();
    if (!pc || !Helper::IsProbablyValidPtr(pc))
        return false;

    float x = 0.0f, y = 0.0f;
    if (!Helper::Try([&] { pc->GetMousePosition(&x, &y); }))
        return false;

    out = gfx::Vec2{ x, y };
    return true;
}

static SDK::APalCharacter* FindRideTargetAtScreenPoint(gfx::Vec2 screenPoint, float thresholdPx = 80.0f)
{
    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player) return nullptr;

    APlayerController* controller = nullptr;
    if (!Helper::Try([&] { controller = reinterpret_cast<APlayerController*>(player->Controller); }))
        return nullptr;
    if (!controller) return nullptr;

    SDK::TArray<SDK::APalCharacter*> allPals;
    if (!Helper::GetTAllPals(&allPals))
        Helper::GetAllPalsFromWorld(&allPals);
    if (allPals.Num() == 0) return nullptr;

    float closestDist = thresholdPx;
    SDK::APalCharacter* best = nullptr;

    for (int32 i = 0; i < allPals.Num(); ++i)
    {
        if (!allPals.IsValidIndex(i)) continue;
        SDK::APalCharacter* pal = allPals[i];
        if (!pal || !Helper::IsProbablyValidPtr(pal)) continue;
        if (pal == player) continue;

        FVector palLoc;
        if (!Helper::Try([&] { palLoc = pal->K2_GetActorLocation(); })) continue;

        FVector2D screenPos;
        bool onScreen = false;
        if (!Helper::Try([&] { onScreen = controller->ProjectWorldLocationToScreen(palLoc, &screenPos, false); }))
            continue;
        if (!onScreen) continue;
        if (screenPos.X < 0 || screenPos.Y < 0) continue;

        float dx = screenPos.X - screenPoint.x;
        float dy = screenPos.Y - screenPoint.y;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist < closestDist)
        {
            closestDist = dist;
            best = pal;
        }
    }

    return best;
}

static SDK::APalCharacter* FindRideTargetAtCrosshair()
{
    const gfx::Vec2 screen = gfx::ScreenSize();
    return FindRideTargetAtScreenPoint(gfx::Vec2{ screen.x * 0.5f, screen.y * 0.5f }, 80.0f);
}

static SDK::APalCharacter* FindRideTargetAtMouse()
{
    gfx::Vec2 cursor{};
    if (!GetCursorScreenPos(cursor))
        return nullptr;
    return FindRideTargetAtScreenPoint(cursor, 60.0f);
}

// ============================================================================
// 获取目标显示名
// ============================================================================
static std::string GetRideTargetName(SDK::APalCharacter* pal)
{
    if (!pal) return "无";
    std::string raw;
    if (!Helper::Try([&] { raw = pal->GetName(); })) return "未知";
    size_t start = 0;
    if (raw.find("BP_") == 0) start += 3;
    if (raw.find("NPC_", start) == start) start += 4;
    size_t end = raw.find("_C", start);
    std::string clean = (end != std::string::npos) ? raw.substr(start, end - start) : raw.substr(start);
    while (!clean.empty() && isdigit(clean.back())) clean.pop_back();
    while (!clean.empty() && clean.back() == '_') clean.pop_back();
    return clean;
}

// ============================================================================
// 强制乘骑（绕过条件限制）
// ============================================================================
static void TryForceRide(SDK::APalCharacter* target)
{
    if (!target || !Helper::IsProbablyValidPtr(target))
        return;

    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !Helper::IsProbablyValidPtr(player))
        return;

    // ============================================================
    // 方案 1（首选）：UPalUtility::RideTo(RiderActor, RidePal, bSkipAnim)
    // 这是游戏提供的静态乘骑入口（蓝图中释放帕鲁交互乘骑时调用），
    // 会正确处理动画、网络同步与骑乘状态机。
    // ============================================================
    bool rideOk = false;
    Helper::Try([&]
    {
        rideOk = UPalUtility::RideTo(player, target, freeRide.skipAnimation);
    });

    if (rideOk)
    {
        freeRide.currentTarget = target;
        freeRide.targetName = GetRideTargetName(target);
        freeRide.isRiding = true;
        pal::log::Log(pal::log::Level::Debug, "ride", "[FreeRide] RideTo success: {}\n", freeRide.targetName.c_str());
        return;
    }

    // ============================================================
    // 方案 2（兜底）：通过 UPalRiderComponent 强制乘骑
    // 获取玩家身上的 RiderComponent，然后调用 Ride() 并同步到服务器。
    // ============================================================
    Helper::Try([&]
    {
        // 在玩家身上找 UPalRiderComponent
        UPalRiderComponent* rider = nullptr;
        // 通过 K2_GetComponentsByClass 获取
        TArray<UActorComponent*> comps = player->K2_GetComponentsByClass(UPalRiderComponent::StaticClass());
        if (comps.Num() == 0)
            return;
        rider = static_cast<UPalRiderComponent*>(comps[0]);
        if (!rider || !Helper::IsProbablyValidPtr(rider))
            return;

        // 在目标上找 UPalRideMarkerComponent
        UPalRideMarkerComponent* marker = nullptr;
        TArray<UActorComponent*> markerComps = target->K2_GetComponentsByClass(UPalRideMarkerComponent::StaticClass());
        if (markerComps.Num() == 0)
            return;
        marker = static_cast<UPalRideMarkerComponent*>(markerComps[0]);
        if (!marker || !Helper::IsProbablyValidPtr(marker))
            return;

        // 强制乘骑（跳过动画）
        bool ok = rider->Ride(marker, freeRide.skipAnimation);
        if (ok)
        {
            freeRide.currentTarget = target;
            freeRide.targetName = GetRideTargetName(target);
            freeRide.isRiding = true;
            pal::log::Log(pal::log::Level::Debug, "ride", "[FreeRide] RiderComponent::Ride success: {}\n", freeRide.targetName.c_str());
        }
    });
}

// ============================================================================
// 强制下马
// ============================================================================
static void TryForceGetOff()
{
    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !Helper::IsProbablyValidPtr(player))
        return;

    // 寻找玩家身上的 RIDER 组件
    Helper::Try([&]
    {
        TArray<UActorComponent*> comps = player->K2_GetComponentsByClass(UPalRiderComponent::StaticClass());
        if (comps.Num() == 0)
            return;
        UPalRiderComponent* rider = static_cast<UPalRiderComponent*>(comps[0]);
        if (!rider || !Helper::IsProbablyValidPtr(rider))
            return;

        // GetOff(bIsSkipAnimation, bNoAnimCancel)
        rider->GetOff(true, false);
    });

    freeRide.isRiding = false;
    freeRide.currentTarget = nullptr;
    freeRide.targetName = "";
}

// ============================================================================
// 每帧更新
// ============================================================================
void TickFreeRide()
{
    if (!freeRide.enabled)
    {
        // 已关闭：不干预
        return;
    }

    // 检测乘骑热键（按下沿触发）
    static bool wasRideKeyDown = false;
    bool isRideKeyDown = (GetAsyncKeyState(freeRide.rideHotkey) & 0x8000) != 0;

    if (isRideKeyDown && !wasRideKeyDown)
    {
        // 如果正在乘骑，先尝试下马
        if (freeRide.isRiding)
        {
            TryForceGetOff();
            wasRideKeyDown = isRideKeyDown;
            return;
        }

        // 否则寻找目标并乘骑（游戏内用准星中心，菜单内用鼠标）
        SDK::APalCharacter* target = nullptr;
        if (pal::core::input::IsMenuOpen())
            target = FindRideTargetAtMouse();
        else
            target = FindRideTargetAtCrosshair();
        if (target)
        {
            TryForceRide(target);
        }
        else
        {
            // 空白处点击：尝试下马
            if (freeRide.isRiding)
                TryForceGetOff();
        }
    }
    wasRideKeyDown = isRideKeyDown;
}

// ============================================================================
// UI 层入口 (原 DrawFreeRidePanel 的操作部分; 面板由 ui/tabs 用 Shadow-Gui 重写)
// ============================================================================
void RideTargetAtMouse()
{
    SDK::APalCharacter* target = FindRideTargetAtMouse();
    if (target)
        TryForceRide(target);
}

void ForceGetOff()
{
    TryForceGetOff();
}