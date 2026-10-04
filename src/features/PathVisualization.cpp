// ============================================================
// PathVisualization.cpp — 路径在 3D 世界中的绘制实现
//
// v2 性能设计（修复“路线卡顿但不掉帧”问题）：
//
//   旧实现的症状根因：
//   为避免每帧对每个航点调用 ProjectWorldLocationToScreen
//   （ProcessEvent），旧版做了极端缓存 —— 相机移动 < 5 米、
//   旋转 < 20 度时屏幕坐标完全不更新。于是转动视角时路线
//   “钉死”在旧屏幕位置，超过阈值后突然跳变，观感极其卡顿；
//   而每帧几乎不做计算，所以游戏帧率和 ESP（每帧投影）完全
//   不受影响。另外路径重算后航点数变化时缓存永不刷新，
//   以及触发重投影时几百个航点的 ProcessEvent 突发尖峰。
//
//   新实现：
//   1. 每帧构建一次相机投影器（全部路径共享）：
//      - 1 次 GetPlayerViewPoint 拿相机位置/朝向；
//      - 3 次 ProjectWorldLocationToScreen 投影“标定点”
//        （正前方 / 右偏 / 上偏），反解出屏幕中心的像素
//        系数 kx/ky。标定法自动适配任意 FOV 语义、宽高比
//        约束、画面缩放，无需猜测引擎投影矩阵细节。
//   2. 所有航点用纯数学投影：世界 → 相机基点积 → 透视
//      除法 → 屏幕像素。单点纳秒级，与航点数量无关，
//      路线每帧精确跟随相机，彻底流畅。
// ============================================================
#include <pch.h>
#include "PathVisualization.hpp"
#include "CheatState.hpp"
#include "engine/GameHelper.hpp"
#include "core/Gfx.hpp"
#include "core/Log.hpp"

#include <cmath>
#include <algorithm>
#include <string>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace SDK;
using namespace Helper;

namespace
{
    namespace gfx = pal::gfx;

    // 相机投影器：每帧构建一次，全部路径/航点共享
    struct CameraProjector
    {
        SDK::FVector camLoc{};
        SDK::FVector forward{};      // 相机前向（X 轴）
        SDK::FVector right{};        // 相机右向（Y 轴）
        SDK::FVector up{};           // 相机上向（Z 轴）
        double      kx = 0.0;        // 深度 1 处，每单位相机 X 的屏幕像素数
        double      ky = 0.0;        // 深度 1 处，每单位相机 Y 的屏幕像素数
        double      cx = 0.0;        // 光轴（屏幕中心）X
        double      cy = 0.0;        // 光轴（屏幕中心）Y
        bool        valid = false;
    };

    // 每帧一次：用 3 个标定点反解投影系数。
    // 标定点都取在相机正前方（深度 1000cm、偏移 100cm ≈ 5.7°，
    // 任何正常 FOV 下都在视锥内），因此投影坐标始终有效。
    bool BuildCameraProjector(SDK::APlayerController* pc, CameraProjector& out)
    {
        SDK::FVector camLoc{};
        SDK::FRotator camRot{};
        if (!Try([&] { pc->GetPlayerViewPoint(&camLoc, &camRot); }))
            return false;

        // UE FRotationMatrix 的三个轴（Pitch/Yaw/Roll，弧度）
        constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
        const double p  = camRot.Pitch * kDegToRad;
        const double yw = camRot.Yaw   * kDegToRad;
        const double rl = camRot.Roll  * kDegToRad;
        const double cp = std::cos(p),  sp = std::sin(p);
        const double cy = std::cos(yw), sy = std::sin(yw);
        const double cr = std::cos(rl), sr = std::sin(rl);

        out.forward = SDK::FVector(cp * cy, cp * sy, sp);
        out.right   = SDK::FVector(cy * sp * sr - sy * cr,
                                   sy * sp * sr + cy * cr,
                                   -cp * sr);
        out.up      = SDK::FVector(-cy * sp * cr - sy * sr,
                                   -sy * sp * cr + cy * sr,
                                   cp * cr);
        out.camLoc  = camLoc;

        constexpr double kCalibDist = 1000.0;   // 标定点深度（厘米）
        constexpr double kCalibOff  = 100.0;    // 标定偏移（厘米）

        const SDK::FVector p0 = camLoc + out.forward * kCalibDist;
        const SDK::FVector p1 = p0 + out.right * kCalibOff;
        const SDK::FVector p2 = p0 + out.up * kCalibOff;

        SDK::FVector2D s0{}, s1{}, s2{};
        // ProjectWorldLocationToScreen 对屏外点也会写入有效坐标，
        // 返回值仅表示“是否在屏幕内”，这里不依赖返回值。
        if (!Try([&] { pc->ProjectWorldLocationToScreen(p0, &s0, false); }))
            return false;
        if (!Try([&] { pc->ProjectWorldLocationToScreen(p1, &s1, false); }))
            return false;
        if (!Try([&] { pc->ProjectWorldLocationToScreen(p2, &s2, false); }))
            return false;

        // 屏幕中心 = 光轴点 P0 的投影
        out.cx = s0.X;
        out.cy = s0.Y;
        // P1 相对 P0：相机空间 X +off、深度 kCalibDist
        //   s1.X - s0.X = (off / kCalibDist) * kx
        out.kx = (s1.X - s0.X) * kCalibDist / kCalibOff;
        // P2 相对 P0：相机空间 Y +off（屏幕 Y 轴向下）
        //   s0.Y - s2.Y = (off / kCalibDist) * ky
        out.ky = (s0.Y - s2.Y) * kCalibDist / kCalibOff;

        out.valid = (out.kx > 0.0 && out.ky > 0.0);
        return out.valid;
    }

    // 纯数学投影（无 ProcessEvent）：世界坐标 → 屏幕像素。
    // 返回 false 表示点在相机背后/贴近视平面（跳过该点）。
    bool ProjectPointFast(const CameraProjector& pr, const SDK::FVector& world, gfx::Vec2& out)
    {
        const SDK::FVector d = world - pr.camLoc;
        const double z = d.Dot(pr.forward);   // 深度
        if (z < 1.0)
            return false;
        const double x = d.Dot(pr.right);
        const double y = d.Dot(pr.up);
        out.x = static_cast<float>(pr.cx + x / z * pr.kx);
        out.y = static_cast<float>(pr.cy - y / z * pr.ky);
        return true;
    }

    // float[4] RGBA (0..1) → gfx::Color；alpha 为附加透明度系数 (0..1)
    gfx::Color ColorRGBA(const float* c, float alpha = 1.0f)
    {
        if (!c) return gfx::Color{1.f, 1.f, 1.f, 1.f};
        return gfx::Color{
            std::clamp(c[0], 0.0f, 1.0f),
            std::clamp(c[1], 0.0f, 1.0f),
            std::clamp(c[2], 0.0f, 1.0f),
            std::clamp(c[3] * alpha, 0.0f, 1.0f)
        };
    }

    // 绘制单条路径：所有航点当帧全量投影（数学投影，开销可忽略）。
    // playerPos 为玩家当前位置：玩家偏离路径起点时动态插入为路线头部，
    // 使路线起点每帧贴合玩家脚下（后台重算周期内的视觉跟随）。
    void DrawSinglePath(const PathResult& path, const PathVisualizeSettings& settings,
                        const CameraProjector& pr,
                        const SDK::FVector& playerPos, bool havePlayer)
    {
        if (path.waypoints.empty())
            return;

        // 距离剔除（米）：终点离相机太远则整条不画
        const double distToEnd = pr.camLoc.GetDistanceTo(path.waypoints.back().location) * 0.01;
        if (distToEnd > static_cast<double>(settings.maxDrawDistance))
            return;

        // 玩家偏离路径起点（重算节流窗口内）→ 插入玩家当前位置为动态起点
        const bool insertPlayer = havePlayer &&
            playerPos.GetDistanceTo(path.startLocation) > 150.0;

        std::vector<gfx::Vec2> visible;
        visible.reserve(path.waypoints.size() + 1);
        if (insertPlayer)
        {
            gfx::Vec2 sp{};
            SDK::FVector raised = playerPos;
            raised.Z += settings.zOffset;
            if (ProjectPointFast(pr, raised, sp))
                visible.push_back(sp);
        }
        for (const auto& wp : path.waypoints)
        {
            SDK::FVector raised = wp.location;
            raised.Z += settings.zOffset;
            gfx::Vec2 sp{};
            if (ProjectPointFast(pr, raised, sp))
                visible.push_back(sp);
        }
        if (visible.size() < 2)
            return;

        // 部分路径（不可达，只能到最近点）用橙色提示
        const float* lineCol = path.isPartial ? settings.partialColor : settings.lineColor;
        const gfx::Color lineColC = ColorRGBA(lineCol);

        if (settings.showPathLine)
        {
            // 门面无折线接口，用分段线段绘制开放折线（语义同 AddPolyline 非闭合）
            for (size_t i = 1; i < visible.size(); ++i)
                gfx::Line(visible[i - 1], visible[i], lineColC, settings.lineThickness);
        }

        if (settings.showStartEnd)
        {
            const gfx::Vec2 spt = visible.front();
            const float s = settings.nodeSize;
            gfx::TriangleFilled(gfx::Vec2{spt.x - s, spt.y + s}, gfx::Vec2{spt.x + s, spt.y + s},
                                gfx::Vec2{spt.x, spt.y - s}, ColorRGBA(settings.startColor));
            const gfx::Vec2 ept = visible.back();
            gfx::TriangleFilled(gfx::Vec2{ept.x - s, ept.y + s}, gfx::Vec2{ept.x + s, ept.y + s},
                                gfx::Vec2{ept.x, ept.y - s}, ColorRGBA(settings.endColor));
        }

        if (settings.showDistanceLabel)
        {
            const size_t mid = visible.size() / 2;
            if (mid < visible.size())
            {
                const float totalM = path.lengthCm / 100.0f;
                const std::string label = path.isPartial
                    ? std::format("≈{:.0f} m（部分）", totalM)
                    : std::format("{:.0f} m", totalM);
                const gfx::Vec2 pos = visible[mid];
                const gfx::Vec2 ts = gfx::TextSize(label);
                // 半透明黑底（Shadow AddRectFilled 无圆角参数）
                gfx::RectFilledMinMax(gfx::Vec2{pos.x - ts.x * 0.5f - 4, pos.y - ts.y - 4},
                                      gfx::Vec2{pos.x + ts.x * 0.5f + 4, pos.y + 2},
                                      gfx::Color{0.f, 0.f, 0.f, 160.f / 255.f});
                gfx::Text(gfx::Vec2{pos.x - ts.x * 0.5f, pos.y - ts.y}, lineColC, label);
            }
        }

        // 在终点处绘制目标名称（寻路目标是……）
        if (!path.targetLabel.empty())
        {
            const gfx::Vec2 ept = visible.back();
            const std::string name = std::format("→ {}", path.targetLabel);
            const gfx::Vec2 nameSize = gfx::TextSize(name);
            const float nameX = ept.x - nameSize.x * 0.5f;
            const float nameY = ept.y - settings.nodeSize - nameSize.y - 4;
            // 半透明背景
            gfx::RectFilledMinMax(gfx::Vec2{nameX - 4, nameY - 2},
                                  gfx::Vec2{nameX + nameSize.x + 4, nameY + nameSize.y + 2},
                                  gfx::Color{0.f, 0.f, 0.f, 180.f / 255.f});
            // 目标名称文字（青色高亮）
            gfx::Text(gfx::Vec2{nameX, nameY}, ColorRGBA(settings.lineColor), name);
        }
    }
}

PathVisualizeSettings PathVisualizeSettings::FromCheatState()
{
    PathVisualizeSettings s;
    s.enabled = cheatState.pathVisualizationEnabled;
    s.showPathLine = true;
    s.showWaypoints = false;
    s.showStartEnd = true;
    s.showDistanceLabel = true;
    s.showDirectionArrows = false;
    for (int i = 0; i < 4; ++i)
    {
        s.lineColor[i] = cheatState.pathColor[i];
        s.startColor[i] = cheatState.pathStartColor[i];
        s.endColor[i] = cheatState.pathEndColor[i];
        s.partialColor[i] = cheatState.pathPartialColor[i];
    }
    s.lineThickness = cheatState.pathLineThickness;
    s.waypointSize = cheatState.pathWaypointSize;
    s.nodeSize = 6.0f;
    s.maxDrawDistance = 2000;
    s.zOffset = 30.0f;
    return s;
}

void DrawAllActivePaths(PathManager& mgr, const PathVisualizeSettings& settings,
                        SDK::APlayerController* pc)
{
    if (!pc || !settings.enabled) return;

    // 每帧构建一次相机投影器（4 次 ProcessEvent，与路径/航点数量无关）
    CameraProjector pr;
    if (!BuildCameraProjector(pc, pr))
        return;

    // 每帧取一次玩家当前位置（1 次 ProcessEvent）：
    // 路线头部动态贴合玩家，起点不再是被点击时的死快照
    SDK::FVector playerPos{};
    bool havePlayer = false;
    SDK::APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (player && IsProbablyValidPtr(player))
        havePlayer = Try([&] { playerPos = player->K2_GetActorLocation(); });

    mgr.ForEachActivePath([&](uint64_t id, const PathResult& path) -> bool {
        if (!path.waypoints.empty())
            DrawSinglePath(path, settings, pr, playerPos, havePlayer);
        return true;
    });
}
