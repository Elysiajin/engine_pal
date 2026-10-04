#pragma once
// ui/Theme.hpp — Shadow-Gui 主题与字体。
// 深色科技风 (沿用原项目 SciFi 配色), 系统字体加载 (微软雅黑, 支持中文)。

#include <pch.h>

namespace pal::ui {

// 一次性应用主题配色与尺寸参数 (需在 Shadow 帧内调用, 幂等)
void ApplyTheme();

// 加载字体 (系统 msyh.ttc, 失败则保持 Shadow 运行时默认字体)。幂等。
void EnsureFont();

} // namespace pal::ui
