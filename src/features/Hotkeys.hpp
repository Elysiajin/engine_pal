#pragma once
#include <pch.h>

// 热键逻辑层: 轮询 GetAsyncKeyState 并执行动作。
// 面板 (原 DrawHotkeys) 已移交 ui 层, 由 Shadow-Gui 的 HotKey 行控件重写。
void TickHotkeys();
void TickHotkeysOneShot();
