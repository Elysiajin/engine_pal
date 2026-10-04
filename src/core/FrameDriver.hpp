#pragma once
// core/FrameDriver.hpp — 每帧驱动器。
// 锚点: UGameViewportClient::Draw/PostRender 虚表钩子 (native, 见 FeatureDispatch)。
// 该函数每帧收到本帧真实的 UCanvas*, 与 EngineDraw 参考工程(hook vtable[0x63])一致。
// 帧结构对齐 Shadow-Gui 官方示例的 PostRender 流程:
//   NewFrame → UpdateAllHotkeyStates → 菜单/覆盖层绘制 → Render

#include <pch.h>

namespace pal::core {

class FrameDriver {
public:
    // 主入口: 由视口钩子传入本帧画布 (游戏渲染线程)。
    static void OnFrameWithCanvas(SDK::UCanvas* canvas);

    // 兼容入口: 从 AHUD 对象取 Canvas (仅当该字段有效时; UE 的 PostRender 期间
    // AHUD::Canvas 可能尚未赋值, 因此主路径用 OnFrameWithCanvas)。
    static void OnFrame(SDK::AHUD* hud);

    // 一次性初始化 (游戏就绪后, 首个帧事件内执行)。
    static void EnsureInitialized();

    static bool IsInitialized();

    // 菜单淡入淡出进度 (0..1), 由 ui 层驱动
    static void UpdateFade(float dt);
    static float MenuAlpha();

private:
    static bool s_initialized;
    static float s_alpha;
};

} // namespace pal::core
