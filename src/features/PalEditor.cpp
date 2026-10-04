// features/PalEditor.cpp — 帕鲁编辑器功能层 (C++23 重写版)
//
// 对应原工程 src/ui/cheat/pal_editor.cpp:
//   - 原文件的 ImGui 绘制代码全部移除, 只保留数据读取与写回流程
//   - 原 std::cout 输出改为 pal::log; 原 static 临时编辑状态移交 UI 层
//   - 偏移、调用顺序、OnRep 触发顺序与原版完全一致

#include <pch.h>
#include "PalEditor.hpp"

#include "Database.hpp"
#include "core/Log.hpp"
#include "engine/GameHelper.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_set>

using namespace SDK;
using namespace Helper;

namespace pal::features {

std::vector<APalCharacter*> cachedTamedPals;
std::vector<APalCharacter*> cachedBaseWorkers;

namespace {

// 工作适应性枚举名 (下标 = EPalWorkSuitability)
constexpr std::string_view kSuitabilityNames[] = {
    "None", "Kindling", "Watering", "Planting", "Generate Electricity",
    "Handcraft", "Gathering", "Cooling", "Mining", "Oil Extraction",
    "Medicine", "Cool", "Transporting", "Farming", "Anyone"
};

// 个体参数 -> SaveParameter (返回 nullptr 表示不可用)
UPalIndividualCharacterParameter* GetSaveParameter(APalCharacter* pal) {
    if (!pal || !IsProbablyValidPtr(pal)) return nullptr;
    UPalCharacterParameterComponent* params = nullptr;
    if (!Try([&] { params = pal->CharacterParameterComponent; })) return nullptr;
    if (!params || !IsProbablyValidPtr(params)) return nullptr;
    UPalIndividualCharacterParameter* individual = nullptr;
    if (!Try([&] { individual = params->GetIndividualParameter(); })) return nullptr;
    if (!individual || !IsProbablyValidPtr(individual)) return nullptr;
    return individual;
}

} // namespace

// ---------------------------------------------------------------------------
// 名称/列表
// ---------------------------------------------------------------------------

std::string GetCleanPalName2(const std::string& rawName) {
    size_t start = 0;

    if (rawName.find("BP_") == 0)
        start += 3;

    if (rawName.find("NPC_", start) == start)
        start += 4;

    const size_t end = rawName.find("_C", start);
    std::string coreName = (end != std::string::npos)
        ? rawName.substr(start, end - start)
        : rawName.substr(start);

    while (!coreName.empty() && std::isdigit(static_cast<unsigned char>(coreName.back())))
        coreName.pop_back();

    while (!coreName.empty() && coreName.back() == '_')
        coreName.pop_back();

    return coreName;
}

bool GetAllTamedPals(std::vector<APalCharacter*>& outResult) {
    outResult.clear();

    TArray<APalCharacter*> allPals;
    if (!GetTAllPals(&allPals))
        return false;

    for (int i = 0; i < allPals.Num(); ++i) {
        if (!allPals.IsValidIndex(i)) continue;

        APalCharacter* pal = allPals[i];
        if (!pal) continue;

        const bool isTamed = IsTamed(pal);
        const bool isAlive = IsAlive(pal);
        const bool baseWorker = IsABaseWorker(pal, true);

        if (isTamed && isAlive && !baseWorker)
            outResult.push_back(pal);
    }

    return !outResult.empty();
}

bool GetAllBaseWorkers(std::vector<APalCharacter*>& outResult) {
    outResult.clear();

    TArray<APalCharacter*> allPals;
    if (!GetTAllPals(&allPals))
        return false;

    for (int i = 0; i < allPals.Num(); ++i) {
        if (!allPals.IsValidIndex(i)) continue;

        APalCharacter* pal = allPals[i];
        if (!pal) continue;

        const bool baseWorker = IsABaseWorker(pal, true);
        const bool isAlive = IsAlive(pal);

        if (baseWorker && isAlive)
            outResult.push_back(pal);
    }

    return !outResult.empty();
}

APalCharacter* ResolveSelectedPal(int selectedPalIndex) {
    if (selectedPalIndex < 0) return nullptr;

    if (selectedPalIndex >= kBaseWorkerIndexBase) {
        const int index = selectedPalIndex - kBaseWorkerIndexBase;
        if (index < 0 || index >= static_cast<int>(cachedBaseWorkers.size())) return nullptr;
        return cachedBaseWorkers[static_cast<size_t>(index)];
    }

    if (selectedPalIndex >= static_cast<int>(cachedTamedPals.size())) return nullptr;
    return cachedTamedPals[static_cast<size_t>(selectedPalIndex)];
}

// ---------------------------------------------------------------------------
// 信息读取
// ---------------------------------------------------------------------------

bool ReadPalInfo(int selectedPalIndex, PalInfo& out) {
    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    if (!pal || !IsProbablyValidPtr(pal) || !pal->CharacterParameterComponent) return false;

    UPalCharacterParameterComponent* params = pal->CharacterParameterComponent;
    UPalIndividualCharacterParameter* individual = nullptr;
    if (!Try([&] { individual = params->GetIndividualParameter(); }) || !individual) return false;

    const FPalIndividualCharacterSaveParameter saveData = individual->SaveParameter;

    out.characterId = saveData.CharacterID.ToString();
    out.uniqueNpcId = saveData.UniqueNPCID.ToString();
    out.level      = saveData.Level;
    out.rank       = saveData.Rank;
    out.rankUpExp  = saveData.RankUpExp;
    out.gender     = (saveData.Gender == EPalGenderType::Male) ? "雄性" : "雌性";

    Try([&] { out.hp = static_cast<float>(params->GetHP().Value); });
    Try([&] { out.maxHp = static_cast<float>(params->GetMaxHP().Value); });
    return true;
}

// ---------------------------------------------------------------------------
// 属性编辑
// ---------------------------------------------------------------------------

bool ReadPalStats(int selectedPalIndex, PalStatsEdit& out) {
    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    UPalIndividualCharacterParameter* individual = GetSaveParameter(pal);
    if (!individual) return false;

    const FPalIndividualCharacterSaveParameter& saveData = individual->SaveParameter;
    out.hp     = static_cast<float>(saveData.Hp.Value);
    out.level  = saveData.Level;
    out.rank   = saveData.Rank;
    out.exp    = saveData.Exp;
    out.gender = (saveData.Gender == EPalGenderType::Male) ? 0 : 1;
    return true;
}

bool ApplyPalStats(int selectedPalIndex, const PalStatsEdit& edit) {
    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    if (!pal || !IsProbablyValidPtr(pal)) return false;

    UPalCharacterParameterComponent* params = nullptr;
    if (!Try([&] { params = pal->CharacterParameterComponent; }) || !params) return false;

    UPalIndividualCharacterParameter* individual = nullptr;
    if (!Try([&] { individual = params->GetIndividualParameter(); }) || !individual) return false;

    FPalIndividualCharacterSaveParameter& saveData = individual->SaveParameter;
    saveData.Hp     = FFixedPoint64(static_cast<int64>(edit.hp));
    saveData.Level  = static_cast<uint8>(edit.level);
    saveData.Rank   = static_cast<uint8>(edit.rank);
    saveData.Exp    = edit.exp;
    saveData.Gender = (edit.gender == 0) ? EPalGenderType::Male : EPalGenderType::Female;

    // 触发复制 (与原版一致)
    Try([&] { params->OnRep_IndividualParameter(); });
    Try([&] { individual->OnRep_SaveParameter(); });
    return true;
}

// ---------------------------------------------------------------------------
// 强化加成
// ---------------------------------------------------------------------------

bool ReadPalRanks(int selectedPalIndex, PalRanksEdit& out) {
    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    UPalIndividualCharacterParameter* individual = GetSaveParameter(pal);
    if (!individual) return false;

    const FPalIndividualCharacterSaveParameter& saveData = individual->SaveParameter;
    out.hp      = saveData.Rank_HP;
    out.attack  = saveData.Rank_Attack;
    out.defence = saveData.Rank_Defence;
    out.craft   = saveData.Rank_CraftSpeed;
    return true;
}

bool ApplyPalRanks(int selectedPalIndex, const PalRanksEdit& edit) {
    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    if (!pal || !IsProbablyValidPtr(pal)) return false;

    UPalCharacterParameterComponent* params = nullptr;
    if (!Try([&] { params = pal->CharacterParameterComponent; }) || !params) return false;

    UPalIndividualCharacterParameter* individual = nullptr;
    if (!Try([&] { individual = params->GetIndividualParameter(); }) || !individual) return false;

    FPalIndividualCharacterSaveParameter& saveData = individual->SaveParameter;
    saveData.Rank_HP         = static_cast<uint8>(std::clamp(edit.hp, 0, 255));
    saveData.Rank_Attack     = static_cast<uint8>(std::clamp(edit.attack, 0, 255));
    saveData.Rank_Defence    = static_cast<uint8>(std::clamp(edit.defence, 0, 255));
    saveData.Rank_CraftSpeed = static_cast<uint8>(std::clamp(edit.craft, 0, 255));

    Try([&] { params->OnRep_IndividualParameter(); });
    return true;
}

// ---------------------------------------------------------------------------
// 工作适应性
// ---------------------------------------------------------------------------

bool ReadWorkSuitabilities(int selectedPalIndex, std::vector<WorkSuitabilityEntry>& out) {
    out.clear();
    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    UPalIndividualCharacterParameter* individual = GetSaveParameter(pal);
    if (!individual) return false;

    const FPalIndividualCharacterSaveParameter& saveData = individual->SaveParameter;
    const auto& suitabilities = saveData.CraftSpeeds;
    const auto& extraSuitabilities = saveData.GotWorkSuitabilityAddRankList;

    for (int i = 0; i < suitabilities.Num(); ++i) {
        const auto& info = suitabilities[i];
        const int baseRank = info.Rank;

        int extraRank = 0;
        for (int j = 0; j < extraSuitabilities.Num(); ++j) {
            if (extraSuitabilities[j].WorkSuitability == info.WorkSuitability) {
                extraRank = extraSuitabilities[j].Rank;
                break;
            }
        }

        const int finalRank = baseRank + extraRank;
        if (finalRank <= 0) continue;   // 原版只显示 rank > 0 的条目

        const int enumIndex = static_cast<int>(info.WorkSuitability);
        WorkSuitabilityEntry entry;
        entry.slot      = i;
        entry.name      = (enumIndex >= 0 &&
                           enumIndex < static_cast<int>(std::size(kSuitabilityNames)))
                              ? std::string(kSuitabilityNames[enumIndex])
                              : std::format("Suitability #{}", enumIndex);
        entry.baseRank  = baseRank;
        entry.extraRank = extraRank;
        entry.finalRank = finalRank;
        out.push_back(std::move(entry));
    }
    return true;
}

bool ApplyWorkSuitabilityRank(int selectedPalIndex, int slot, int desiredRank) {
    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    if (!pal || !IsProbablyValidPtr(pal)) return false;

    UPalCharacterParameterComponent* params = nullptr;
    if (!Try([&] { params = pal->CharacterParameterComponent; }) || !params) return false;

    UPalIndividualCharacterParameter* individual = nullptr;
    if (!Try([&] { individual = params->GetIndividualParameter(); }) || !individual) return false;

    FPalIndividualCharacterSaveParameter& saveData = individual->SaveParameter;
    auto& suitabilities = saveData.CraftSpeeds;
    auto& extraSuitabilities = saveData.GotWorkSuitabilityAddRankList;

    if (!suitabilities.IsValidIndex(slot)) return false;

    auto& info = suitabilities[slot];
    const int baseRank = info.Rank;

    if (desiredRank <= baseRank) {
        // 直接改基础等级, 并清掉对应的额外加成条目
        info.Rank = desiredRank;
        for (int j = 0; j < extraSuitabilities.Num(); ++j) {
            if (extraSuitabilities[j].WorkSuitability == info.WorkSuitability) {
                extraSuitabilities.Remove(j);
                break;
            }
        }
    } else {
        // 移除旧条目后按"基础 + 额外"的方式重设
        for (int j = 0; j < extraSuitabilities.Num(); ++j) {
            if (extraSuitabilities[j].WorkSuitability == info.WorkSuitability) {
                extraSuitabilities.Remove(j);
                break;
            }
        }
        Try([&] { individual->SetWorkSuitabilityAddRank(info.WorkSuitability, desiredRank - baseRank); });
    }

    Try([&] { params->OnRep_IndividualParameter(); });
    Try([&] { individual->OnRep_SaveParameter(); });
    return true;
}

// ---------------------------------------------------------------------------
// 饱食度
// ---------------------------------------------------------------------------

bool ReadPalHunger(int selectedPalIndex, float& current, float& maximum) {
    current = 0.f;
    maximum = 0.f;

    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    UPalIndividualCharacterParameter* individual = GetSaveParameter(pal);
    if (!individual) return false;

    Try([&] { maximum = individual->GetMaxFullStomach(); });
    Try([&] { current = individual->GetFullStomach(); });
    return true;
}

bool ApplyPalHunger(int selectedPalIndex, float value) {
    if (value < 0.f) return false;

    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    UPalIndividualCharacterParameter* individual = GetSaveParameter(pal);
    if (!individual) return false;

    return Try([&] { individual->SetFullStomach(value); });
}

// ---------------------------------------------------------------------------
// 被动技能
// ---------------------------------------------------------------------------

bool ReadPalPassives(int selectedPalIndex, std::vector<std::string>& out) {
    out.clear();

    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    UPalIndividualCharacterParameter* individual = GetSaveParameter(pal);
    if (!individual) return false;

    const auto& passiveSkills = individual->SaveParameter.PassiveSkillList;
    out.reserve(static_cast<size_t>(passiveSkills.Num()));
    for (int i = 0; i < passiveSkills.Num(); ++i)
        out.push_back(passiveSkills[i].ToString());
    return true;
}

bool RemovePalPassiveAt(int selectedPalIndex, int passiveIndex) {
    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    UPalIndividualCharacterParameter* individual = GetSaveParameter(pal);
    if (!individual) return false;

    auto& passiveSkills = individual->SaveParameter.PassiveSkillList;
    if (!passiveSkills.IsValidIndex(passiveIndex)) return false;

    Try([&] { passiveSkills.Remove(passiveIndex); });
    return true;
}

bool AddPalPassive(int selectedPalIndex, const std::string& key) {
    if (key.empty()) return false;

    APalCharacter* pal = ResolveSelectedPal(selectedPalIndex);
    UPalIndividualCharacterParameter* individual = GetSaveParameter(pal);
    if (!individual) return false;

    auto& passiveSkills = individual->SaveParameter.PassiveSkillList;
    for (int i = 0; i < passiveSkills.Num(); ++i) {
        if (passiveSkills[i].ToString() == key)
            return false;   // 已存在
    }

    const FName name = StringToFName(key);
    if (name.IsNone()) return false;

    return Try([&] { passiveSkills.Add(name); });
}

void DumpAllPassiveSkills() {
    std::unordered_set<std::string> uniqueSkills;

    for (auto* pal : cachedTamedPals) {
        if (!pal || !pal->CharacterParameterComponent) continue;

        UPalIndividualCharacterParameter* individual = GetSaveParameter(pal);
        if (!individual) continue;

        const auto& passiveSkills = individual->SaveParameter.PassiveSkillList;
        for (int i = 0; i < passiveSkills.Num(); ++i)
            uniqueSkills.insert(passiveSkills[i].ToString());
    }

    log::Info("[PalEditor] 被动技能枚举: {} 条", uniqueSkills.size());
    for (const auto& skill : uniqueSkills)
        log::Log(log::Level::Debug, "paleditor", "  {}", skill);
}

} // namespace pal::features
