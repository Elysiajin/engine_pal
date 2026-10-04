#include <pch.h>
#include "Esp.hpp"
#include <Windows.h>
#include <cmath>
#include <unordered_map>
#include <algorithm>
#include <cfloat>
#include "core/Gfx.hpp"
#include "core/Log.hpp"
#include "CheatState.hpp"
#include "engine/GameHelper.hpp"



#include "engine/GameHelper.hpp"
#include "Database.hpp"
#include "engine/NameMapper.hpp"

using namespace Helper;
using namespace SDK;

namespace gfx = pal::gfx;   // 覆盖层绘制门面别名 (本文件在全局命名空间)



std::string GetCleanPalName(const std::string& rawName) {
    size_t start = 0;

    // Remove "BP_" prefix if present
    if (rawName.find("BP_") == 0)
        start += 3;

    // Remove "NPC_" prefix if present (after BP_ or at start)
    if (rawName.find("NPC_", start) == start)
        start += 4;

    // Find _C (typical UE4/5 suffix)
    size_t end = rawName.find("_C", start);
    std::string coreName = (end != std::string::npos)
        ? rawName.substr(start, end - start)
        : rawName.substr(start);

    // Remove trailing digits (e.g. "_123456")
    while (!coreName.empty() && std::isdigit(coreName.back()))
        coreName.pop_back();

    // Remove trailing underscores after digits
    while (!coreName.empty() && coreName.back() == '_')
        coreName.pop_back();

    return coreName;
}

void DrawRelicESPText(pal::gfx::Vec2 screenPos, const char* nameLabel, float distance)
{
    pal::gfx::Vec2 nameSize = pal::gfx::TextSize(nameLabel);

    // Center name
    screenPos.x -= nameSize.x / 2.0f;

    // Background + shadow for name
    pal::gfx::Vec2 nameBgMin = pal::gfx::Vec2(screenPos.x - 4, screenPos.y - 2);
    pal::gfx::Vec2 nameBgMax = pal::gfx::Vec2(screenPos.x + nameSize.x + 4, screenPos.y + nameSize.y + 2);
    pal::gfx::RectFilledMinMax(nameBgMin, nameBgMax, pal::gfx::Color{0.000f, 0.000f, 0.000f, 0.549f});
    pal::gfx::Text(pal::gfx::Vec2(screenPos.x + 1, screenPos.y + 1), pal::gfx::Color{0.000f, 0.000f, 0.000f, 0.784f}, nameLabel);
    pal::gfx::Text(screenPos, pal::gfx::Color{1.000f, 1.000f, 0.392f, 1.000f}, nameLabel);

    // Advance Y for distance label
    if (cheatState.espShowDistance)
    {
        const std::string distLabel = std::format("[{:.1f}m]", distance / 100.0f);

        pal::gfx::Vec2 distSize = pal::gfx::TextSize(distLabel);
        pal::gfx::Vec2 distPos = pal::gfx::Vec2(screenPos.x + nameSize.x / 2.0f - distSize.x / 2.0f, screenPos.y + nameSize.y + 4);

        pal::gfx::Vec2 distBgMin = pal::gfx::Vec2(distPos.x - 4, distPos.y - 2);
        pal::gfx::Vec2 distBgMax = pal::gfx::Vec2(distPos.x + distSize.x + 4, distPos.y + distSize.y + 2);
        pal::gfx::RectFilledMinMax(distBgMin, distBgMax, pal::gfx::Color{0.000f, 0.000f, 0.000f, 0.549f});
        pal::gfx::Text(pal::gfx::Vec2(distPos.x + 1, distPos.y + 1), pal::gfx::Color{0.000f, 0.000f, 0.000f, 0.784f}, distLabel);
        pal::gfx::Text(distPos, pal::gfx::Color{0.471f, 1.000f, 0.471f, 1.000f}, distLabel);
    }
}

void DrawESPText(pal::gfx::Vec2 screenPos, const char* name, float distance = -1.0f, pal::gfx::Color color = pal::gfx::Color{1.000f, 1.000f, 1.000f, 1.000f})
{
    // Skip entire ESP if out of range
    if (distance >= 0.0f && distance > cheatState.espDistance)
        return;

    pal::gfx::Vec2 nameSize = pal::gfx::TextSize(name);
    pal::gfx::Vec2 pos = screenPos;
    pos.x -= nameSize.x / 2.0f;

    // Outline
    pal::gfx::Text(pal::gfx::Vec2(pos.x + 1, pos.y + 1),
        pal::gfx::Color{0.000f, 0.000f, 0.000f, 0.784f}, name);

    // Main text with passed color
    pal::gfx::Text(pos, color, name);

    // Optional distance label
    if (cheatState.espShowDistance && distance >= 0.0f && distance <= cheatState.espDistance)
    {
        const std::string distText = std::format("[{:.0f}m]", distance / 100.0f);

        pal::gfx::Vec2 distSize = pal::gfx::TextSize(distText);
        pal::gfx::Vec2 distPos = pal::gfx::Vec2(screenPos.x - distSize.x / 2.0f, pos.y + nameSize.y + 4);

        pal::gfx::RectFilledMinMax(
            pal::gfx::Vec2(distPos.x - 4, distPos.y - 2),
            pal::gfx::Vec2(distPos.x + distSize.x + 4, distPos.y + distSize.y + 2),
            pal::gfx::Color{0.000f, 0.000f, 0.000f, 0.549f});

        pal::gfx::Text(pal::gfx::Vec2(distPos.x + 1, distPos.y + 1),
            pal::gfx::Color{0.000f, 0.000f, 0.000f, 0.784f}, distText);

        pal::gfx::Text(distPos,
            pal::gfx::Color{0.471f, 1.000f, 0.471f, 1.000f}, distText);
    }
}

// ============================================================================
// ESP 增强: 3D 方框 / 骨骼可视化 / 热能透视
// ============================================================================

// 把 FName 字符串转为 FName（用于骨骼 socket 名）
static SDK::FName EspStringToFName(const char* name)
{
    static SDK::UKismetStringLibrary* lib = SDK::UKismetStringLibrary::GetDefaultObj();
    if (!lib) return SDK::FName();
    std::wstring w(name, name + strlen(name));
    return lib->Conv_StringToName(SDK::FString(w.c_str()));
}

// 投影一个世界坐标到屏幕; 成功返回 true
static bool ProjectPoint(SDK::APlayerController* pc, const SDK::FVector& world, SDK::FVector2D& out)
{
    if (!pc) return false;
    bool ok = false;
    if (!Helper::Try([&] { ok = pc->ProjectWorldLocationToScreen(world, &out, false); }))
        return false;
    return ok;
}

// 获取角色骨骼 socket 的世界坐标; 失败返回 false
static bool GetBoneWorld(SDK::APalCharacter* pal, const char* socketName, SDK::FVector& out)
{
    if (!pal) return false;
    SDK::USceneComponent* mesh = nullptr;
    if (!Helper::Try([&] { mesh = pal->GetMainMesh(); }) || !Helper::IsProbablyValidPtr(mesh))
        return false;
    SDK::FName fn = EspStringToFName(socketName);
    if (fn.IsNone()) return false;
    bool ok = false;
    if (!Helper::Try([&] { out = mesh->GetSocketLocation(fn); ok = true; }))
        return false;
    return ok && !out.IsZero();
}

// ============================================================================
// 通用骨骼采集系统（修复非人形生物骨骼显示问题）
//
// 原实现使用硬编码人形骨骼名列表(head/neck_01/spine_01/pelvis/upperarm_l/...)，
// 对四足动物(马/狼)、鱼类、鸟类等非人形帕鲁几乎全部失效，导致骨骼显示为
// 单线或三角形。新方案通过 USkinnedMeshComponent::GetNumBones / GetBoneName /
// GetParentBone 枚举角色实际的骨骼层级结构，连接每个骨骼到其父骨骼绘制，
// 适用于任意生物类型（人形/四足/鱼/鸟等），显示效果与角色真实骨架一致。
// ============================================================================

// 一个采集到的骨骼节点：世界坐标 + 屏幕坐标 + 是否有效
struct CollectedBone
{
    SDK::FVector    world;
    SDK::FVector2D  screen;
    bool            valid;
    int             parentIndex; // -1 表示根骨骼
};

// 采集角色全部骨骼的世界坐标并投影到屏幕。
// 返回骨骼数组；每个元素记录 parentIndex（通过 GetParentBone 解析）。
// maxBones 限制采集数量，避免极端骨架过长导致性能问题。
static bool CollectSkeletonBonesUniversal(SDK::APlayerController* pc, SDK::APalCharacter* pal,
                                          std::vector<CollectedBone>& outBones, int maxBones = 128)
{
    outBones.clear();
    if (!pal || !pc) return false;

    SDK::USceneComponent* mesh = nullptr;
    if (!Helper::Try([&] { mesh = pal->GetMainMesh(); }) || !Helper::IsProbablyValidPtr(mesh))
        return false;

    // 尝试转为 USkinnedMeshComponent 以访问骨骼枚举 API
    SDK::USkinnedMeshComponent* skinnedMesh = nullptr;
    if (!Helper::Try([&] { skinnedMesh = reinterpret_cast<SDK::USkinnedMeshComponent*>(mesh); }))
        return false;
    if (!Helper::IsProbablyValidPtr(skinnedMesh))
        return false;

    int numBones = 0;
    if (!Helper::Try([&] { numBones = skinnedMesh->GetNumBones(); }))
        return false;
    if (numBones <= 0 || numBones > 4000)
        return false;

    if (numBones > maxBones)
        numBones = maxBones;

    // 采集骨骼名 + 世界坐标 + 投影
    struct BoneNameEntry { SDK::FName name; int parentIndex; };
    std::vector<BoneNameEntry> entries;
    entries.resize(numBones);

    for (int i = 0; i < numBones; ++i)
    {
        entries[i].name = SDK::FName();
        entries[i].parentIndex = -1;
        if (!Helper::Try([&] { entries[i].name = skinnedMesh->GetBoneName(i); }))
            continue;
    }

    // 解析父骨骼索引
    for (int i = 0; i < numBones; ++i)
    {
        if (entries[i].name.IsNone())
            continue;
        SDK::FName parentName;
        if (!Helper::Try([&] { parentName = skinnedMesh->GetParentBone(entries[i].name); }))
            continue;
        if (parentName.IsNone())
            continue; // 根骨骼，parentIndex 保持 -1
        int pIdx = -1;
        if (Helper::Try([&] { pIdx = skinnedMesh->GetBoneIndex(parentName); }))
            entries[i].parentIndex = pIdx;
    }

    // 采集世界坐标并投影
    outBones.resize(numBones);
    for (int i = 0; i < numBones; ++i)
    {
        outBones[i].valid = false;
        outBones[i].parentIndex = entries[i].parentIndex;
        outBones[i].world = SDK::FVector(0, 0, 0);
        outBones[i].screen = SDK::FVector2D(0, 0);

        if (entries[i].name.IsNone())
            continue;

        // 用 GetSocketLocation 获取骨骼世界坐标（骨骼本身就是隐式 socket）
        SDK::FVector w;
        bool ok = false;
        if (Helper::Try([&] { w = skinnedMesh->GetSocketLocation(entries[i].name); ok = true; }) && ok)
        {
            if (!w.IsZero())
            {
                outBones[i].world = w;
                if (ProjectPoint(pc, w, outBones[i].screen))
                    outBones[i].valid = true;
            }
        }
    }

    // 如果几乎全部骨骼都无效，说明通用枚举失败了
    int nValid = 0;
    for (auto& b : outBones)
        if (b.valid) ++nValid;

    return nValid > 0;
}

// 过滤：只保留有实际连接关系的骨骼（父骨骼也有效），减少噪声根骨骼。
// 同时跳过坐标完全相同（退化）的骨骼对。
static void DrawUniversalSkeleton(SDK::APlayerController* pc, SDK::APalCharacter* pal, pal::gfx::Color color)
{
    if (!pal || !pc) return;

    std::vector<CollectedBone> bones;
    if (!CollectSkeletonBonesUniversal(pc, pal, bones))
        return;

    auto drawList = pal::gfx::Overlay();

    // 绘制每条 骨骼->父骨骼 的连线
    for (size_t i = 0; i < bones.size(); ++i)
    {
        if (!bones[i].valid)
            continue;
        int p = bones[i].parentIndex;
        if (p < 0 || p >= (int)bones.size())
            continue;
        if (!bones[p].valid)
            continue;

        // 跳过退化连线（两端坐标相同）
        float dx = bones[i].screen.X - bones[p].screen.X;
        float dy = bones[i].screen.Y - bones[p].screen.Y;
        if (dx * dx + dy * dy < 1.0f)
            continue;

        pal::gfx::Line(
            pal::gfx::Vec2(bones[i].screen.X, bones[i].screen.Y),
            pal::gfx::Vec2(bones[p].screen.X, bones[p].screen.Y),
            color, 1.5f);
    }

    // 绘制关节点
    for (size_t i = 0; i < bones.size(); ++i)
    {
        if (!bones[i].valid)
            continue;
        pal::gfx::CircleFilled(
            pal::gfx::Vec2(bones[i].screen.X, bones[i].screen.Y), 2.5f, color);
    }
}

// 3D 方框: 根据角色脚底/头顶构造世界空间 AABB, 投影 8 个角并绘制 12 条边。
// 框高/宽根据距离与角色体型自适应。

// 采集角色全身骨骼的最高/最低点与包围半径, 用于方框与热能透视。
// 使用通用骨骼枚举（适配所有生物类型），失败则回退到脚底/头顶的估算值。
static void GetBodyBoundsWorld(SDK::APalCharacter* pal, const SDK::FVector& palLocation,
                               float fallbackHeight, float fallbackRadius,
                               SDK::FVector& outTop, SDK::FVector& outBottom, float& outRadius)
{
    // 优先使用通用骨骼枚举获取真实身体范围
    std::vector<CollectedBone> bones;
    // 传入 nullptr 的 controller 仅采集世界坐标不做投影——但 CollectSkeletonBonesUniversal
    // 需要 controller 做投影。这里直接用骨骼世界坐标算包围盒即可。
    // 简化：直接用 GetBoneWorld 采样几个关键骨骼做兜底。
    float maxZ = -FLT_MAX, minZ = FLT_MAX;
    bool anyBone = false;

    // 尝试通过通用骨骼枚举获取真实包围盒（不依赖 controller 投影）
    {
        SDK::USceneComponent* mesh = nullptr;
        if (Helper::Try([&] { mesh = pal->GetMainMesh(); }) && Helper::IsProbablyValidPtr(mesh))
        {
            SDK::USkinnedMeshComponent* skinnedMesh = nullptr;
            if (Helper::Try([&] { skinnedMesh = reinterpret_cast<SDK::USkinnedMeshComponent*>(mesh); }) &&
                Helper::IsProbablyValidPtr(skinnedMesh))
            {
                int numBones = 0;
                if (Helper::Try([&] { numBones = skinnedMesh->GetNumBones(); }) && numBones > 0 && numBones <= 4000)
                {
                    int limit = numBones < 128 ? numBones : 128;
                    for (int i = 0; i < limit; ++i)
                    {
                        SDK::FName bname;
                        if (!Helper::Try([&] { bname = skinnedMesh->GetBoneName(i); }))
                            continue;
                        if (bname.IsNone())
                            continue;
                        SDK::FVector w;
                        bool ok = false;
                        if (Helper::Try([&] { w = skinnedMesh->GetSocketLocation(bname); ok = true; }) && ok)
                        {
                            if (w.IsZero())
                                continue;
                            anyBone = true;
                            if (w.Z > maxZ) maxZ = w.Z;
                            if (w.Z < minZ) minZ = w.Z;
                        }
                    }
                }
            }
        }
    }

    // 回退：旧版硬编码人形骨骼名采样
    if (!anyBone)
    {
        const char* bones[] = {
            "head", "neck_01", "spine_01", "pelvis",
            "upperarm_l", "lowerarm_l", "hand_l",
            "upperarm_r", "lowerarm_r", "hand_r",
            "thigh_l", "calf_l", "foot_l",
            "thigh_r", "calf_r", "foot_r"
        };
        const int nBones = sizeof(bones) / sizeof(bones[0]);
        for (int i = 0; i < nBones; ++i)
        {
            SDK::FVector w;
            if (!GetBoneWorld(pal, bones[i], w))
                continue;
            anyBone = true;
            if (w.Z > maxZ) maxZ = w.Z;
            if (w.Z < minZ) minZ = w.Z;
        }
    }

    if (anyBone && minZ > -999999.f && maxZ > -999999.f)
    {
        // 以骨骼最高/最低点作为垂直边界
        outBottom = SDK::FVector(palLocation.X, palLocation.Y, minZ);
        outTop    = SDK::FVector(palLocation.X, palLocation.Y, maxZ);
        // 包围半径: 用角色体型估算(高度比例), 保证方框能把整个角色横向包裹
        float height = (maxZ - minZ);
        if (height < 10.f) height = fallbackHeight;
        outRadius = height * 0.28f;
        if (outRadius < 10.f) outRadius = fallbackRadius;
    }
    else
    {
        outBottom = palLocation;
        outTop    = palLocation + SDK::FVector(0, 0, fallbackHeight);
        outRadius = fallbackRadius;
    }
}

static void DrawESPBox3D(SDK::APlayerController* pc, SDK::APalCharacter* pal,
                         const SDK::FVector& footWorld, const SDK::FVector& headWorld,
                         float radius, pal::gfx::Color color)
{
    if (!pal || !pc) return;

    // 以"角色原点"(actor origin) 为方框锚点。角色原点一定投影成功
    // (名字/2D 方框都用它且显示正常), 保证任何可见帕鲁都能画出一个框。
    // 尺寸只用固定/受限制的合理值, 不依赖骨骼——骨骼 socket 间距对某些
    // 帕鲁异常, 会导致框偏到角色之外或超大, 表现为"没画出来"。
    SDK::FVector origin = footWorld;

    // 高度: 用骨骼粗略估计, 但强制限制在合理范围 [60, 300]; 非法/NaN 兜底。
    float boxH = 190.f;
    {
        SDK::FVector bTop, bBottom;
        float bRadius = 45.0f;
        GetBodyBoundsWorld(pal, origin, 190.0f, 45.0f, bTop, bBottom, bRadius);
        float h = bTop.Z - bBottom.Z;
        if (h >= 60.f && h <= 300.f && h == h)
            boxH = h;
    }

    // 半宽: 限制在 [18, 80], 保证框不会超出屏幕。
    float boxRadius = 45.f;
    if (radius >= 18.f && radius <= 80.f)
        boxRadius = radius;

    // 垂直定位: 让框的中点大致落在角色原点高度, 顶点=原点+boxH。
    SDK::FVector foot = origin;
    SDK::FVector head = origin + SDK::FVector(0, 0, boxH);

    // 面向方向: 以角色朝向为准(绕 Z 轴), 计算失败则退化为正方向
    SDK::FVector forward(1, 0, 0);
    {
        SDK::FRotator rot;
        if (Helper::Try([&] { rot = pal->K2_GetActorRotation(); }))
        {
            SDK::FVector fwd;
            if (Helper::Try([&] { fwd = SDK::UKismetMathLibrary::GetForwardVector(rot); }))
            {
                fwd.Z = 0.0f;
                if (!fwd.IsZero())
                {
                    fwd.Normalize();
                    forward = fwd;
                }
            }
        }
    }
    // right = up × forward
    SDK::FVector up(0, 0, 1);
    SDK::FVector right(up.Y * forward.Z - up.Z * forward.Y,
                       up.Z * forward.X - up.X * forward.Z,
                       up.X * forward.Y - up.Y * forward.X);

    // 8 个角的本地偏移 (相对脚底中心)
    SDK::FVector corners[8];
    const float hw = boxRadius, hl = boxRadius;
    SDK::FVector base[4] = {
        foot + forward * hl + right * hw,     // 0 前右
        foot + forward * hl - right * hw,     // 1 前左
        foot - forward * hl - right * hw,     // 2 后左
        foot - forward * hl + right * hw      // 3 后右
    };
    for (int i = 0; i < 4; ++i)
    {
        corners[i]     = base[i];
        corners[i + 4] = base[i] + up * (head.Z - foot.Z);
    }

    // 逐个角投影, 容忍个别角失败(如被摄像机裁剪/在身后)。
    // 若某角失败, 仅跳过以它为端点的边, 不再整框放弃。
    SDK::FVector2D proj[8];
    bool projOk[8] = { false };
    int nOk = 0;
    for (int i = 0; i < 8; ++i)
        if (ProjectPoint(pc, corners[i], proj[i])) { projOk[i] = true; ++nOk; }

    auto drawList = pal::gfx::Overlay();
    auto edge = [&](int a, int b) {
        if (projOk[a] && projOk[b])
            pal::gfx::Line(pal::gfx::Vec2(proj[a].X, proj[a].Y),
                              pal::gfx::Vec2(proj[b].X, proj[b].Y), color, 1.5f);
    };

    // 至少要有 4 个角可用才画真正的 3D 框; 否则回退到屏幕空间方框。
    if (nOk >= 4)
    {
        // 底面 4 边
        for (int i = 0; i < 4; ++i) edge(i, (i + 1) % 4);
        // 顶面 4 边
        for (int i = 4; i < 8; ++i) edge(i, (i == 7) ? 4 : (i + 1));
        // 垂直 4 条边
        for (int i = 0; i < 4; ++i) edge(i, i + 4);
        return;
    }

    // 回退: 以"角色原点"投影为中心画 2D 屏幕方框(保证一定可见)。
    SDK::FVector2D originScr, headScr;
    if (!ProjectPoint(pc, origin, originScr))
        return;
    float boxHeight = 100.0f;
    if (ProjectPoint(pc, head, headScr))
    {
        float h = headScr.Y - originScr.Y;
        if (h > 24.0f) boxHeight = h;
    }
    float boxWidth = boxHeight * 0.5f;
    pal::gfx::Vec2 tl(originScr.X - boxWidth * 0.5f, originScr.Y - boxHeight * 0.5f);
    pal::gfx::Vec2 br(originScr.X + boxWidth * 0.5f, originScr.Y + boxHeight * 0.5f);
    pal::gfx::RectMinMax(tl, br, color, 1.5f);
}

// 骨骼可视化: 通用骨骼层级枚举方案，适配任意生物类型（人形/四足/鱼/鸟）。
// 通过 GetNumBones + GetBoneName + GetParentBone 枚举实际骨骼结构，
// 连接每根骨骼到其父骨骼绘制完整骨架，不再依赖硬编码骨骼名。
static void DrawSkeleton(SDK::APlayerController* pc, SDK::APalCharacter* pal, pal::gfx::Color color)
{
    if (!pal || !pc) return;

    // 优先使用通用骨骼枚举（修复非人形生物显示问题）
    std::vector<CollectedBone> bones;
    if (CollectSkeletonBonesUniversal(pc, pal, bones))
    {
        auto drawList = pal::gfx::Overlay();

        // 绘制骨骼连线
        for (size_t i = 0; i < bones.size(); ++i)
        {
            if (!bones[i].valid)
                continue;
            int p = bones[i].parentIndex;
            if (p < 0 || p >= (int)bones.size())
                continue;
            if (!bones[p].valid)
                continue;

            float dx = bones[i].screen.X - bones[p].screen.X;
            float dy = bones[i].screen.Y - bones[p].screen.Y;
            if (dx * dx + dy * dy < 1.0f)
                continue;

            pal::gfx::Line(
                pal::gfx::Vec2(bones[i].screen.X, bones[i].screen.Y),
                pal::gfx::Vec2(bones[p].screen.X, bones[p].screen.Y),
                color, 1.5f);
        }

        // 关节节点
        for (size_t i = 0; i < bones.size(); ++i)
            if (bones[i].valid)
                pal::gfx::CircleFilled(
                    pal::gfx::Vec2(bones[i].screen.X, bones[i].screen.Y), 2.5f, color);

        return; // 成功绘制，不再走旧路径
    }

    // 回退：旧版硬编码人形骨骼名（仅在通用枚举失败时使用）
    const char* boneNames[] = {
        "head", "neck_01", "spine_01", "pelvis",
        "upperarm_l", "lowerarm_l", "hand_l",
        "upperarm_r", "lowerarm_r", "hand_r",
        "thigh_l", "calf_l", "foot_l",
        "thigh_r", "calf_r", "foot_r"
    };
    const int nBones = sizeof(boneNames) / sizeof(boneNames[0]);

    SDK::FVector world[16];
    SDK::FVector2D screen[16];
    bool valid[16] = { false };

    for (int i = 0; i < nBones; ++i)
    {
        if (GetBoneWorld(pal, boneNames[i], world[i]))
            valid[i] = ProjectPoint(pc, world[i], screen[i]);
    }

    auto drawList = pal::gfx::Overlay();
    auto line = [&](int a, int b) {
        if (valid[a] && valid[b])
            pal::gfx::Line(pal::gfx::Vec2(screen[a].X, screen[a].Y),
                              pal::gfx::Vec2(screen[b].X, screen[b].Y), color, 1.5f);
    };

    // 躯干
    line(0, 1); line(1, 2); line(2, 3);
    // 左臂
    line(2, 4); line(4, 5); line(5, 6);
    // 右臂
    line(2, 7); line(7, 8); line(8, 9);
    // 左腿
    line(3, 10); line(10, 11); line(11, 12);
    // 右腿
    line(3, 13); line(13, 14); line(14, 15);
    // 关节节点
    for (int i = 0; i < nBones; ++i)
        if (valid[i])
            pal::gfx::CircleFilled(pal::gfx::Vec2(screen[i].X, screen[i].Y), 2.5f, color);
}

// ============================================================================
// 真·热成像绘制
// ============================================================================

// 经典热成像调色板 (铁红 / Ironbow): 低->高
//   0.0 黑  0.17 蓝  0.34 青  0.51 绿  0.68 黄  0.85 橙红  1.0 白
// 返回 RGB(0-255)。
static void ThermalLUT(float t, int& r, int& g, int& b)
{
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    // 分段线性采样 7 个关键色
    const float stops = 6.f;
    float seg = t * stops;
    int i = (int)seg;
    if (i < 0) i = 0;
    if (i > 6) i = 6;
    float f = seg - (float)i;
    if (f < 0.f) f = 0.f;
    if (f > 1.f) f = 1.f;

    static const int kLUT[7][3] = {
        { 0,   0,   0  },   // black
        { 0,   0,   160},   // blue
        { 0,   220, 220},   // cyan
        { 0,   220, 0  },   // green
        { 220, 220, 0  },   // yellow
        { 255, 60,  0  },   // orange/red
        { 255, 255, 255}    // white
    };

    int j = i + 1; if (j > 6) j = 6;
    auto lerp = [&](int a, int c) -> int {
        return (int)(kLUT[i][a] + (kLUT[j][a] - kLUT[i][a]) * f);
    };
    r = lerp(0, 0); g = lerp(1, 1); b = lerp(2, 2);
}

// 热成像颜色: 由归一化"热度"(0-1) 决定。alpha 可调。
static pal::gfx::Color ThermalHeatColor(float heat, int alpha = 230)
{
    int r, g, b;
    ThermalLUT(heat, r, g, b);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    return pal::gfx::Color{r / 255.0f, g / 255.0f, b / 255.0f, alpha / 255.0f};
}

// 画一个"热源光斑": 以 screenPos 为中心, 由内向外做多层同心圆,
// 颜色从热核(白/亮) 渐变到冷边缘, alpha 随半径衰减, 形成径向热源光晕。
// size 为该目标的屏幕包围盒直径(px), 光斑半径随距离缩放。
static void DrawHeatBlob(gfx::ShadowDrawList* dl, const gfx::Vec2& center, float size,
                         float heat, int coreAlpha = 220)
{
    if (!dl || size <= 1.f) return;
    float baseR = size * 0.5f;
    if (baseR < 2.f) baseR = 2.f;
    if (baseR > 60.f) baseR = 60.f;

    // 由内向外: 4 层, 每层颜色更冷、更透明、更大
    const int layers = 4;
    float tInner = 0.25f;         // 中心过热偏移 -> 让核心更白/更亮
    for (int li = layers - 1; li >= 0; --li)
    {
        float n = (float)li / (float)(layers - 1);           // 0=边缘 .. 1=核心
        float r = baseR * (0.25f + 0.75f * n);
        float heatT = heat * (0.7f + 0.3f * n) + tInner * (1.f - n);
        if (heatT > 1.f) heatT = 1.f;
        int a = (int)(coreAlpha * (0.12f + 0.28f * n));      // 边缘更透明
        pal::gfx::Color col = ThermalHeatColor(heatT, a);
        pal::gfx::CircleFilled(center, r, col, 24);
    }
}

// 画一条"热迹线": 骨骼连线, 叠加多层半透明线, 模拟热源向外辐射的边缘。
static void DrawHeatLine(gfx::ShadowDrawList* dl, const gfx::Vec2& a, const gfx::Vec2& b,
                         float heat, float thickness, int alpha = 200)
{
    if (!dl) return;
    // 三层: 粗的冷色外圈 + 中等的中间色 + 细的亮核
    int aOuter = (int)(alpha * 0.20f);
    int aMid   = (int)(alpha * 0.45f);
    int aCore  = alpha;
    float coreHeat = heat * 0.9f + 0.1f;
    if (coreHeat > 1.f) coreHeat = 1.f;

    pal::gfx::Line(a, b, ThermalHeatColor(heat * 0.55f, aOuter), thickness * 2.6f);
    pal::gfx::Line(a, b, ThermalHeatColor(heat * 0.78f, aMid),   thickness * 1.6f);
    pal::gfx::Line(a, b, ThermalHeatColor(coreHeat,      aCore), thickness);
}

// 渲染整只帕鲁的热成像身体:
//  - 把全身骨骼投影到屏幕
//  - 每个骨骼点画一个热源光斑(径向渐变), 颜色/大小随距离与朝向变化
//  - 沿骨骼连线画热迹线, 形成覆盖身体的"热源轮廓"(随身体动作变形, 无边框)
//  - 热度 = 血量*0.6 + 朝向相机程度*0.4; 距离越近越亮
static void DrawThermalBody(gfx::ShadowDrawList* dl, SDK::APlayerController* pc,
                            SDK::APalCharacter* pal, float healthFrac,
                            float distance, bool isVisible)
{
    if (!dl || !pc || !pal) return;

    // 优先使用通用骨骼枚举（修复非人形生物热成像显示）
    std::vector<CollectedBone> bones;
    bool useUniversal = CollectSkeletonBonesUniversal(pc, pal, bones);
    int nValid = 0;
    if (useUniversal)
    {
        for (auto& b : bones)
            if (b.valid) ++nValid;
    }

    if (useUniversal && nValid > 0)
    {
        // 热度: 血量越高越"热"; 距离越近越亮; 不可见(被墙挡)时稍微降温但仍可见(X-Ray)
        float heat = healthFrac * 0.65f + 0.25f;
        if (heat > 1.f) heat = 1.f;
        float df = 1.f - (distance - 300.f) / 8000.f;
        if (df < 0.35f) df = 0.35f;
        if (df > 1.0f) df = 1.0f;
        heat *= df;

        // 屏幕包围盒尺寸 -> 光斑基础大小
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (auto& b : bones)
            if (b.valid)
            {
                if (b.screen.X < minX) minX = b.screen.X;
                if (b.screen.X > maxX) maxX = b.screen.X;
                if (b.screen.Y < minY) minY = b.screen.Y;
                if (b.screen.Y > maxY) maxY = b.screen.Y;
            }
        float bodyHeight = (maxY - minY) > 20.f ? (maxY - minY) : 20.f;
        float bodyWidth  = (maxX - minX) > 10.f ? (maxX - minX) : bodyHeight * 0.35f;
        float blobSize = bodyWidth * 0.6f;
        if (blobSize < 6.f) blobSize = 6.f;
        if (blobSize > 34.f) blobSize = 34.f;

        int alphaCore = isVisible ? 230 : 180;

        float lineTh = bodyWidth * 0.16f;
        if (lineTh < 1.5f) lineTh = 1.5f;
        if (lineTh > 6.f) lineTh = 6.f;

        // 骨骼连线 -> 热迹线(身体轮廓)
        for (size_t i = 0; i < bones.size(); ++i)
        {
            if (!bones[i].valid)
                continue;
            int p = bones[i].parentIndex;
            if (p < 0 || p >= (int)bones.size() || !bones[p].valid)
                continue;

            float dx = bones[i].screen.X - bones[p].screen.X;
            float dy = bones[i].screen.Y - bones[p].screen.Y;
            if (dx * dx + dy * dy < 1.0f)
                continue;

            DrawHeatLine(dl,
                pal::gfx::Vec2(bones[i].screen.X, bones[i].screen.Y),
                pal::gfx::Vec2(bones[p].screen.X, bones[p].screen.Y),
                heat, lineTh, alphaCore);
        }

        // 每个关节 -> 热源光斑
        for (size_t i = 0; i < bones.size(); ++i)
        {
            if (!bones[i].valid)
                continue;
            float h = heat;
            DrawHeatBlob(dl, pal::gfx::Vec2(bones[i].screen.X, bones[i].screen.Y), blobSize, h, alphaCore);
        }

        // 轮廓高亮边缘
        for (size_t i = 0; i < bones.size(); ++i)
        {
            if (!bones[i].valid)
                continue;
            int p = bones[i].parentIndex;
            if (p < 0 || p >= (int)bones.size() || !bones[p].valid)
                continue;

            float dx = bones[i].screen.X - bones[p].screen.X;
            float dy = bones[i].screen.Y - bones[p].screen.Y;
            if (dx * dx + dy * dy < 1.0f)
                continue;

            pal::gfx::Line(
                pal::gfx::Vec2(bones[i].screen.X, bones[i].screen.Y),
                pal::gfx::Vec2(bones[p].screen.X, bones[p].screen.Y),
                ThermalHeatColor((heat + 0.2f > 1.f) ? 1.f : heat + 0.2f, 90),
                lineTh * 3.2f);
        }
        return; // 通用方案成功，不再走旧路径
    }

    // 回退：旧版硬编码人形骨骼名（仅在通用枚举失败时使用）
    const char* legacyBones[] = {
        "head", "neck_01", "spine_01", "pelvis",
        "upperarm_l", "lowerarm_l", "hand_l",
        "upperarm_r", "lowerarm_r", "hand_r",
        "thigh_l", "calf_l", "foot_l",
        "thigh_r", "calf_r", "foot_r"
    };
    const int nBones = sizeof(legacyBones) / sizeof(legacyBones[0]);

    SDK::FVector2D screen[16];
    bool valid[16] = { false };
    int nValidOld = 0;
    for (int i = 0; i < nBones; ++i)
    {
        SDK::FVector w;
        if (GetBoneWorld(pal, legacyBones[i], w))
        {
            if (ProjectPoint(pc, w, screen[i]))
            {
                valid[i] = true;
                ++nValidOld;
            }
        }
    }
    if (nValidOld == 0)
        return;

    // 热度: 血量越高越"热"; 距离越近越亮; 不可见(被墙挡)时稍微降温但仍可见(X-Ray)
    float heat = healthFrac * 0.65f + 0.25f;
    if (heat > 1.f) heat = 1.f;
    float df = 1.f - (distance - 300.f) / 8000.f;
    if (df < 0.35f) df = 0.35f;
    if (df > 1.0f) df = 1.0f;
    float distanceFactor = df;
    heat *= distanceFactor;

    // 屏幕包围盒尺寸 -> 光斑基础大小
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < nBones; ++i)
        if (valid[i])
        {
            if (screen[i].X < minX) minX = screen[i].X;
            if (screen[i].X > maxX) maxX = screen[i].X;
            if (screen[i].Y < minY) minY = screen[i].Y;
            if (screen[i].Y > maxY) maxY = screen[i].Y;
        }
    float bodyHeight = (maxY - minY) > 20.f ? (maxY - minY) : 20.f;
    float bodyWidth  = (maxX - minX) > 10.f ? (maxX - minX) : bodyHeight * 0.35f;
    float blobSize = bodyWidth * 0.6f;
    if (blobSize < 6.f) blobSize = 6.f;
    if (blobSize > 34.f) blobSize = 34.f;

    int alphaCore = isVisible ? 230 : 180;

    // 骨骼连线 -> 热迹线(身体轮廓)
    const int conn[][2] = {
        {0,1},{1,2},{2,3},                 // 躯干
        {2,4},{4,5},{5,6},                 // 左臂
        {2,7},{7,8},{8,9},                 // 右臂
        {3,10},{10,11},{11,12},            // 左腿
        {3,13},{13,14},{14,15}             // 右腿
    };
    const int nConn = sizeof(conn) / sizeof(conn[0]);
    float lineTh = bodyWidth * 0.16f;
    if (lineTh < 1.5f) lineTh = 1.5f;
    if (lineTh > 6.f) lineTh = 6.f;
    for (int c = 0; c < nConn; ++c)
    {
        int a = conn[c][0], b = conn[c][1];
        if (valid[a] && valid[b])
            DrawHeatLine(dl, pal::gfx::Vec2(screen[a].X, screen[a].Y),
                         pal::gfx::Vec2(screen[b].X, screen[b].Y),
                         heat, lineTh, alphaCore);
    }

    // 每个关节 -> 热源光斑(径向渐变核心)
    for (int i = 0; i < nBones; ++i)
    {
        if (!valid[i]) continue;
        float h = heat;
        // 头部更"热"(脑部/脸部热源), 手脚边缘略凉
        if (i == 0) h = (h + 0.12f > 1.f) ? 1.f : h + 0.12f;
        else if (i == 6 || i == 9 || i == 12 || i == 15) h = h * 0.8f;
        DrawHeatBlob(dl, pal::gfx::Vec2(screen[i].X, screen[i].Y), blobSize, h, alphaCore);
    }

    // 轮廓高亮边缘: 用最粗的外圈描一遍身体连线, 形成"发光边缘"(穿墙时依然清晰)
    for (int c = 0; c < nConn; ++c)
    {
        int a = conn[c][0], b = conn[c][1];
        if (valid[a] && valid[b])
        {
            float cy = (screen[a].Y + screen[b].Y) * 0.5f;
            (void)cy;
            pal::gfx::Line(pal::gfx::Vec2(screen[a].X, screen[a].Y),
                        pal::gfx::Vec2(screen[b].X, screen[b].Y),
                        ThermalHeatColor((heat + 0.2f > 1.f) ? 1.f : heat + 0.2f, 90),
                        lineTh * 3.2f);
        }
    }
}

// 热成像环境覆盖: 只做极淡的冷色去色薄罩, 突出热源目标, 不再压暗画面。
// 修复: 原实现用 alpha=150 的深蓝全屏遮罩 + 暗角, 导致开启后整体变暗;
// 现改为近乎透明的冷色薄罩, 保持画面亮度, 热成像效果完全由角色热源高亮体现。
// 注意: 该覆盖要放在所有角色绘制之前, 否则会盖住热源。
static void DrawThermalOverlay()
{
    auto dl = pal::gfx::Overlay();
    if (!dl) return;
    pal::gfx::Vec2 sz = pal::gfx::ScreenSize();
    if (sz.x <= 0.f || sz.y <= 0.f) return;
    // 热成像底色：仅做极淡的冷色去色处理，不降低整体亮度。
    // 修复：原实现用 alpha=150 的全屏深色矩形把画面整体压暗（用户反馈“整体变暗”），
    // 现改为近乎透明的冷色薄罩，保留亮度，仅靠热源高亮表达热成像效果。
    pal::gfx::RectFilledMinMax(pal::gfx::Vec2(0), sz, pal::gfx::Color{0.031f, 0.063f, 0.125f, 0.071f});
    // 四周轻微冷色淡化（不再叠加暗角压暗）
    const float vig = sz.y * 0.22f;
    for (int i = 0; i < 4; ++i)
    {
        float inset = vig * ((float)i / 3.f);
        float a = (int)(5 * ((float)(i + 1) / 4.f));
        pal::gfx::RectMinMax(pal::gfx::Vec2(inset, inset),
                    pal::gfx::Vec2(sz.x - inset, sz.y - inset),
                    pal::gfx::Color{0.0f, 0.0f, 10 / 255.0f, a / 255.0f});
    }
}

// 热成像调色板（兼容旧的血条调用, 由血量比例映射 白->黄->红）
static pal::gfx::Color ThermalColor(float frac)
{
    int r, g, b;
    ThermalLUT(0.5f + frac * 0.5f, r, g, b);   // 0.5(黄绿)~1.0(白)
    return pal::gfx::Color{r / 255.0f, g / 255.0f, b / 255.0f, 230 / 255.0f};
}

// 把 RGBA(0-1) 颜色数组转成 pal::gfx::Color
static pal::gfx::Color ColorRGBA(const float* c, float alphaMul = 1.0f)
{
    if (!c) return pal::gfx::Color{1.f, 1.f, 1.f, 1.f};
    float a = c[3] * alphaMul;
    if (a < 0.f) a = 0.f;
    if (a > 1.f) a = 1.f;
    return pal::gfx::Color{c[0], c[1], c[2], a};
}

void DrawPalESP()
{
    if (!cheatState.espEnabled || !cheatState.espShowPals)
        return;

    auto drawList = pal::gfx::Overlay();
    if (!drawList)
        return;

    // 热成像模式下: 先把环境整体去色/转冷色调, 再叠加热源, 使目标在冷背景上突出
    if (cheatState.espThermal)
        DrawThermalOverlay();

    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player)
        return;

    // 使用 player->Controller（原始可用版本的方法），而不是 GetPalPlayerController()。
    // GetPalPlayerController() 走的是 pPlayer->GetPalPlayerController()（SDK 函数），
    // 在某些情况下可能返回空指针导致 ESP 无法绘制。
    APlayerController* controller = nullptr;
    if (!Helper::Try([&] { controller = reinterpret_cast<APlayerController*>(player->Controller); }))
        return;
    if (!controller)
        return;

    // Get all Pals: 先用游戏官方的 CharacterImportanceManager（原始版本可用），
    // 如果拿不到再用世界 Actor 遍历。
    SDK::TArray<SDK::APalCharacter*> pals;
    if (!Helper::GetTAllPals(&pals))
    {
        // Fallback: 世界 Actor 遍历
        Helper::GetAllPalsFromWorld(&pals);
    }
    if (pals.Num() == 0)
        return;

    FVector cameraLocation;
    FRotator cameraRotation;
    controller->GetPlayerViewPoint(&cameraLocation, &cameraRotation);

    for (int i = 0; i < pals.Num(); ++i)
    {
        if (!pals.IsValidIndex(i))
            continue;

        SDK::APalCharacter* pal = pals[i];
        if (!pal)
            continue;

        if (pal == player)
            continue;

        // 把每个帕鲁的绘制包在 Try 里：若某个帕鲁已失效（正被 GC/销毁），
        // 访问其成员会崩溃。若不包裹，全局 handler 吞掉异常后会中断整帧绘制，
        // 导致"一个帕鲁都画不出来"。包上后坏帕鲁被跳过，其余正常绘制。
        Helper::Try([&]
        {
            // 先做廉价、安全的判断：有效指针。
            if (!Helper::IsProbablyValidPtr(pal))
                return;

            // 取参数组件（用于绘制血条）。不把它当作过滤条件，避免"误判导致
            // 所有帕鲁都不绘制"的问题。
            UPalCharacterParameterComponent* params = nullptr;
            if (!Helper::Try([&] { params = pal->CharacterParameterComponent; }))
                params = nullptr;
            if (!Helper::IsProbablyValidPtr(params))
                params = nullptr;

            // Get location and distance
            FVector palLocation;
            if (!Helper::Try([&] { palLocation = pal->K2_GetActorLocation(); }))
                return;
            float distance = palLocation.GetDistanceTo(cameraLocation);

            if (distance > cheatState.espDistance)
                return;

            // 过滤：跳过已驯服/基地工作/死亡的帕鲁，减少绘制干扰
            bool baseWorker = false;
            bool isAlive = false;
            bool isTamed = false;
            if (params) {
                if (Helper::SafeCallRet(isAlive, [&] { return params->IsLive(); })) {
                    if (!isAlive)
                        return;
                    if (Helper::SafeCallRet(baseWorker, [&] { return params->IsAssignedToAnyWork(); }))
                        if (baseWorker) return;
                    if (Helper::SafeCallRet(isTamed, [&] { return params->IsOtomo(); }))
                        if (isTamed) return;
                }
            }

            // Project to screen
            FVector2D screenPos;
            if (!controller->ProjectWorldLocationToScreen(palLocation, &screenPos, false))
                return;

            const bool isVisible = Helper::HasCameraLOS_Kismet(controller, player, pal);

            // 计算血量比例 (供血条 / 热能透视使用)
            float currentHealth = 0.0f;
            float maxHealth = 1.0f;
            if (params)
            {
                auto hp = params->GetHP();
                auto maxHp = params->GetMaxHP();
                currentHealth = (hp.Value >= 0) ? static_cast<float>(hp.Value) : 0.0f;
                maxHealth = (maxHp.Value > 0) ? static_cast<float>(maxHp.Value) : 1.0f;
            }
            float healthFrac = maxHealth > 0 ? (currentHealth / maxHealth) : 0.0f;

            // 热能透视: 真·热成像(骨骼热源光斑 + 热迹线轮廓), 非方框。
            // 热源颜色随血量/距离变化, 可穿墙(X-Ray), 身体随动作变形无边框。
            pal::gfx::Color espBaseColor = isVisible ? pal::gfx::Color{0.784f, 1.000f, 0.784f, 1.000f}
                                           : pal::gfx::Color{0.627f, 0.627f, 0.627f, 0.784f};
            if (cheatState.espThermal)
            {
                espBaseColor = ThermalColor(healthFrac);
                DrawThermalBody(pal::gfx::Overlay(), controller,
                                pal, healthFrac, distance, isVisible);
                // 目标名称也用热源色, 保证在热成像下可读
                if (!cheatState.espShowNames)
                    espBaseColor = ThermalHeatColor(0.75f + healthFrac * 0.25f, 255);
            }

            if (cheatState.espShowPalHealth && params)
            {
                float barWidth = 100.0f;
                float barHeight = 6.0f;
                pal::gfx::Vec2 barPos(screenPos.X - barWidth / 2, screenPos.Y + 16); // 16 px below text

                // Background (grey)
                pal::gfx::RectFilledMinMax(
                    barPos, pal::gfx::Vec2(barPos.x + barWidth, barPos.y + barHeight),
                    pal::gfx::Color{0.235f, 0.235f, 0.235f, 0.784f});

                // Foreground (green->red / 热能时为热成像色)
                pal::gfx::Color hpColor = cheatState.espThermal
                    ? ThermalColor(healthFrac)
                    : pal::gfx::Color{(1.0f - healthFrac) * 220 / 255.0f, healthFrac * 220 / 255.0f,
                                      30 / 255.0f, 230 / 255.0f};

                pal::gfx::RectFilledMinMax(
                    barPos, pal::gfx::Vec2(barPos.x + barWidth * healthFrac, barPos.y + barHeight),
                    hpColor);
            }

            // 计算角色脚底/头顶世界坐标(用于 3D 方框与骨骼)
            SDK::FVector footWorld = palLocation;
            SDK::FVector headWorld = palLocation + SDK::FVector(0, 0, 190.0f);

            // 2D 方框 (自适应: 随屏幕高度变化)
            if (cheatState.espBoxes)
            {
                SDK::FVector2D headScreen2d;
                float boxHeight = 100.0f;
                if (ProjectPoint(controller, headWorld, headScreen2d))
                    boxHeight = (headScreen2d.Y - screenPos.Y > 30.0f) ? static_cast<float>(headScreen2d.Y - screenPos.Y) : 30.0f;
                float boxWidth = boxHeight * 0.55f;

                pal::gfx::Vec2 topLeft = pal::gfx::Vec2(screenPos.X - boxWidth / 2.0f, screenPos.Y - boxHeight / 2.0f);
                pal::gfx::Vec2 bottomRight = pal::gfx::Vec2(screenPos.X + boxWidth / 2.0f, screenPos.Y + boxHeight / 2.0f);

                pal::gfx::RectMinMax(
                    topLeft, bottomRight, ColorRGBA(cheatState.espColorBox), 1.5f);
            }

            // 3D 方框
            if (cheatState.espBoxes3D)
            {
                float radius = 45.0f;
                DrawESPBox3D(controller, pal, footWorld, headWorld, radius, ColorRGBA(cheatState.espColorBox3D));
            }

            // 骨骼可视化
            if (cheatState.espSkeleton)
            {
                DrawSkeleton(controller, pal, ColorRGBA(cheatState.espColorSkeleton));
            }


            std::string palName = "Unknown";
            if (pal)
            {
                std::string rawName;
                if (Helper::Try([&] { rawName = pal->GetName(); })) //Valid check
                {
                    if (!rawName.empty())
                    {
                        palName = GetCleanPalName(rawName);
                        // 绘制界面中文名：未勾选"使用英文名称"时，优先用名称映射表
                        if (!cheatState.useEnglishNames)
                        {
                            std::string cn;
                            if (NameMapper::Get().IsLoaded() && NameMapper::Get().GetPalChineseName(palName, cn))
                                palName = cn;
                        }
                    }
                }
            }

            // Show name (热能时使用热成像色, 始终最高深度可穿墙显示)
            DrawESPText(pal::gfx::Vec2(screenPos.X, screenPos.Y),
                palName.c_str(),
                distance,
                cheatState.espShowNames ? ColorRGBA(cheatState.espColorName) : espBaseColor);
        });
    }
}

std::vector<AActor*> cachedRelics;
float lastRelicScan = 0.0f;
const float scanInterval = 5.0f;

void UpdateRelicCache(UWorld* world)
{
    cachedRelics.clear();
    if (!world) return;

    const auto& levels = world->Levels;
    if (levels.Num() == 0)
        return;

    for (int32 i = 0; i < levels.Num(); ++i)
    {
        ULevel* level = levels[i];
        if (!level) continue;

        const auto& actors = level->Actors;
        for (int32 j = 0; j < actors.Num(); ++j)
        {

            AActor* actor = actors[j];
            if (!actor || !actor->Class) continue;

            if (actor->bHidden)
                continue;

            std::string className = actor->Class->GetName();
            if (className == "BP_LevelObject_Relic_C")
            {
                cachedRelics.push_back(actor);
            }
        }
    }
}

void DrawRelicESP()
{
    if (!cheatState.espEnabled || !cheatState.espShowRelics)
        return;

    UWorld* world = UWorld::GetWorld();
    if (!world) return;

    if (world->Levels.Num() == 0) {
        cachedRelics.clear();
        return;
    }

    if (!world->OwningGameInstance)
    {
        cachedRelics.clear();
        return;
    }

    double currentTime = UKismetSystemLibrary::GetGameTimeInSeconds(world);
    if (currentTime - lastRelicScan > scanInterval)
    {
        UpdateRelicCache(world);
        lastRelicScan = currentTime;
    }



    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player)
    {
        cachedRelics.clear();
        return;
    }

    APlayerController* controller = reinterpret_cast<APlayerController*>(player->Controller);
    if (!controller)
    {
        cachedRelics.clear();
        return;
    }

    FVector cameraLoc;
    FRotator cameraRot;
    controller->GetPlayerViewPoint(&cameraLoc, &cameraRot);

    // Remove null actors before using them
    // Cleanup only null pointers
    cachedRelics.erase(
        std::remove_if(
            cachedRelics.begin(),
            cachedRelics.end(),
            [](AActor* actor) {
                return actor == nullptr;
            }
        ),
        cachedRelics.end()
    );


    for (auto it = cachedRelics.begin(); it != cachedRelics.end(); )
    {
        AActor* actor = *it;

        if (!actor || reinterpret_cast<uintptr_t>(actor) < 0x10000)
        {
            it = cachedRelics.erase(it);
            continue;
        }

        // Check if Class pointer is valid
        uintptr_t classPtr = reinterpret_cast<uintptr_t>(actor->Class);
        if (classPtr < 0x10000)
        {
            it = cachedRelics.erase(it);
            continue;
        }

        //trying to get the name safely because it crashes always
        std::string className;
        try
        {
            className = actor->Class->GetName();
        }
        catch (...)
        {
            it = cachedRelics.erase(it);
            continue;
        }

        if (className == "BP_LevelObject_Relic_C" || className.find("PalLevelObjectRelic") != std::string::npos)
        {
            APalLevelObjectObtainable* obtainable = reinterpret_cast<APalLevelObjectObtainable*>(actor);
            if (obtainable && obtainable->bPickedInClient)
            {
                ++it;
                continue;
            }
        }


        FVector relicLoc = actor->K2_GetActorLocation();
        float distance = relicLoc.GetDistanceTo(cameraLoc);

        if (distance <= cheatState.espDistance)
        {
            FVector2D screenPos;
            if (controller->ProjectWorldLocationToScreen(relicLoc, &screenPos, false))
            {
                DrawRelicESPText(pal::gfx::Vec2(screenPos.X, screenPos.Y), "Relic", distance);
            }
        }
        ++it;
    }
}

void DebugNearbyActors(float radius)
{
    uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL); // Only works if injected into game process
    uintptr_t GObjectsAddress = moduleBase + Offsets::GObjects;
    TUObjectArray* GObjects = reinterpret_cast<TUObjectArray*>(GObjectsAddress);
    if (!GObjects) return;

    int found = 0;
    for (int i = 0; i < GObjects->Num(); ++i)
    {
        UObject* obj = GObjects->GetByIndex(i);
        if (!obj) continue;

        std::string objName = obj->GetName();
        std::string className = "Unknown";
        if (obj->Class)
            className = obj->Class->GetName();

        // Search for keywords
        if (
            objName.find("Relic") != std::string::npos ||
            className.find("Relic") != std::string::npos ||
            objName.find("LevelObject") != std::string::npos ||
            className.find("LevelObject") != std::string::npos ||
            objName.find("Obtainable") != std::string::npos ||
            className.find("Obtainable") != std::string::npos ||
            objName.find("Pickup") != std::string::npos ||
            className.find("Pickup") != std::string::npos
            )
        {
            pal::log::Log(pal::log::Level::Debug, "relic",
                          "[RelicDebug] Name: {} | Class: {} | Ptr: {:#x}",
                          objName, className, reinterpret_cast<uintptr_t>(obj));
            ++found;
        }
    }
    pal::log::Log(pal::log::Level::Debug, "relic",
                  "[RelicDebug] Total matching objects: {}", found);
}