#pragma once
// features/FeatureHost.hpp — 功能层门面 (FrameDriver 唯一依赖的功能接口)

#include <pch.h>

namespace pal::features {

// 安装: 注册 ProcessEvent 监听者 + native 钩子 (游戏就绪后由 Bootstrap 调用)
void Install();

// 首帧一次性初始化 (FrameDriver::EnsureInitialized 调用)
void InitializeFrame();

// 每帧逻辑 tick (游戏线程)
void TickGameFrame(float dt);

// 每帧世界覆盖层绘制 (Shadow::NewFrame 之后 / Render 之前)
void DrawOverlays();

// 内部: 分发安装与调试标志同步 (FeatureDispatch.cpp 实现)
void InstallEventDispatch();
void TickDispatchFlags();

} // namespace pal::features
