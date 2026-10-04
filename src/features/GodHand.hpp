#pragma once
#include <pch.h>
#include <vector>
#include <string>

// ============================================================================
// 上帝之手（God's Hand）——球面运动控制系统
//
// 交互模式（用户需求）：
//   1. 按住热键（默认鼠标中键）→ 准星指向的对象被"抓取"
//   2. 对象被吸附到以玩家为中心、半径 R 的球面上：
//        对象位置 = 玩家位置 + 相机前方向 × R
//   3. 转动视角 → 对象在球面上跟随运动（每帧更新位置）
//   4. 鼠标滚轮 → 调整半径 R（滚轮向上=拉近，向下=推远）
//   5. 松开热键 → 对象留在当前位置，玩家恢复正常操作
//   6. 不影响角色正常移动
//
// 面板辅助功能（菜单打开时可用）：
//   - 手动目标选择列表
//   - X/Y/Z 精确偏移输入（用于微调）
//   - 远程移动按钮
// ============================================================================

struct GodHandState
{
    bool    enabled = false;            // 总开关
    int     grabHotkey = VK_MBUTTON;   // 默认鼠标中键：按住=抓取控制
    float   sphereRadius = 2000.0f;    // 球面半径（cm），默认 2000=20m
    float   wheelSensitivity = 100.0f; // 滚轮每格半径变化（cm）
    float   minRadius = 100.0f;        // 最小半径
    float   maxRadius = 50000.0f;      // 最大半径

    // 抓取状态
    SDK::APalCharacter* grabbedTarget = nullptr;
    std::string         targetName;
    bool                isGrabbing = false;   // 是否正在控制

    // 面板辅助：精确偏移
    float   offsetX = 0.0f;
    float   offsetY = 0.0f;
    float   offsetZ = 0.0f;
};

inline GodHandState godHand;

// ============================================================================
// 公开接口
// ============================================================================

// 绘制锁定目标可视化高亮 + 准星光标（由功能层每帧覆盖层绘制调用）
void DrawGodHandOverlay();

// 每帧更新（必须在渲染线程/游戏线程帧内调用）
void TickGodHand();

// ---- UI 层入口 (原 DrawGodHandPanel 的操作部分, 面板由 ui/tabs 重写) ----
void GrabAtMouse();        // 抓取光标下的目标
void ReleaseGrabbed();     // 释放当前抓取目标
void ApplyGrabbedOffset(); // 应用 offsetX/Y/Z 精确偏移
