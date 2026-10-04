#pragma once
// ui/Menu.hpp — 主菜单外壳 (侧边栏 + 头部 + 内容区)

#include <pch.h>

namespace pal::ui {

// 一次性初始化: 主题 + 字体 (需在 Shadow 帧上下文可用的游戏线程调用)
void Initialize();

// 每帧绘制主菜单 (FrameDriver 在菜单 alpha > 0 时调用)
void DrawMenu();

} // namespace pal::ui
