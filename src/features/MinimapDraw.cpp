#include <pch.h>
#include "MinimapDraw.hpp"
#include "MinimapData.hpp"    // Minimap::Entity, EntityType, ScanResult, ScanEntities, GetEntityTypeColor
#include "CheatState.hpp"     // cheatState
#include "core/Gfx.hpp"
#include "engine/GameHelper.hpp"

#include <cmath>
#include <numbers>
#include <string>

// ============================================================
// MinimapDraw.cpp — 小地图雷达渲染模块
//
// 职责：
//   - 纯 pal::gfx 覆盖层绘制，不包含任何扫描/世界遍历逻辑。
//   - 数据来自 Minimap::ScanEntities()（由 MinimapData 提供）。
//   - 所有配置从 cheatState 读取。
//
// 坐标数学（设计文档 §3.1，评审修正后）：
//   Rotation-Up: yawRad = -cameraYaw * DEG_TO_RAD
//     rx = delta.X * sin(yawRad) + delta.Y * cos(yawRad)  // 右分量 → 屏幕右
//     ry = delta.X * cos(yawRad) - delta.Y * sin(yawRad)  // 前分量 → 屏幕上
//   North-Up: rx = delta.Y, ry = delta.X（等价于 yaw=0 的 Rotation-Up）
//
// 崩溃安全：
//   - 纯 pal::gfx 绘制 API（CircleFilled/Line/Text 等）不需要
//     Helper::Try 保护。
//   - 唯一需要安全保护的是调用 ScanEntities（它内部有自己的 Try 保护）。
// ============================================================

static constexpr float kPi = std::numbers::pi_v<float>;

// 常用颜色（由原 RGBA 常量换算为 0..1 浮点）
static constexpr pal::gfx::Color kIconOutline        = {0.f, 0.f, 0.f, 0.784f}; // (0,0,0,200)
static constexpr pal::gfx::Color kIconOutlineStrong  = {0.f, 0.f, 0.f, 0.863f}; // (0,0,0,220)
static constexpr pal::gfx::Color kEdgeArrow          = {1.f, 1.f, 1.f, 0.784f}; // (255,255,255,200)
static constexpr pal::gfx::Color kEdgeArrowOutline   = {0.f, 0.f, 0.f, 0.706f}; // (0,0,0,180)
static constexpr pal::gfx::Color kPlayerArrow        = {1.f, 1.f, 1.f, 1.000f}; // (255,255,255,255)
static constexpr pal::gfx::Color kPlayerArrowOutline = {0.f, 0.f, 0.f, 0.784f}; // (0,0,0,200)
static constexpr pal::gfx::Color kRadarBg            = {0.f, 0.f, 0.f, 0.627f}; // (0,0,0,160)
static constexpr pal::gfx::Color kRadarRing          = {1.f, 1.f, 1.f, 0.314f}; // (255,255,255,80)
static constexpr pal::gfx::Color kRadarCross         = {1.f, 1.f, 1.f, 0.098f}; // (255,255,255,25)
static constexpr pal::gfx::Color kDistRingOuter      = {1.f, 1.f, 1.f, 0.098f}; // (255,255,255,25)
static constexpr pal::gfx::Color kDistRingInner      = {1.f, 1.f, 1.f, 0.071f}; // (255,255,255,18)
static constexpr pal::gfx::Color kCompassText        = {1.f, 1.f, 1.f, 0.510f}; // (255,255,255,130)
static constexpr pal::gfx::Color kRangeText          = {1.f, 1.f, 1.f, 0.392f}; // (255,255,255,100)
static constexpr pal::gfx::Color kLabelBg            = {0.f, 0.f, 0.f, 0.549f}; // (0,0,0,140)
static constexpr pal::gfx::Color kLabelText          = {1.f, 1.f, 1.f, 0.863f}; // (255,255,255,220)

// ============================================================
// Shadow 无 AddCircle 描边圆 (只有 AddCircleFilled)：
// 用分段 Line 等价实现描边圆。
// ============================================================
static void DrawCircleOutline(pal::gfx::Vec2 center, float radius,
                              pal::gfx::Color color, float thickness, int segments)
{
    const float step = (2.0f * kPi) / static_cast<float>(segments);
    pal::gfx::Vec2 prev{center.x + radius, center.y};
    for (int i = 1; i <= segments; ++i) {
        const float a = static_cast<float>(i) * step;
        const pal::gfx::Vec2 cur{center.x + radius * cosf(a), center.y + radius * sinf(a)};
        pal::gfx::Line(prev, cur, color, thickness);
        prev = cur;
    }
}

// Shadow 无 AddEllipseFilled：用分段三角扇等价实现填充椭圆。
static void DrawEllipseFilled(pal::gfx::Vec2 center, float radiusX, float radiusY,
                              pal::gfx::Color color, int segments)
{
    const float step = (2.0f * kPi) / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        const float a0 = static_cast<float>(i) * step;
        const float a1 = static_cast<float>(i + 1) * step;
        const pal::gfx::Vec2 p0{center.x + radiusX * cosf(a0), center.y + radiusY * sinf(a0)};
        const pal::gfx::Vec2 p1{center.x + radiusX * cosf(a1), center.y + radiusY * sinf(a1)};
        pal::gfx::TriangleFilled(center, p0, p1, color);
    }
}

// Shadow 无 AddEllipse 描边：用分段 Line 等价实现描边椭圆。
static void DrawEllipseOutline(pal::gfx::Vec2 center, float radiusX, float radiusY,
                               pal::gfx::Color color, float thickness, int segments)
{
    const float step = (2.0f * kPi) / static_cast<float>(segments);
    pal::gfx::Vec2 prev{center.x + radiusX, center.y};
    for (int i = 1; i <= segments; ++i) {
        const float a = static_cast<float>(i) * step;
        const pal::gfx::Vec2 cur{center.x + radiusX * cosf(a), center.y + radiusY * sinf(a)};
        pal::gfx::Line(prev, cur, color, thickness);
        prev = cur;
    }
}

// ============================================================
// 世界坐标 → 雷达屏幕坐标 转换
// ============================================================
struct RadarScreenPos {
    pal::gfx::Vec2 pos;       // 屏幕像素坐标（已偏移到雷达圆心）
    bool           clamped;   // true = 被夹取到雷达圆周边缘
};

static RadarScreenPos WorldToRadarScreen(
    const SDK::FVector& targetPos,
    const SDK::FVector& playerPos,
    double cameraYaw,              // [视图] 摄像机视角 Yaw（double），替代原角色朝向
    bool   rotationUp,
    const pal::gfx::Vec2& center,
    float  radarRadius,
    float  worldRadius)
{
    RadarScreenPos result;
    result.clamped = false;

    // 1. XY 平面偏移（FVector::X/Y 是 double，转换为 float 计算）
    const float dx = static_cast<float>(targetPos.X - playerPos.X);
    const float dy = static_cast<float>(targetPos.Y - playerPos.Y);

    // 2. 方位旋转
    float rx, ry;
    if (rotationUp) {
        // Rotation-Up：评审修正公式
        const float yawRad = -static_cast<float>(cameraYaw) * (kPi / 180.0f);
        const float sinYaw = sinf(yawRad);
        const float cosYaw = cosf(yawRad);
        rx = dx * sinYaw + dy * cosYaw;   // 右分量 → 屏幕右
        ry = dx * cosYaw - dy * sinYaw;   // 前分量 → 屏幕上
    } else {
        // North-Up：等价于 yaw=0 时的 Rotation-Up
        rx = dy;  // Y 正 = 东 = 右
        ry = dx;  // X 正 = 北 = 上
    }

    // 3. 缩放
    const float scale = (worldRadius > 0.0f) ? (radarRadius / worldRadius) : 1.0f;
    float sx = rx * scale;
    float sy = ry * scale;

    // 4. 边缘夹取
    const float dist = sqrtf(sx * sx + sy * sy);
    if (dist > radarRadius && dist > 0.0f) {
        sx = sx / dist * radarRadius;
        sy = sy / dist * radarRadius;
        result.clamped = true;
    }

    // 5. 偏移到雷达圆心（屏幕 Y 向下为正，ry 取反）
    result.pos.x = center.x + sx;
    result.pos.y = center.y - sy;

    return result;
}

// ============================================================
// 实体图标绘制函数
// 所有绘制均为纯 pal::gfx 操作，不需 Helper::Try 保护。
//
// [统一基准] 所有图标使用统一的 ICON_SIZE（外接圆半径），
// 保证帕鲁圆点与矿石/蛋/宝箱等图标视觉大小一致，
// 不会出现"小红点不明显"的问题。
// ============================================================

// 统一图标外接圆半径（像素）。所有图标都以此半径为基准绘制，
// 保证图例大小一致、清晰可辨。
static constexpr float ICON_SIZE = 5.0f;

// 圆点（帕鲁/NPC 用）— 半径 = ICON_SIZE，与其他图标外接圆一致
static void DrawDotIcon(pal::gfx::Vec2 pos, pal::gfx::Color color, float radius = ICON_SIZE)
{
    pal::gfx::CircleFilled(pos, radius, color);
    DrawCircleOutline(pos, radius, kIconOutline, 1.5f, 16);
}

// 小三角（其他玩家用）— 外接圆半径 = ICON_SIZE
static void DrawTriangleIcon(pal::gfx::Vec2 pos, pal::gfx::Color color, float size = ICON_SIZE)
{
    const pal::gfx::Vec2 p1{pos.x,                  pos.y - size};          // 上顶点
    const pal::gfx::Vec2 p2{pos.x - size * 0.866f,  pos.y + size * 0.5f};  // 左下（等边三角形）
    const pal::gfx::Vec2 p3{pos.x + size * 0.866f,  pos.y + size * 0.5f};  // 右下
    pal::gfx::TriangleFilled(p1, p2, p3, color);
    pal::gfx::Overlay()->AddTriangle(p1, p2, p3, kIconOutline, 1.5f);
}

// 菱形（矿石用）— 外接圆半径 = ICON_SIZE
// Shadow 无 AddQuad(Filled)：菱形拆成两个填充三角 + 四段描边 Line 等价实现。
static void DrawDiamondIcon(pal::gfx::Vec2 pos, pal::gfx::Color color, float size = ICON_SIZE)
{
    const pal::gfx::Vec2 p1{pos.x,        pos.y - size};  // 上
    const pal::gfx::Vec2 p2{pos.x + size, pos.y};         // 右
    const pal::gfx::Vec2 p3{pos.x,        pos.y + size};  // 下
    const pal::gfx::Vec2 p4{pos.x - size, pos.y};         // 左
    pal::gfx::TriangleFilled(p1, p2, p3, color);
    pal::gfx::TriangleFilled(p1, p3, p4, color);
    pal::gfx::Line(p1, p2, kIconOutline, 1.5f);
    pal::gfx::Line(p2, p3, kIconOutline, 1.5f);
    pal::gfx::Line(p3, p4, kIconOutline, 1.5f);
    pal::gfx::Line(p4, p1, kIconOutline, 1.5f);
}

// 椭圆（蛋用）— 外接圆半径 = ICON_SIZE
static void DrawEggIcon(pal::gfx::Vec2 pos, pal::gfx::Color color, float size = ICON_SIZE)
{
    DrawEllipseFilled(pos, size, size * 0.75f, color, 16);
    DrawEllipseOutline(pos, size, size * 0.75f, kIconOutline, 1.5f, 16);
}

// 方形（宝箱用）— 外接圆半径 = ICON_SIZE（对角线 = 2*ICON_SIZE）
static void DrawSquareIcon(pal::gfx::Vec2 pos, pal::gfx::Color color, float size = ICON_SIZE * 0.75f)
{
    // 方形对角线 = 2*size*sqrt(2) ≈ 外接圆直径，使方形视觉大小与其他图标一致
    const pal::gfx::Vec2 boxMin{pos.x - size, pos.y - size};
    const pal::gfx::Vec2 boxMax{pos.x + size, pos.y + size};
    // Shadow 的 AddRectFilled 无圆角参数，原 1.0f 圆角舍弃
    pal::gfx::RectFilledMinMax(boxMin, boxMax, color);
    pal::gfx::RectMinMax(boxMin, boxMax, kIconOutline, 1.5f);
}

// 五角星（翠叶鼠雕像用）— 外接圆半径 = ICON_SIZE
static void DrawStarIcon(pal::gfx::Vec2 pos, pal::gfx::Color color,
                         float outerR = ICON_SIZE, float innerR = ICON_SIZE * 0.5f)
{
    constexpr int   kNumPoints = 5;
    constexpr int   kTotalVerts = kNumPoints * 2;
    constexpr float step = kPi / static_cast<float>(kNumPoints);
    constexpr float startAngle = -kPi / 2.0f;  // 顶点朝上

    pal::gfx::Vec2 verts[kTotalVerts];
    for (int i = 0; i < kTotalVerts; ++i) {
        const float r = (i % 2 == 0) ? outerR : innerR;
        const float angle = startAngle + static_cast<float>(i) * step;
        verts[i].x = pos.x + r * cosf(angle);
        verts[i].y = pos.y + r * sinf(angle);
    }

    for (int i = 0; i < kTotalVerts; ++i) {
        const int next = (i + 1) % kTotalVerts;
        pal::gfx::TriangleFilled(verts[i], verts[next], pos, color);
    }
    for (int i = 0; i < kTotalVerts; ++i) {
        const int next = (i + 1) % kTotalVerts;
        pal::gfx::Line(verts[i], verts[next], kIconOutline, 1.5f);
    }
}

// 圆圈+十字（传送点用）— 外接圆半径 = ICON_SIZE
static void DrawFastTravelIcon(pal::gfx::Vec2 pos, pal::gfx::Color color, float size = ICON_SIZE)
{
    pal::gfx::CircleFilled(pos, size, color);
    DrawCircleOutline(pos, size, kIconOutline, 1.5f, 20);
    const float cross = size * 0.7f;
    pal::gfx::Line(pal::gfx::Vec2{pos.x - cross, pos.y}, pal::gfx::Vec2{pos.x + cross, pos.y}, kIconOutlineStrong, 1.5f);
    pal::gfx::Line(pal::gfx::Vec2{pos.x, pos.y - cross}, pal::gfx::Vec2{pos.x, pos.y + cross}, kIconOutlineStrong, 1.5f);
}

// 通用实体图标分发
static void DrawEntityIcon(pal::gfx::Vec2 screenPos, Minimap::EntityType type, pal::gfx::Color color)
{
    switch (type) {
    case Minimap::EntityType::WildPal:
    case Minimap::EntityType::TamedPal:
    case Minimap::EntityType::NPC:
        DrawDotIcon(screenPos, color);
        break;
    case Minimap::EntityType::OtherPlayer:
        DrawTriangleIcon(screenPos, color);
        break;
    case Minimap::EntityType::Ore:
        DrawDiamondIcon(screenPos, color);
        break;
    case Minimap::EntityType::Egg:
        DrawEggIcon(screenPos, color);
        break;
    case Minimap::EntityType::TreasureBox:
        DrawSquareIcon(screenPos, color);
        break;
    case Minimap::EntityType::Relic:
        DrawStarIcon(screenPos, color);
        break;
    case Minimap::EntityType::FastTravel:
        DrawFastTravelIcon(screenPos, color);
        break;
    default:
        DrawDotIcon(screenPos, color);
        break;
    }
}

// ============================================================
// 边缘方向指示器
// 当实体被夹取到雷达圆周时，在圆周边缘上绘制一个小箭头，
// 指示实体的实际方向。
// ============================================================
static void DrawEdgeIndicator(pal::gfx::Vec2 center, pal::gfx::Vec2 edgePos, pal::gfx::Color color)
{
    pal::gfx::Vec2 dir{edgePos.x - center.x, edgePos.y - center.y};
    const float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.1f) return;

    dir.x /= len;
    dir.y /= len;

    const float arrowSize = 4.0f;
    const pal::gfx::Vec2 tip{edgePos.x - dir.x * arrowSize, edgePos.y - dir.y * arrowSize};
    const pal::gfx::Vec2 left(
        edgePos.x + dir.y * arrowSize * 0.5f - dir.x * arrowSize * 0.3f,
        edgePos.y - dir.x * arrowSize * 0.5f - dir.y * arrowSize * 0.3f);
    const pal::gfx::Vec2 right(
        edgePos.x - dir.y * arrowSize * 0.5f - dir.x * arrowSize * 0.3f,
        edgePos.y + dir.x * arrowSize * 0.5f - dir.y * arrowSize * 0.3f);

    pal::gfx::TriangleFilled(tip, left, right, kEdgeArrow);
    pal::gfx::Overlay()->AddTriangle(tip, left, right, kEdgeArrowOutline, 1.0f);
}

// ============================================================
// 玩家箭头绘制
// 设计文档 §3.3（评审修正后）：
//   Rotation-Up: 箭头始终指向上方（屏幕 Y 负方向）
//   North-Up:    箭头方向 = 摄像机 Yaw 旋转
// 注意：玩家箭头绘制在雷达圆心（玩家位置）。
// ============================================================
static void DrawPlayerArrow(pal::gfx::Vec2 center, bool rotationUp, double cameraYaw)
{
    if (rotationUp) {
        // 箭头始终向上（顶点指向屏幕上方）
        // 等腰三角形，顶点向上，相对圆心偏移
        const float tipY  = center.y - 14.0f;  // 顶点
        const float baseY = center.y + 8.0f;   // 底边
        const float halfW = 7.0f;              // 半宽
        const pal::gfx::Vec2 top{center.x,          tipY};
        const pal::gfx::Vec2 left{center.x - halfW, baseY};
        const pal::gfx::Vec2 right{center.x + halfW, baseY};
        pal::gfx::TriangleFilled(top, left, right, kPlayerArrow);
        pal::gfx::Overlay()->AddTriangle(top, left, right, kPlayerArrowOutline, 1.5f);
    } else {
        // North-Up：箭头按摄像机 Yaw 旋转
        const float yawRad = static_cast<float>(cameraYaw) * (kPi / 180.0f);
        const float cosYaw = cosf(yawRad);
        const float sinYaw = sinf(yawRad);

        // 基础三角形顶点（向上，相对圆心偏移）
        const float vertsIn[3][2] = {
            { 0.0f,  -14.0f },  // 顶点
            { -7.0f,   8.0f },  // 左下
            { 7.0f,    8.0f },  // 右下
        };

        pal::gfx::Vec2 vertsOut[3];
        for (int i = 0; i < 3; ++i) {
            const float sx = vertsIn[i][0] * cosYaw - vertsIn[i][1] * sinYaw;
            const float sy = vertsIn[i][0] * sinYaw + vertsIn[i][1] * cosYaw;
            vertsOut[i].x = center.x + sx;
            vertsOut[i].y = center.y - sy;  // 屏幕 Y 向下为正
        }

        pal::gfx::TriangleFilled(vertsOut[0], vertsOut[1], vertsOut[2], kPlayerArrow);
        pal::gfx::Overlay()->AddTriangle(vertsOut[0], vertsOut[1], vertsOut[2], kPlayerArrowOutline, 1.5f);
    }
}

// ============================================================
// DrawMinimap() — 主绘制入口
// 在覆盖层绘制阶段调用（esp 绘制之后）。
// ============================================================
void DrawMinimap()
{
    // ---- 1. 开关检查 ----
    if (!cheatState.minimapEnabled) return;

    // ---- 2. 读取配置 ----
    const float radarRadius = cheatState.minimapRadius;
    const float worldRadius = cheatState.minimapRange;
    const bool  rotationUp  = cheatState.minimapRotationUp;
    // 标签显示：由 cheatState.minimapShowLabels 控制
    const bool  showLabels  = cheatState.minimapShowLabels;

    // 防除零
    if (radarRadius <= 1.0f || worldRadius <= 1.0f) return;

    // ---- 3. 计算雷达屏幕位置 ----
    const pal::gfx::Vec2 displaySize = pal::gfx::ScreenSize();
    const float radarPosX = cheatState.minimapPosX * displaySize.x;
    const float radarPosY = cheatState.minimapPosY * displaySize.y;
    const pal::gfx::Vec2 center{radarPosX + radarRadius, radarPosY + radarRadius};

    // ---- 4. 获取 UWorld 并采集数据 ----
    // 注意：获取 UWorld 是必要的 SDK 访问，但仅获取指针后传给
    // ScanEntities（其内部有 Helper::Try 保护），所以不需要额外 Try 包裹。
    SDK::UWorld* world = SDK::UWorld::GetWorld();
    if (!world) return;

    Minimap::ScanResult result;
    Helper::Try([&] {
        Minimap::ScanEntities(world, worldRadius, result);
    });
    // 扫描失败时 result.entities 为空，后续绘制仍安全

    // ============================================================
    // 6. 绘制雷达背景
    // ============================================================
    // 半透明黑色填充圆
    pal::gfx::CircleFilled(center, radarRadius, kRadarBg);
    // 白色描边（分段 Line 实现）
    DrawCircleOutline(center, radarRadius, kRadarRing, 1.5f, 64);

    // ============================================================
    // 7. 距离环（2 圈半透明环）
    // ============================================================
    DrawCircleOutline(center, radarRadius * 0.50f, kDistRingOuter, 1.0f, 48);
    DrawCircleOutline(center, radarRadius * 0.75f, kDistRingInner, 1.0f, 48);

    // ============================================================
    // 8. 方位指示
    // ============================================================
    if (rotationUp) {
        // Rotation-Up：十字线
        pal::gfx::Line(pal::gfx::Vec2{center.x - radarRadius, center.y},
                       pal::gfx::Vec2{center.x + radarRadius, center.y},
                       kRadarCross, 1.0f);
        pal::gfx::Line(pal::gfx::Vec2{center.x, center.y - radarRadius},
                       pal::gfx::Vec2{center.x, center.y + radarRadius},
                       kRadarCross, 1.0f);
    } else {
        // North-Up：N/S/E/W 文字标记（按实测文本尺寸定位）
        const float offset = radarRadius + 12.0f;

        const pal::gfx::Vec2 nSize = pal::gfx::TextSize("N");
        const pal::gfx::Vec2 sSize = pal::gfx::TextSize("S");
        const pal::gfx::Vec2 eSize = pal::gfx::TextSize("E");
        const pal::gfx::Vec2 wSize = pal::gfx::TextSize("W");

        pal::gfx::Text(pal::gfx::Vec2{center.x - nSize.x * 0.5f, center.y - offset},                kCompassText, "N");
        pal::gfx::Text(pal::gfx::Vec2{center.x - sSize.x * 0.5f, center.y + offset - sSize.y},      kCompassText, "S");
        pal::gfx::Text(pal::gfx::Vec2{center.x + offset - eSize.x, center.y - eSize.y * 0.5f},      kCompassText, "E");
        pal::gfx::Text(pal::gfx::Vec2{center.x - offset,           center.y - wSize.y * 0.5f},      kCompassText, "W");
    }

    // ============================================================
    // 9. 范围指示文字
    // ============================================================
    {
        const std::string rangeText = std::format("{:.0f}m", worldRadius / 100.0f);
        const pal::gfx::Vec2 rangeSize = pal::gfx::TextSize(rangeText);
        const pal::gfx::Vec2 textPos{center.x - rangeSize.x * 0.5f, center.y + radarRadius + 2.0f};
        pal::gfx::Text(textPos, kRangeText, rangeText);
    }

    // ============================================================
    // 10. 遍历实体绘制（带类型过滤）
    // ============================================================
    for (const auto& entity : result.entities) {
        // 类型过滤（由 cheatState.minimapShow* 字段控制）
        bool showType = true;
        switch (entity.type) {
        case Minimap::EntityType::WildPal:     showType = cheatState.minimapShowWildPals; break;
        case Minimap::EntityType::TamedPal:    showType = cheatState.minimapShowTamedPals; break;
        case Minimap::EntityType::OtherPlayer: showType = cheatState.minimapShowPlayers; break;
        case Minimap::EntityType::NPC:         showType = cheatState.minimapShowNPC; break;
        case Minimap::EntityType::Ore:         showType = cheatState.minimapShowOre; break;
        case Minimap::EntityType::Egg:         showType = cheatState.minimapShowEggs; break;
        case Minimap::EntityType::TreasureBox: showType = cheatState.minimapShowTreasure; break;
        case Minimap::EntityType::Relic:       showType = cheatState.minimapShowRelics; break;
        case Minimap::EntityType::FastTravel:  showType = cheatState.minimapShowFastTravel; break;
        default: break;
        }
        if (!showType) continue;
        // 坐标转换
        const RadarScreenPos rs = WorldToRadarScreen(
            entity.worldPos, result.playerPos, result.cameraYaw,
            rotationUp, center, radarRadius, worldRadius);

        // 从共享契约获取颜色（Minimap::GetEntityTypeColor）
        const pal::gfx::Color color = Minimap::GetEntityTypeColor(entity.type);

        // 绘制图标
        DrawEntityIcon(rs.pos, entity.type, color);

        // 边缘方向指示器
        if (rs.clamped) {
            DrawEdgeIndicator(center, rs.pos, color);
        }

        // 标签（可选）
        if (showLabels && entity.label[0] != '\0') {
            const std::string_view labelText{entity.label};
            const pal::gfx::Vec2 textSize = pal::gfx::TextSize(labelText);
            const pal::gfx::Vec2 labelPos{rs.pos.x + 4.0f, rs.pos.y - textSize.y};

            // 标签背景（Shadow 的 AddRectFilled 无圆角参数，原 2.0f 圆角舍弃）
            pal::gfx::RectFilledMinMax(
                pal::gfx::Vec2{labelPos.x - 1.0f, labelPos.y - 1.0f},
                pal::gfx::Vec2{labelPos.x + textSize.x + 1.0f, labelPos.y + textSize.y + 1.0f},
                kLabelBg);
            pal::gfx::Text(labelPos, kLabelText, labelText);
        }
    }

    // ============================================================
    // 11. 玩家箭头（最上层绘制，确保不被实体遮挡）
    // ============================================================
    DrawPlayerArrow(center, rotationUp, result.cameraYaw);
}
