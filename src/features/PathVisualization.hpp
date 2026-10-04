// ============================================================
// PathVisualization.hpp — 路径在 3D 世界中的绘制
// 依赖：pal::gfx 绘制门面 (Shadow 后台画布), SDK (APlayerController)
//       PathManager, PathResult, CheatState
//
// 性能设计（v2）：
//   每帧仅调用 4 次 ProcessEvent（1 次 GetPlayerViewPoint +
//   3 次标定投影）构建相机投影器，随后所有路径航点用纯数学
//   投影（世界 → 相机基点积 → 透视除法 → 屏幕像素），单点
//   纳秒级开销，与路径数量、航点数量完全无关。
//   因此路线每帧精确跟随相机移动/旋转，彻底消除旧版
//   “大阈值缓存”导致的路线钉屏、跳变（卡顿）问题。
// ============================================================
#pragma once
#include <pch.h>
#include "Pathfinding.hpp"

// 路径可视化配置
struct PathVisualizeSettings
{
    bool    enabled = true;              // 总开关
    bool    showPathLine = true;         // 绘制路径线
    bool    showWaypoints = true;        // 绘制路径点标记
    bool    showStartEnd = true;         // 起点/终点标记
    bool    showDistanceLabel = true;    // 显示路径距离文字
    bool    showDirectionArrows = true;  // 路径方向箭头
    bool    fadeTrail = false;           // 路径尾部渐隐效果
    float   lineColor[4] = { 0.0f, 0.8f, 1.0f, 1.0f };   // 主线颜色 RGBA 0-1（默认亮青蓝）
    float   startColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };  // 起点颜色（绿色）
    float   endColor[4] = { 1.0f, 0.2f, 0.2f, 1.0f };    // 终点颜色（红色）
    float   partialColor[4] = { 1.0f, 0.8f, 0.0f, 1.0f };// 部分路径（不可达段）颜色（橙色/黄色）
    float   lineThickness = 3.0f;        // 路径线宽（像素）
    float   waypointSize = 4.0f;         // 路径点半径（像素）
    float   nodeSize = 6.0f;             // 起点/终点标记大小（像素）
    int     maxDrawDistance = 2000;      // 最大绘制距离（米），超出不画
    float   zOffset = 30.0f;             // 路径绘制时抬高 Z 偏移（厘米，避免与地面 z-fighting）

    // 从 cheatState（全局）读取本配置
    static PathVisualizeSettings FromCheatState();
};

// ---- 绘制函数 ----

// 绘制所有活跃路径（每帧调用）
// 内部每帧构建一次相机投影器（4 次 ProcessEvent），
// 之后所有航点均为本地数学投影，无缓存、无阈值跳变。
// 绘制通过 pal::gfx 门面 (Shadow 后台画布) 完成，无需外部传入画布。
void DrawAllActivePaths(PathManager& mgr, const PathVisualizeSettings& settings,
                        SDK::APlayerController* pc);
