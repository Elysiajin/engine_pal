#include <pch.h>
#include "MinimapData.hpp"
#include "core/Gfx.hpp"
#include "engine/GameHelper.hpp"

#include <cctype>
#include <cstring>
#include <cmath>

using namespace SDK;

// ============================================================
// 评审修正 ✗-6: GetCleanPalName 在 esp.cpp 中为 static（文件作用域），
// 本模块无法调用。此处定义本地等价辅助函数。
// ============================================================
static std::string CleanPalName(const std::string& rawName)
{
    size_t start = 0;

    // 移除 "BP_" 前缀
    if (rawName.find("BP_") == 0)
        start += 3;

    // 移除 "NPC_" 前缀（可能在 BP_ 之后或直接开头）
    if (rawName.find("NPC_", start) == start)
        start += 4;

    // 查找 UE4/5 后缀 _C
    size_t end = rawName.find("_C", start);
    std::string coreName = (end != std::string::npos)
        ? rawName.substr(start, end - start)
        : rawName.substr(start);

    // 移除尾部数字
    while (!coreName.empty() && std::isdigit(static_cast<unsigned char>(coreName.back())))
        coreName.pop_back();

    // 移除尾部下划线
    while (!coreName.empty() && coreName.back() == '_')
        coreName.pop_back();

    return coreName;
}

namespace Minimap {

// ============================================================
// ScanEntities — 核心数据采集
//
// 说明：
//   - 角色类实体通过 Helper::GetTAllPals() 获取（含帕鲁/NPC/玩家）
//   - 地图物件类实体通过 UWorld::Levels 遍历获取
//
// [评审修正] 全部已应用：
//   ✗-4: 使用 IsA() + static_cast，不使用 dynamic_cast（项目 /GR- 无 RTTI）
//   ✗-6: 文件内定义 CleanPalName 辅助函数替代 esp.cpp 的 static 函数
//   ✗-7: Entity 默认初始化 isAlive = true, label 为空字符串
//   ✗-8: 每个 actor 访问前检查 actor->Class；actor->Class->GetName() 前先 if (!actor->Class)
//   △-1: 距离使用 XY 平面（FVector2D），非 3D GetDistanceTo
//   △-2: 每个 actor 的访问有独立的 Helper::Try 保护
//   △-4: 蛋类匹配排除 "Spawner" 子串（APalMapObjectSpawnerPalEgg）
// ============================================================
void ScanEntities(UWorld* world, float worldRadius, ScanResult& outResult)
{
    outResult.entities.clear();
    outResult.entities.reserve(512);

    // =========================================
    // 1. 获取玩家自身位置和朝向
    // =========================================
    SDK::APalPlayerCharacter* player = Helper::GetPalPlayerCharacter();
    if (!player)
        return;

    if (!Helper::SafeCallRet(outResult.playerPos, [&] { return player->K2_GetActorLocation(); }))
        return;

    // [视图] 雷达旋转基准改为"摄像机视角"而非角色朝向：
    // GetPlayerViewPoint 返回摄像机当前帧的实际朝向（自由视角/瞄准
    // 时随视野旋转，与玩家所见一致）。角色 K2_GetActorRotation 只给
    // 角色面朝方向，自由视角下会与雷达旋转脱节。
    // 获取玩家 Controller（与 esp.cpp 相同的模式，避免 SDK 函数返回空）
    SDK::APlayerController* controller = nullptr;
    if (!Helper::Try([&] { controller = reinterpret_cast<SDK::APlayerController*>(player->Controller); }) ||
        !controller || !Helper::IsProbablyValidPtr(controller))
        return;

    SDK::FVector camLoc{};
    SDK::FRotator camRot{};
    if (!Helper::Try([&] { controller->GetPlayerViewPoint(&camLoc, &camRot); }))
        return;

    // [契约] FRotator::Yaw 是 double，契约要求 cameraYaw 为 double（此处为摄像机 Yaw）
    outResult.cameraYaw = camRot.Yaw;

    const float radiusSq = worldRadius * worldRadius;

    // =========================================
    // 2. 遍历角色类实体（帕鲁/NPC/其他玩家）
    // =========================================
    SDK::TArray<SDK::APalCharacter*> pals;
    if (Helper::GetTAllPals(&pals))
    {
        for (int32 i = 0; i < pals.Num(); ++i)
        {
            if (!pals.IsValidIndex(i))
                continue;

            SDK::APalCharacter* pal = pals[i];

            // [评审修正 △-2] 每个 actor 的访问有独立 Helper::Try 保护
            Helper::Try([&]
            {
                if (!pal || !Helper::IsProbablyValidPtr(pal))
                    return;

                // 跳过自身
                if (pal == player)
                    return;

                // 获取位置
                SDK::FVector pos;
                if (!Helper::SafeCallRet(pos, [&] { return pal->K2_GetActorLocation(); }))
                    return;

                // [评审修正 △-1] XY 平面距离，非 3D GetDistanceTo
                const double dx = pos.X - outResult.playerPos.X;
                const double dy = pos.Y - outResult.playerPos.Y;
                const double dist = std::sqrt(dx * dx + dy * dy);
                if (dist > static_cast<double>(worldRadius))
                    return;

                // 填充 Entity（默认值见 struct 定义）
                Entity e;
                e.worldPos = pos;
                e.distance = static_cast<float>(dist);
                // isAlive 默认为 true，label 默认为 ""，见 struct 定义

                // 分类：使用 IsA（[评审修正 ✗-4] 不用 dynamic_cast）
                // 注意顺序：APalMonsterCharacter 继承自 APalNPC 继承自 APalCharacter，
                // 先检查最具体的类型
                if (pal->IsA(SDK::APalPlayerCharacter::StaticClass()))
                {
                    e.type = EntityType::OtherPlayer;
                }
                else if (pal->IsA(SDK::APalMonsterCharacter::StaticClass()))
                {
                    // 获取参数组件判断驯服/存活状态
                    SDK::UPalCharacterParameterComponent* param = nullptr;
                    if (!Helper::Try([&] { param = pal->CharacterParameterComponent; }))
                        param = nullptr;

                    if (param && Helper::IsProbablyValidPtr(param))
                    {
                        bool isOtomo = false;
                        bool isWorking = false;
                        bool isLive = true;

                        Helper::SafeCallRet(isOtomo,   [&] { return param->IsOtomo(); });
                        Helper::SafeCallRet(isWorking, [&] { return param->IsAssignedToAnyWork(); });
                        Helper::SafeCallRet(isLive,    [&] { return param->IsLive(); });

                        e.isAlive = isLive;
                        e.type = (isOtomo || isWorking)
                            ? EntityType::TamedPal
                            : EntityType::WildPal;
                    }
                    else
                    {
                        // 无参数组件，默认为野生
                        e.type = EntityType::WildPal;
                    }
                }
                else if (pal->IsA(SDK::APalNPC::StaticClass()))
                {
                    e.type = EntityType::NPC;
                }
                else
                {
                    // 不匹配任何已知角色类型，跳过
                    return;
                }

                // 取短名称（标签）
                std::string rawName;
                if (Helper::SafeCallRet(rawName, [&] { return pal->GetName(); }))
                {
                    const std::string cleanName = CleanPalName(rawName);
                    strncpy_s(e.label, cleanName.c_str(), sizeof(e.label) - 1);
                }

                outResult.entities.push_back(e);
            });
        }
    }

    // =========================================
    // 3. 遍历 UWorld::Levels 获取地图物件
    //    （矿石/蛋/宝箱/传送点/翠叶鼠雕像）
    // =========================================
    if (!world || !Helper::IsProbablyValidPtr(world))
    {
        // 无法获取 UWorld，直接记录时间返回
        outResult.scanTime = SDK::UKismetSystemLibrary::GetGameTimeInSeconds(world);
        return;
    }

    {
        const auto& levels = world->Levels;
        for (int32 li = 0; li < levels.Num(); ++li)
        {
            if (!levels.IsValidIndex(li))
                continue;

            SDK::ULevel* level = levels[li];
            if (!level || !Helper::IsProbablyValidPtr(level))
                continue;

            const auto& actors = level->Actors;
            for (int32 ai = 0; ai < actors.Num(); ++ai)
            {
                if (!actors.IsValidIndex(ai))
                    continue;

                SDK::AActor* actor = actors[ai];

                // [评审修正 △-2] 每个 actor 的访问有独立 Helper::Try
                Helper::Try([&]
                {
                    if (!actor || !Helper::IsProbablyValidPtr(actor))
                        return;

                    // [评审修正 ✗-8] 先检查 actor->Class 再访问
                    if (!actor->Class)
                        return;

                    // 获取类名用于匹配
                    std::string clsName;
                    if (!Helper::SafeCallRet(clsName, [&] { return actor->Class->GetName(); }))
                        return;

                    // 获取位置
                    SDK::FVector pos;
                    if (!Helper::SafeCallRet(pos, [&] { return actor->K2_GetActorLocation(); }))
                        return;

                    // [评审修正 △-1] XY 平面距离
                    const double dx = pos.X - outResult.playerPos.X;
                    const double dy = pos.Y - outResult.playerPos.Y;
                    const double dist = std::sqrt(dx * dx + dy * dy);
                    if (dist > static_cast<double>(worldRadius))
                        return;

                    Entity e;
                    e.worldPos = pos;
                    e.distance = static_cast<float>(dist);
                    // isAlive 默认为 true，label 默认为 ""

                    // 分类匹配（按类名子串匹配，因 UE 蓝图类名可能与 SDK 不完全一致）
                    if (clsName.find("WeakPointOre") != std::string::npos)
                    {
                        e.type = EntityType::Ore;
                    }
                    else if (clsName.find("PalEgg") != std::string::npos)
                    {
                        // [评审修正 △-4] 排除 APalMapObjectSpawnerPalEgg（含 "PalEgg" 字符串）
                        if (clsName.find("Spawner") != std::string::npos)
                            return;
                        e.type = EntityType::Egg;
                    }
                    else if (clsName.find("TreasureBox") != std::string::npos)
                    {
                        // [修复] 类名子串会误命中生成器/可拾取物等含 "TreasureBox" 的
                        // 其他类（如 SpawnerTreasureBox、宝箱拾取物）。改用 IsA 精确
                        // 判定：只有真正的 APalMapObjectTreasureBox（及其蓝图子类）
                        // 才算宝箱。生成器继承自 SpawnerSingleBase，不会被匹配。
                        bool isRealChest = false;
                        if (!Helper::Try([&] { isRealChest = actor->IsA(SDK::APalMapObjectTreasureBox::StaticClass()); }))
                            return;
                        if (!isRealChest)
                            return;
                        e.type = EntityType::TreasureBox;
                    }
                    else if (clsName.find("Relic") != std::string::npos)
                    {
                        // [评审修正 ✗-4] 使用 IsA + static_cast 而非 dynamic_cast
                        if (actor->IsA(SDK::APalLevelObjectObtainable::StaticClass()))
                        {
                            SDK::APalLevelObjectObtainable* obj =
                                static_cast<SDK::APalLevelObjectObtainable*>(actor);
                            bool picked = false;
                            if (Helper::SafeCallRet(picked, [&] { return obj->bPickedInClient; }))
                            {
                                if (picked)
                                    return;  // 已拾取，跳过
                            }
                        }
                        e.type = EntityType::Relic;
                    }
                    else if (clsName.find("FastTravel") != std::string::npos ||
                             clsName.find("WarpPoint") != std::string::npos)
                    {
                        e.type = EntityType::FastTravel;
                    }
                    else
                    {
                        // 不匹配任何已知地图物件类型，跳过
                        return;
                    }

                    outResult.entities.push_back(e);
                });
            }
        }
    }

    // 记录采集时间
    outResult.scanTime = SDK::UKismetSystemLibrary::GetGameTimeInSeconds(world);
}

// ============================================================
// GetEntityTypeName — 获取实体类型显示名称
// ============================================================
const char* GetEntityTypeName(EntityType type)
{
    switch (type)
    {
    case EntityType::WildPal:     return "WildPal";
    case EntityType::TamedPal:    return "TamedPal";
    case EntityType::OtherPlayer: return "OtherPlayer";
    case EntityType::NPC:         return "NPC";
    case EntityType::Ore:         return "Ore";
    case EntityType::Egg:         return "Egg";
    case EntityType::TreasureBox: return "TreasureBox";
    case EntityType::Relic:       return "Relic";
    case EntityType::FastTravel:  return "FastTravel";
    default:                      return "Unknown";
    }
}

// ============================================================
// GetEntityTypeColor — 获取实体类型默认颜色
// 颜色值对应设计文档 §4 实体分类表（原 RGBA 常量换算为 0..1 浮点）
// ============================================================
pal::gfx::Color GetEntityTypeColor(EntityType type)
{
    switch (type)
    {
    case EntityType::WildPal:     return {1.000f, 0.267f, 0.267f, 1.000f};  // 红色   (0xFF,0x44,0x44)
    case EntityType::TamedPal:    return {0.267f, 1.000f, 0.267f, 1.000f};  // 绿色   (0x44,0xFF,0x44)
    case EntityType::OtherPlayer: return {0.267f, 0.533f, 1.000f, 1.000f};  // 蓝色   (0x44,0x88,0xFF)
    case EntityType::NPC:         return {1.000f, 0.800f, 0.267f, 1.000f};  // 黄色   (0xFF,0xCC,0x44)
    case EntityType::Ore:         return {0.533f, 0.533f, 0.533f, 1.000f};  // 灰色   (0x88,0x88,0x88)
    case EntityType::Egg:         return {1.000f, 0.533f, 1.000f, 1.000f};  // 粉色   (0xFF,0x88,0xFF)
    case EntityType::TreasureBox: return {1.000f, 0.843f, 0.000f, 1.000f};  // 金色   (0xFF,0xD7,0x00)
    case EntityType::Relic:       return {1.000f, 0.267f, 1.000f, 1.000f};  // 紫色   (0xFF,0x44,0xFF)
    case EntityType::FastTravel:  return {0.000f, 1.000f, 1.000f, 1.000f};  // 青色   (0x00,0xFF,0xFF)
    default:                      return {1.000f, 1.000f, 1.000f, 1.000f};  // 白色
    }
}

}  // namespace Minimap