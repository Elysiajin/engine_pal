#pragma once
// features/PalSpawner.hpp — 帕鲁生成 (原 src/ui/cheat/pal_spawner_util.cpp)
//
// 与原版一致: 用 UPalUtility::GetInitializedCharacterSaveParemter_ParamSetup
// 初始化基础数据, 覆盖用户指定的属性, 再 SpawnNewCharacter + (可选) 加入队伍。
// 差异仅为表达方式: std::cout -> pal::log, 全部 SDK 访问走 Helper 边界。

#include <pch.h>

namespace pal::features::palspawn {

// 生成帕鲁时可配置的全部属性 (对应 FPalIndividualCharacterSaveParameter 的可编辑字段)。
// 默认值与游戏默认初始化一致; 未开启的字段使用游戏自动算出的值。
struct PalSpawnOptions {
    int  level     = 1;      // 等级 (1-50)
    int  rank      = 1;      // 强化等级 (星级, 1-5)
    int  rankHP    = 0;      // 生命强化加成 (Rank_HP, 0-255)
    int  rankAtk   = 0;      // 攻击强化加成 (Rank_Attack)
    int  rankDef   = 0;      // 防御强化加成 (Rank_Defence)
    int  rankCraft = 0;      // 制作强化加成 (Rank_CraftSpeed)

    bool setTalents = false; // 是否覆盖个体值(天赋 0-100)
    int  talentHP   = 100;   // 个体值-生命
    int  talentAtk  = 100;   // 个体值-攻击
    int  talentDef  = 100;   // 个体值-防御

    bool  setExactHp = false;// 是否覆盖当前生命值 (否则回满血)
    float hpValue    = 0.f;  // 精确生命值

    int gender = 0;          // 0=随机 1=雄性 2=雌性

    std::string nickname;                  // 昵称 (留空则不设置)
    std::vector<std::string> passives;     // 被动技能 key (PassiveSkillDatabase 的 key)
    std::vector<int>         wazas;        // 主动技能 (EPalWazaID 枚举值)

    bool addToParty = true;  // true: 生成后加入队伍; false: 只生成到玩家面前
};

// 创建一只全新的帕鲁并生成到玩家面前; 返回生成出的 UPalIndividualCharacterHandle (可能为 nullptr)
SDK::UPalIndividualCharacterHandle* SpawnPalWithOptions(const std::string& palID,
                                                        const PalSpawnOptions& opts);

// 兼容旧接口
bool SpawnPalToPlayer(const std::string& palID, int level = 1);
bool SpawnPalWithParams(const std::string& palID, int level, int rank,
                        const std::vector<std::string>& passiveSkills);

// 把一只已存在帕鲁的 PassiveSkillList 批量补上指定被动技能 (已存在的跳过)
bool ApplyPassiveSkillsToPal(SDK::APalCharacter* pal, const std::vector<std::string>& passiveKeys);

} // namespace pal::features::palspawn
