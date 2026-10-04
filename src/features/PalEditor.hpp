#pragma once
// features/PalEditor.hpp — 帕鲁编辑器 (数据访问 + 修改操作)
//
// 原工程 src/ui/cheat/pal_editor.cpp 的重构版:
//   - 删除全部 ImGui 面板函数, 只保留"读取数据 / 应用修改"的功能接口
//   - UI 由 ui/tabs/TabPalEditor.cpp 用 Shadow-Gui 重写
//   - 所有 SDK 指针访问走 Helper::Try / Helper::IsProbablyValidPtr
//   - 索引约定与原版一致: 0..N-1 = 已捕获帕鲁; >=10000 = 据点工作帕鲁 (索引 - 10000)

#include <pch.h>

namespace pal::features {

// 菜单每帧刷新的两个列表缓存 (原 cachedTamedPals / cachedBaseWorkers)
extern std::vector<SDK::APalCharacter*> cachedTamedPals;
extern std::vector<SDK::APalCharacter*> cachedBaseWorkers;

constexpr int kBaseWorkerIndexBase = 10000;

std::string GetCleanPalName2(const std::string& rawName);
bool GetAllTamedPals(std::vector<SDK::APalCharacter*>& outResult);
bool GetAllBaseWorkers(std::vector<SDK::APalCharacter*>& outResult);

// 选择索引 -> 帕鲁指针; 索引无效或指针不可用时返回 nullptr
SDK::APalCharacter* ResolveSelectedPal(int selectedPalIndex);

// ---- 帕鲁信息 (只读展示) ----
struct PalInfo {
    std::string characterId;
    std::string uniqueNpcId;
    std::string gender;
    int   level      = 0;
    int   rank       = 0;
    int   rankUpExp  = 0;
    float hp         = 0.f;
    float maxHp      = 0.f;
};
bool ReadPalInfo(int selectedPalIndex, PalInfo& out);

// ---- 属性编辑 (生命/等级/强化等级/经验/性别) ----
struct PalStatsEdit {
    float     hp     = 0.f;
    int       level  = 1;
    int       rank   = 1;
    std::int64_t exp = 0;
    int       gender = 0;   // 0 = 雄性, 1 = 雌性
};
bool ReadPalStats(int selectedPalIndex, PalStatsEdit& out);
bool ApplyPalStats(int selectedPalIndex, const PalStatsEdit& edit);

// ---- 强化加成 (Rank_HP / Attack / Defence / CraftSpeed) ----
struct PalRanksEdit {
    int hp      = 0;
    int attack  = 0;
    int defence = 0;
    int craft   = 0;
};
bool ReadPalRanks(int selectedPalIndex, PalRanksEdit& out);
bool ApplyPalRanks(int selectedPalIndex, const PalRanksEdit& edit);

// ---- 工作适应性 ----
struct WorkSuitabilityEntry {
    int         slot      = 0;   // CraftSpeeds 下标
    std::string name;
    int         baseRank  = 0;
    int         extraRank = 0;
    int         finalRank = 0;
};
bool ReadWorkSuitabilities(int selectedPalIndex, std::vector<WorkSuitabilityEntry>& out);
bool ApplyWorkSuitabilityRank(int selectedPalIndex, int slot, int desiredRank);

// ---- 饱食度 ----
bool ReadPalHunger(int selectedPalIndex, float& current, float& maximum);
bool ApplyPalHunger(int selectedPalIndex, float value);

// ---- 被动技能 ----
bool ReadPalPassives(int selectedPalIndex, std::vector<std::string>& out);
bool RemovePalPassiveAt(int selectedPalIndex, int passiveIndex);
bool AddPalPassive(int selectedPalIndex, const std::string& key);
void DumpAllPassiveSkills();

} // namespace pal::features
