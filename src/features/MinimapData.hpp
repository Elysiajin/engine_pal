#pragma once
#include <pch.h>
#include "core/Gfx.hpp"
#include <vector>
#include <cstdint>

// ============================================================
// 小地图实体结构体 (minimap_data.h)
// 数据模块与渲染模块之间的契约：由 minimap_data 采集，
// 由 minimap_draw 消费。
//
// 说明：
// - 本头文件同时被 minimap_data.cpp（生产者）和
//   minimap_draw.cpp（消费者，T4）包含，任何修改必须同步。
// - cameraYaw 使用 double（SDK::FRotator::Yaw 为 double），
//   取自 APlayerController::GetPlayerViewPoint（摄像机视角）。
// - distance 为 XY 平面距离（厘米），非 3D 距离。
// ============================================================

namespace Minimap {

// 实体类型枚举
enum class EntityType : uint8_t {
    WildPal      = 0,   // 野生帕鲁
    TamedPal     = 1,   // 已驯服帕鲁（Otomo 或基地工作）
    OtherPlayer  = 2,   // 其他玩家
    NPC          = 3,   // NPC（非帕鲁、非玩家）
    Ore          = 4,   // 矿石/矿脉
    Egg          = 5,   // 帕鲁蛋
    TreasureBox  = 6,   // 宝箱
    Relic        = 7,   // 翠叶鼠雕像
    FastTravel   = 8,   // 传送点
    Count        = 9    // 用于数组大小
};

// 单条实体数据（渲染模块只读）
struct Entity {
    SDK::FVector  worldPos;            // 世界坐标
    EntityType    type;                // 实体类型
    float         distance;            // 距玩家距离（XY 平面，厘米）
    bool          isAlive = true;      // 是否存活（仅对角色有效）[评审修正] 默认初始化
    char          label[32] = "";      // [评审修正] 可选的短标签（如帕鲁名），默认空字符串
};

// 小地图数据采集结果
struct ScanResult {
    std::vector<Entity> entities;      // 所有实体
    SDK::FVector        playerPos;     // 玩家世界坐标
    double              cameraYaw;     // 摄像机朝向 Yaw（度）[视图] 雷达旋转基准以摄像机视角为准
    double              scanTime;      // 采集时间（游戏时间）
};

// ============================================================
// 数据采集接口
// ============================================================

// 扫描一次周围所有实体，填充 result。
// 内部使用 Helper::Try/SafeCallRet 保证崩溃安全。
// 参数 worldRadius：雷达探测半径（厘米），范围外的实体被过滤。
void ScanEntities(SDK::UWorld* world, float worldRadius, ScanResult& outResult);

// 获取实体类型的显示名称（用于调试/UI）
const char* GetEntityTypeName(EntityType type);

// 获取实体类型对应的默认颜色 (pal::gfx::Color, 0..1 浮点)
pal::gfx::Color GetEntityTypeColor(EntityType type);

}  // namespace Minimap