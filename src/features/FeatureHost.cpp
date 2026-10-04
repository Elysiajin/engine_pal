// features/FeatureHost.cpp — 每帧功能编排
//
// 原项目把每帧工作拆在 Menu::Draw (渲染线程) 与 Menu::Loops (后台线程) 两处,
// 造成 ImGui 状态跨线程访问等隐患。重构版统一收敛到游戏线程的帧事件内:
//   TickGameFrame — 纯逻辑 tick (SDK 状态维护)
//   DrawOverlays  — 世界覆盖层绘制 (Shadow 画布)

#include <pch.h>
#include "FeatureHost.hpp"

#include "Aimbot.hpp"
#include "CheatState.hpp"
#include "Esp.hpp"
#include "FreeRide.hpp"
#include "GodHand.hpp"
#include "Hotkeys.hpp"
#include "MinimapDraw.hpp"
#include "PathVisualization.hpp"
#include "Pathfinding.hpp"
#include "core/Gfx.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"
#include "engine/GameHelper.hpp"

#include <ShadowGui/Shadow.h>

namespace pal::features {

void Install() {
    InstallEventDispatch();
}

void InitializeFrame() {
    // 名称映射器已在 FrameDriver 初始化时加载; 目前无额外首帧工作
}

void TickGameFrame(float dt) {
    // 官方调试标志同步 (捕获必中 / 建造不消耗材料)
    TickDispatchFlags();

    // 原热键系统 (GetAsyncKeyState 轮询)
    TickHotkeys();
    TickHotkeysOneShot();

    // 持续生效类
    if (cheatState.isInvicity) SetPlayerInvicity();

    // 自瞄 (按住热键时)
    if (cheatState.aimbotEnabled && (GetAsyncKeyState(cheatState.aimbotHotkey) & 0x8000))
        RunPalAimbot();

    // 上帝之手 / 自由乘骑 (原 Menu::Draw 中调用, 依赖输入状态)
    Helper::Try([&] {
        TickGodHand();
        TickFreeRide();
    });

    // 寻路系统
    if (cheatState.pathfindingEnabled) {
        Helper::Try([&] {
            PathManager& pathMgr = PathManager::Get();
            pathMgr.SetRecalcThresholdMeters(cheatState.pathRecalcThreshold);
            pathMgr.SetThrottleMs(cheatState.pathThrottleMs);
            pathMgr.SetMaxActivePaths(cheatState.pathMaxActive);
            pathMgr.Tick(dt);
        });
    }

    // 原菜单循环 tick (建造解锁/无限跳跃/飞行/状态免疫/隐身/弹药/食物/时间静止)
    TickBuildUnlock();
    TickInfiniteJump();
    TickFly(dt > 0.f ? dt : 0.016f);
    TickStatusImmune();
    TickInvisible();
    TickInfiniteAmmo();
    TickInfiniteMagazine();
    TickFoodNoSpoil();
    TickTimeFreeze();
}

void DrawOverlays() {
    // ESP
    if (cheatState.espEnabled)
        Helper::Try([&] {
            DrawPalESP();
            DrawRelicESP();
        });

    // 小地图雷达
    if (cheatState.minimapEnabled)
        Helper::Try([&] { DrawMinimap(); });

    // 上帝之手可视化
    Helper::Try([&] { DrawGodHandOverlay(); });

    // 寻路可视化
    if (cheatState.pathfindingEnabled && cheatState.pathVisualizationEnabled) {
        Helper::Try([&] {
            if (SDK::APlayerController* pc = Helper::GetPalPlayerController();
                pc && Helper::IsProbablyValidPtr(pc))
                DrawAllActivePaths(PathManager::Get(), PathVisualizeSettings::FromCheatState(), pc);
        });
    }

    // 自瞄 FOV 圈
    if (cheatState.aimbotEnabled && cheatState.aimbotDrawFOV) {
        const Shadow::Vec2 center = {Shadow::GetIO().DisplaySize.x * 0.5f,
                                     Shadow::GetIO().DisplaySize.y * 0.5f};
        const Shadow::Color color{0.707f, 0.707f, 0.707f, 0.707f};
        auto* dl = Shadow::GetBackgroundDrawList();
        // 圆环: 分段折线
        constexpr int kSegments = 64;
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = static_cast<float>(i) / kSegments * 6.2831853f;
            const float a1 = static_cast<float>(i + 1) / kSegments * 6.2831853f;
            dl->AddLine({center.x + std::cos(a0) * cheatState.aimbotFov,
                         center.y + std::sin(a0) * cheatState.aimbotFov},
                        {center.x + std::cos(a1) * cheatState.aimbotFov,
                         center.y + std::sin(a1) * cheatState.aimbotFov},
                        color, 2.0f);
        }
    }

    // 时间静止视觉反馈 (原 Menu::HUD): 仅在菜单关闭时显示
    if (cheatState.timeFreeze && !core::input::IsMenuOpen()) {
        const gfx::Vec2 screen = gfx::ScreenSize();
        gfx::RectFilled({0.f, 0.f}, screen, gfx::Color{0.f, 0.39f, 1.f, 0.07f});

        constexpr std::string_view kLabel = "时间静止：世界暂停，仅玩家可动";
        const gfx::Vec2 textSize = gfx::TextSize(kLabel);
        gfx::Text({screen.x * 0.5f - textSize.x * 0.5f, 40.f},
                  gfx::Color{0.47f, 0.78f, 1.f, 1.f}, kLabel);
    }
}

} // namespace pal::features
