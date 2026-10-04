#pragma once
// core/Input.hpp — WndProc 钩子与菜单开关。
// 职责: 菜单显隐状态、按键/鼠标消息的拦截与放行、菜单打开时阻止镜头穿透。

#include <pch.h>

namespace pal::core::input {

// 安装 WndProc 钩子 (枚举本进程可见主窗口)。幂等。
void Install();

bool IsMenuOpen();
void SetMenuOpen(bool open);
void ToggleMenu();

// 菜单切换键 (VK_*, 默认 Insert)。可被配置覆写。
void SetMenuToggleKey(int vk);
int  GetMenuToggleKey();

// 滚轮增量: 自上次调用以来累计的滚动格数 (正=上滚), 读取后清零。
// 供上帝之手等"菜单关闭时也要响应滚轮"的功能使用。
float ConsumeWheelDelta();

} // namespace pal::core::input
