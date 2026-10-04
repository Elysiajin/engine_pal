#include <pch.h>
#include "GodHand.hpp"
#include "core/Gfx.hpp"
#include "core/Input.hpp"
#include "engine/GameHelper.hpp"

using namespace SDK;
using namespace Helper;

namespace gfx = pal::gfx;   // 覆盖层绘制门面 (本文件在全局命名空间, 需要别名)

// ============================================================================
// 目标识别：屏幕空间查找最近目标
// ============================================================================

// 当前光标屏幕位置 (通过 SDK 读取, 功能层不接触 UI 输入状态)
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

static SDK::APalCharacter* FindTargetAtScreenPoint(gfx::Vec2 screenPoint, float thresholdPx)
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

        // 跳过死亡目标
        bool isAlive = false;
        if (Helper::SafeCallRet(isAlive, [&] { return IsAlive(pal); }) && !isAlive)
            continue;

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

static SDK::APalCharacter* FindTargetAtCrosshair(float thresholdPx = 120.0f)
{
    const gfx::Vec2 screen = gfx::ScreenSize();
    return FindTargetAtScreenPoint(gfx::Vec2{ screen.x * 0.5f, screen.y * 0.5f }, thresholdPx);
}

static SDK::APalCharacter* FindTargetAtMouse(float thresholdPx = 60.0f)
{
    gfx::Vec2 cursor{};
    if (!GetCursorScreenPos(cursor))
        return nullptr;
    return FindTargetAtScreenPoint(cursor, thresholdPx);
}

// ============================================================================
// 获取目标显示名
// ============================================================================
static std::string GetTargetDisplayName(SDK::APalCharacter* pal)
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
// 球面运动核心：将目标放置在玩家前方球面上
// 目标位置 = 玩家位置 + 相机前方向 × 半径
// 注意：不修改目标任何状态（CustomTimeDilation / MovementMode / Tick 等），
// 只每帧设置位置。崩溃时（Try 返回 false）自动释放目标。
// ============================================================================
static void UpdateTargetOnSphere()
{
    if (!godHand.isGrabbing || !godHand.grabbedTarget)
        return;
    if (!Helper::IsProbablyValidPtr(godHand.grabbedTarget))
        return;

    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !Helper::IsProbablyValidPtr(player))
        return;

    APalPlayerController* pc = GetPalPlayerController();
    if (!pc) return;

    // Try 返回 false = SEH 捕获到崩溃 = 目标已 GC 销毁 → 立即释放
    bool ok = Helper::Try([&]
    {
        FVector cameraLoc;
        FRotator cameraRot;
        pc->GetPlayerViewPoint(&cameraLoc, &cameraRot);

        FVector forward = UKismetMathLibrary::GetForwardVector(cameraRot);
        if (forward.IsZero()) return;

        FVector playerLoc = player->K2_GetActorLocation();
        FVector targetPos = playerLoc + forward * godHand.sphereRadius;

        godHand.grabbedTarget->K2_SetActorLocation(targetPos, false, nullptr, true);
    });

    if (!ok)
    {
        godHand.grabbedTarget = nullptr;
        godHand.isGrabbing = false;
        godHand.targetName = "";
    }
}

// ============================================================================
// 滚轮调整半径
// ============================================================================
static void TickWheelRadius()
{
    if (!godHand.isGrabbing) return;

    const float wheel = pal::core::input::ConsumeWheelDelta();
    if (wheel == 0.0f) return;

    float delta = -wheel * godHand.wheelSensitivity;
    godHand.sphereRadius += delta;
    if (godHand.sphereRadius < godHand.minRadius) godHand.sphereRadius = godHand.minRadius;
    if (godHand.sphereRadius > godHand.maxRadius) godHand.sphereRadius = godHand.maxRadius;
}

// ============================================================================
// 绘制锁定目标可视化高亮 + 准星光标
// ============================================================================
void DrawGodHandOverlay()
{
    if (!godHand.enabled) return;

    // 准星指示器 (菜单关闭时)
    if (!pal::core::input::IsMenuOpen())
    {
        const gfx::Vec2 screen = gfx::ScreenSize();
        const gfx::Vec2 center{ screen.x * 0.5f, screen.y * 0.5f };
        constexpr float r = 6.0f;
        const gfx::Color crossCol{ 0.f, 0.86f, 1.f, 0.78f };

        gfx::Line({ center.x - r, center.y }, { center.x + r, center.y }, crossCol, 1.5f);
        gfx::Line({ center.x, center.y - r }, { center.x, center.y + r }, crossCol, 1.5f);

        // 描边圆: Shadow 只有实心圆, 用分段折线代替
        const gfx::Color ringCol{ 0.f, 0.86f, 1.f, 0.39f };
        constexpr int kSegments = 24;
        constexpr float kRingR = r + 3.0f;
        for (int i = 0; i < kSegments; ++i)
        {
            const float a0 = static_cast<float>(i) / kSegments * 6.2831853f;
            const float a1 = static_cast<float>(i + 1) / kSegments * 6.2831853f;
            gfx::Line({ center.x + std::cos(a0) * kRingR, center.y + std::sin(a0) * kRingR },
                      { center.x + std::cos(a1) * kRingR, center.y + std::sin(a1) * kRingR },
                      ringCol, 1.0f);
        }
    }

    // 抓取目标高亮框（Try 崩溃时自动释放）
    if (!godHand.isGrabbing || !godHand.grabbedTarget) return;
    if (!Helper::IsProbablyValidPtr(godHand.grabbedTarget)) return;

    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player) return;

    APlayerController* controller = nullptr;
    if (!Helper::Try([&] { controller = reinterpret_cast<APlayerController*>(player->Controller); }))
        return;
    if (!controller) return;

    // Helper::Try 的 bool 返回值表示 SEH 是否捕获了崩溃，不是 lambda 的返回值。
    // 真正投影失败不是崩溃（SEH 不触发），Try 返回 true。我们只关心"是否崩溃"。
    bool crashed = !Helper::Try([&]
    {
        FVector loc = godHand.grabbedTarget->K2_GetActorLocation();

        SDK::FVector2D footScr, headScr;
        FVector head = loc + FVector(0, 0, 190.0f);
        bool footOk = controller->ProjectWorldLocationToScreen(loc, &footScr, false);
        bool headOk = controller->ProjectWorldLocationToScreen(head, &headScr, false);
        if (!footOk || !headOk) return;

        float boxH = headScr.Y - footScr.Y;
        if (boxH < 20.0f) boxH = 100.0f;
        float boxW = boxH * 0.6f;
        if (boxW < 30.0f) boxW = 30.0f;

        const gfx::Vec2 boxMin{ footScr.X - boxW * 0.5f, headScr.Y };
        const gfx::Vec2 boxMax{ footScr.X + boxW * 0.5f, footScr.Y };

        // 呼吸效果 (原 ImGui::GetTime 改为系统计时)
        const float seconds = static_cast<float>(GetTickCount64()) / 1000.0f;
        const float pulse = 0.5f + 0.5f * sinf(seconds * 4.0f);
        const gfx::Color col{ 0.f, 0.86f, 1.f, (180.f + 60.f * pulse) / 255.f };

        gfx::RectMinMax(boxMin, boxMax, col, 2.0f);

        // 四角加粗
        constexpr float cnr = 12.0f;
        gfx::Line({ boxMin.x, boxMin.y + cnr }, { boxMin.x, boxMin.y }, col, 2.0f);
        gfx::Line({ boxMin.x, boxMin.y }, { boxMin.x + cnr, boxMin.y }, col, 2.0f);
        gfx::Line({ boxMax.x, boxMax.y - cnr }, { boxMax.x, boxMax.y }, col, 2.0f);
        gfx::Line({ boxMax.x, boxMax.y }, { boxMax.x - cnr, boxMax.y }, col, 2.0f);
        gfx::Line({ boxMax.x, boxMin.y + cnr }, { boxMax.x, boxMin.y }, col, 2.0f);
        gfx::Line({ boxMax.x, boxMin.y }, { boxMax.x - cnr, boxMin.y }, col, 2.0f);
        gfx::Line({ boxMin.x, boxMax.y - cnr }, { boxMin.x, boxMax.y }, col, 2.0f);
        gfx::Line({ boxMin.x, boxMax.y }, { boxMin.x + cnr, boxMax.y }, col, 2.0f);

        const std::string label = std::format("{} [{:.0f}m]",
                                              godHand.targetName, godHand.sphereRadius / 100.0f);
        const gfx::Vec2 labelSize = gfx::TextSize(label);
        const gfx::Vec2 labelPos{ boxMin.x + (boxW - labelSize.x) * 0.5f,
                                  boxMin.y - labelSize.y - 4.f };

        gfx::RectFilled({ labelPos.x - 4.f, labelPos.y - 2.f },
                        { labelSize.x + 8.f, labelSize.y + 4.f },
                        gfx::Color{ 0.f, 0.f, 0.f, 0.63f });
        gfx::Text(labelPos, gfx::Color{ 0.f, 0.86f, 1.f, 1.f }, label);
    });

    if (crashed)
    {
        godHand.grabbedTarget = nullptr;
        godHand.isGrabbing = false;
        godHand.targetName = "";
    }
}

// ============================================================================
// 每帧更新
// 注意：不修改目标任何状态（CustomTimeDilation / MovementMode 等），
// 只做位置传送。释放时仅清空指针，不访问目标成员。
// 崩溃自动由 Menu::Draw 外层的全局 Try 兜底。
// ============================================================================
void TickGodHand()
{
    if (!godHand.enabled)
    {
        if (godHand.isGrabbing)
        {
            godHand.grabbedTarget = nullptr;
            godHand.isGrabbing = false;
            godHand.targetName = "";
        }
        return;
    }

    bool hotkeyDown = (GetAsyncKeyState(godHand.grabHotkey) & 0x8000) != 0;

    // 首次按下：抓取
    if (hotkeyDown && !godHand.isGrabbing)
    {
        SDK::APalCharacter* target = nullptr;
        if (pal::core::input::IsMenuOpen())
            target = FindTargetAtMouse(60.0f);
        else
            target = FindTargetAtCrosshair(120.0f);

        if (target)
        {
            godHand.grabbedTarget = target;
            godHand.isGrabbing = true;
            godHand.targetName = GetTargetDisplayName(target);
        }
    }

    // 松开热键：释放（不访问目标任何成员，只清空指针）
    if (!hotkeyDown && godHand.isGrabbing)
    {
        godHand.grabbedTarget = nullptr;
        godHand.isGrabbing = false;
        godHand.targetName = "";
    }

    // 球面运动
    if (godHand.isGrabbing)
    {
        TickWheelRadius();
        UpdateTargetOnSphere();
    }
}

// ============================================================================
// 面板辅助：精确偏移
// ============================================================================
void ApplyGrabbedOffset()
{
    if (!godHand.isGrabbing || !godHand.grabbedTarget) return;
    if (!Helper::IsProbablyValidPtr(godHand.grabbedTarget)) return;

    float ox = godHand.offsetX, oy = godHand.offsetY, oz = godHand.offsetZ;
    godHand.offsetX = godHand.offsetY = godHand.offsetZ = 0.0f;

    Helper::Try([&]
    {
        FVector pos = godHand.grabbedTarget->K2_GetActorLocation();
        pos.X += ox; pos.Y += oy; pos.Z += oz;
        godHand.grabbedTarget->K2_TeleportTo(pos, godHand.grabbedTarget->K2_GetActorRotation());
    });
}

// ============================================================================
// UI 层入口 (原 DrawGodHandPanel 的操作部分; 面板本身由 ui/tabs 用 Shadow-Gui 重写)
// ============================================================================
void GrabAtMouse()
{
    SDK::APalCharacter* target = FindTargetAtMouse(60.0f);
    if (!target) return;

    godHand.grabbedTarget = target;
    godHand.isGrabbing = true;
    godHand.targetName = GetTargetDisplayName(target);
}

void ReleaseGrabbed()
{
    godHand.grabbedTarget = nullptr;
    godHand.isGrabbing = false;
    godHand.targetName = "";
}