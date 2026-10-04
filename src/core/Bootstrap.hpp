#pragma once
// core/Bootstrap.hpp — DLL 入口引导流程。

#include <pch.h>

namespace pal::core {

// 主线程入口: 初始化日志 → 等游戏就绪 → 安装钩子 → 常驻。
// 原项目在此之后还会开后台功能线程; 重构版把所有功能 tick 收敛到游戏帧内
// (FrameDriver), 不再开任何游戏数据访问线程, 消除线程安全隐患。
void Bootstrap(HMODULE module);

} // namespace pal::core
