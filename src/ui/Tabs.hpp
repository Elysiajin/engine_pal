#pragma once
// ui/Tabs.hpp — 菜单标签页入口 (由 Menu 外壳调度)
//
// 约定: 每个 Tab 函数在内容区被调用时, 光标已定位到内容区起点,
// 使用 pal::ui 的行式控件 (Widgets.hpp) 绘制; 内容超高由外壳统一滚动。

#include <pch.h>

namespace pal::ui {

void TabAimbotESP();      // 自瞄与透视
void TabFeatures();       // 在线功能
void TabSinglePlayer();   // 单人功能
void TabPalEditor();      // 帕鲁编辑器
void TabPalSpawner();     // 帕鲁生成器
void TabItemSpawner();    // 物品生成器
void TabTeleporter();     // 传送器
void TabPlayerObjects();  // 人物/周围对象
void TabSettings();       // 热键与设置
void TabChangeLog();      // 更新日志

} // namespace pal::ui
