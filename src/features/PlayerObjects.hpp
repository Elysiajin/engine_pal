#pragma once
// features/PlayerObjects.hpp — 人物 / 周围对象 (数据收集 + 对象操作)
//
// 原工程 src/ui/tabs/TabPlayerObjects.cpp 中与 ImGui 无关的部分抽出为功能层:
//   - 周围生物 / 周围世界对象 (物品/矿石/宝箱/蛋) 的收集与分类
//   - 单个对象的拾取、破坏采集、开箱、传送等操作
//   - 玩家经验读写 (人物属性修改用)
// UI 由 ui/tabs/TabPlayerObjects.cpp 用 Shadow-Gui 重写。

#include <pch.h>

namespace pal::features::objects {

// 默认收集半径 (厘米): 周围对象 100m, 帕鲁 500m —— 与原版一致
inline constexpr float kNearbyMaxDist   = 10000.0f;
inline constexpr float kCreatureMaxDist = 50000.0f;

// 周围对象分类 (原 WorldObjectKind)
enum class WorldObjectKind {
    DropItem,
    Ore,
    Chest,
    Egg,
};

// 一条周围世界对象记录
struct NearbyWorldObject {
    SDK::AActor* actor = nullptr;
    float        distance = 0.f;   // 米
    std::string  label;            // 名称 + 距离
    bool         pickupStyleChest = false; // 以可拾取物形式出现的宝箱
};

// ---------------------------------------------------------------------------
// 玩家位置 / 距离
// ---------------------------------------------------------------------------
bool GetPlayerLocation(SDK::FVector& outLoc);
float CalcDistanceToPlayer(SDK::AActor* actor, const SDK::FVector& playerLoc);
std::string GetActorClassName(SDK::AActor* actor);

// ---------------------------------------------------------------------------
// 周围生物
// ---------------------------------------------------------------------------
bool CollectNearbyCreatures(std::vector<SDK::APalCharacter*>& out,
                            float maxRangeCm, bool fullMap);
std::string CreatureDisplayName(SDK::APalCharacter* pal);
int CreatureLevel(SDK::APalCharacter* pal);
float CreatureHealthFrac(SDK::APalCharacter* pal);

// ---------------------------------------------------------------------------
// 周围世界对象
// ---------------------------------------------------------------------------
void CollectNearbyWorldObjects(WorldObjectKind kind, std::vector<NearbyWorldObject>& out,
                               float maxRangeCm, bool fullMap);

// ---------------------------------------------------------------------------
// 对象操作
// ---------------------------------------------------------------------------
void TeleportToActor(SDK::AActor* actor);
void TeleportActorToPlayer(SDK::AActor* actor);
void PickupSingleItem(SDK::AActor* actor);
void DestroySingleOre(SDK::AActor* actor);
void OpenSingleChest(SDK::AActor* actor);
void KillPal(SDK::APalCharacter* pal, SDK::APalCharacter* player);

// ---------------------------------------------------------------------------
// 玩家经验
// ---------------------------------------------------------------------------
std::int64_t GetPlayerExp();
void AddPlayerExp(std::int64_t amount);

} // namespace pal::features::objects
