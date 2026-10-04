#include <pch.h>
#include "ui/Tabs.hpp"
#include "ui/Widgets.hpp"
#include "features/CheatState.hpp"
#include "features/Database.hpp"
#include "features/PalEditor.hpp"
#include "engine/GameHelper.hpp"
#include "engine/NameMapper.hpp"

#include <ShadowGui/Shadow.h>

#include <algorithm>
#include <cctype>

// ============================================================================
// TabPalEditor —— 帕鲁编辑器。
// 原工程 src/ui/tabs/TabPalEditor.cpp 的 Shadow-Gui 重写版:
//   列表/信息/属性/强化/工作适应性/被动技能/饱食度 七个分区,
//   行卡片风格与其余标签页一致; 数据读写全部经 features/PalEditor。
// ============================================================================

namespace pal::ui {

namespace {

namespace fe = pal::features;

constexpr float kListButtonW = 66.f;

// 行: 左侧标签 + 右侧按钮 (列表选择用); 返回按钮是否被点击
bool ListRow(std::string_view label, std::string_view id, bool selected) {
    BeginRow();
    RowLabel(selected ? std::format("▶ {}", label) : std::string(label));

    Shadow::g_Ctx.Cursor.x = RowRight() - kListButtonW - 12.f;
    const bool clicked = Shadow::Button(
        std::format("{}##{}", selected ? "已选" : "选择", id), {kListButtonW, 0.f});
    EndRow();
    return clicked;
}

// 行: 左侧标签 + 右侧按钮 (动作按钮, 自定义文字)
bool ActionRow(std::string_view label, std::string_view action, std::string_view id) {
    BeginRow();
    RowLabel(label);

    Shadow::g_Ctx.Cursor.x = RowRight() - kListButtonW - 12.f;
    const bool clicked = Shadow::Button(std::format("{}##{}", action, id), {kListButtonW, 0.f});
    EndRow();
    return clicked;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ---------------------------------------------------------------------------
// 帕鲁列表 (已捕获 + 据点工作)
// ---------------------------------------------------------------------------

void DrawPalList(int& selectedPalIndex, std::string& selectedPalName) {
    BeginPanel(std::format("已捕获帕鲁 ({})", fe::cachedTamedPals.size()));
    if (fe::cachedTamedPals.empty())
        TextDesc("没有已捕获的帕鲁。");

    for (int i = 0; i < static_cast<int>(fe::cachedTamedPals.size()); ++i) {
        SDK::APalCharacter* pal = fe::cachedTamedPals[static_cast<size_t>(i)];
        if (!pal) continue;

        std::string rawName;
        SDK::int32 level = 0;
        if (!Helper::Try([&] {
                rawName = pal->GetName();
                if (pal->CharacterParameterComponent)
                    if (auto* iv = pal->CharacterParameterComponent->GetIndividualParameter())
                        level = iv->SaveParameter.Level;
            })) continue;

        const std::string name = fe::GetCleanPalName2(rawName);
        const std::string label = std::format("{} [Lv. {}]", name, level);

        if (ListRow(label, std::format("t{}", i), selectedPalIndex == i)) {
            selectedPalIndex = i;
            selectedPalName = name;
        }
    }
    EndPanel();

    BeginPanel(std::format("据点工作帕鲁 ({})", fe::cachedBaseWorkers.size()));
    if (fe::cachedBaseWorkers.empty())
        TextDesc("没有据点工作帕鲁。");

    for (int i = 0; i < static_cast<int>(fe::cachedBaseWorkers.size()); ++i) {
        SDK::APalCharacter* pal = fe::cachedBaseWorkers[static_cast<size_t>(i)];
        if (!pal) continue;

        std::string rawName;
        SDK::int32 level = 0;
        if (!Helper::Try([&] {
                rawName = pal->GetName();
                if (pal->CharacterParameterComponent)
                    if (auto* iv = pal->CharacterParameterComponent->GetIndividualParameter())
                        level = iv->SaveParameter.Level;
            })) continue;

        const std::string name = fe::GetCleanPalName2(rawName);
        const int actualIndex = fe::kBaseWorkerIndexBase + i;
        const std::string label = std::format("{} [Lv. {}]", name, level);

        if (ListRow(label, std::format("b{}", i), selectedPalIndex == actualIndex)) {
            selectedPalIndex = actualIndex;
            selectedPalName = name;
        }
    }
    EndPanel();
}

// ---------------------------------------------------------------------------
// 帕鲁信息
// ---------------------------------------------------------------------------

void DrawPalInfoSection(int selectedPalIndex) {
    BeginPanel("帕鲁信息");

    if (selectedPalIndex < 0) {
        TextDesc("请从上方列表中选择一只帕鲁。");
        EndPanel();
        return;
    }

    fe::PalInfo info;
    if (!fe::ReadPalInfo(selectedPalIndex, info)) {
        TextDesc("帕鲁无效或缺少参数。");
        EndPanel();
        return;
    }

    Text(std::format("已选帕鲁: {}", info.characterId));
    Text(std::format("等级: {}    强化等级: {}", info.level, info.rank));
    Text(std::format("强化所需经验: {}", info.rankUpExp));
    Text(std::format("性别: {}", info.gender));
    Text(std::format("唯一 NPC ID: {}", info.uniqueNpcId));
    Text(std::format("生命值: {:.0f} / {:.0f}", info.hp, info.maxHp));
    ProgressBar(info.maxHp > 0.f ? info.hp / info.maxHp : 0.f);
    EndPanel();
}

// ---------------------------------------------------------------------------
// 属性 / 强化 编辑
// ---------------------------------------------------------------------------

void DrawPalStatsSection(int selectedPalIndex) {
    BeginPanel("属性");

    static fe::PalStatsEdit edit;
    static int lastIndex = -1;
    if (lastIndex != selectedPalIndex) {
        lastIndex = selectedPalIndex;
        if (selectedPalIndex >= 0)
            fe::ReadPalStats(selectedPalIndex, edit);
    }

    if (selectedPalIndex < 0) {
        TextDesc("请从上方列表中选择一只帕鲁。");
        EndPanel();
        return;
    }

    InputFloat("当前生命值", &edit.hp);
    SliderInt("等级", &edit.level, 1, 255);
    SliderInt("强化等级 (技能等级)", &edit.rank, 0, 255);

    int expInt = static_cast<int>(std::clamp<std::int64_t>(edit.exp, 0, 2'000'000'000));
    if (InputInt("经验值", &expInt, 1000))
        edit.exp = expInt;

    Combo("性别", &edit.gender, {"雄性", "雌性"});

    if (ButtonFull("应用属性修改")) {
        if (fe::ApplyPalStats(selectedPalIndex, edit))
            TextDesc("属性已更新。");
        else
            TextDesc("属性更新失败 (帕鲁无效)。");
    }
    EndPanel();
}

void DrawPalRanksSection(int selectedPalIndex) {
    BeginPanel("强化等级 (加成)");

    static fe::PalRanksEdit edit;
    static int lastIndex = -1;
    if (lastIndex != selectedPalIndex) {
        lastIndex = selectedPalIndex;
        if (selectedPalIndex >= 0)
            fe::ReadPalRanks(selectedPalIndex, edit);
    }

    if (selectedPalIndex < 0) {
        TextDesc("请从上方列表中选择一只帕鲁。");
        EndPanel();
        return;
    }

    SliderInt("生命强化", &edit.hp, 0, 255);
    SliderInt("攻击强化", &edit.attack, 0, 255);
    SliderInt("防御强化", &edit.defence, 0, 255);
    SliderInt("制作强化", &edit.craft, 0, 255);

    if (ButtonFull("应用强化修改")) {
        if (fe::ApplyPalRanks(selectedPalIndex, edit))
            TextDesc("强化属性已更新。");
        else
            TextDesc("强化属性更新失败 (帕鲁无效)。");
    }
    EndPanel();
}

// ---------------------------------------------------------------------------
// 工作适应性
// ---------------------------------------------------------------------------

void DrawPalWorkSuitabilitySection(int selectedPalIndex) {
    BeginPanel("工作适应性");

    if (selectedPalIndex < 0) {
        TextDesc("请从上方列表中选择一只帕鲁。");
        EndPanel();
        return;
    }

    std::vector<fe::WorkSuitabilityEntry> entries;
    if (!fe::ReadWorkSuitabilities(selectedPalIndex, entries) || entries.empty()) {
        TextDesc("该帕鲁没有已解锁的工作适应性。");
        EndPanel();
        return;
    }

    for (const auto& entry : entries) {
        int rank = entry.finalRank;
        if (SliderInt(entry.name, &rank, 0, 20))
            fe::ApplyWorkSuitabilityRank(selectedPalIndex, entry.slot, rank);
    }
    TextDesc("数值 = 基础等级 + 额外加成; 修改后立即同步到游戏。");
    EndPanel();
}

// ---------------------------------------------------------------------------
// 被动技能
// ---------------------------------------------------------------------------

void DrawPalPassiveSection(int selectedPalIndex) {
    BeginPanel("被动技能");

    if (selectedPalIndex < 0) {
        TextDesc("请从上方列表中选择一只帕鲁。");
        EndPanel();
        return;
    }

    std::vector<std::string> passives;
    fe::ReadPalPassives(selectedPalIndex, passives);

    if (passives.empty()) {
        TextDesc("尚未分配被动技能。");
    } else {
        for (int i = 0; i < static_cast<int>(passives.size()); ++i) {
            if (ActionRow(passives[static_cast<size_t>(i)], "移除", std::format("rm{}", i))) {
                fe::RemovePalPassiveAt(selectedPalIndex, i);
                break;   // 列表已变化, 本帧不再继续
            }
        }
    }

    static std::string search;
    static std::string selectedKey;
    InputText("搜索技能", search);
    const std::string needle = ToLower(search);

    int shown = 0;
    for (const auto& [key, label] : database::PassiveSkillDatabase) {
        if (!needle.empty() && ToLower(label).find(needle) == std::string::npos)
            continue;
        ++shown;
        if (ListRow(label, key, selectedKey == key))
            selectedKey = key;
    }
    if (shown == 0)
        TextDesc("没有匹配的被动技能。");

    if (ButtonFull("添加所选技能")) {
        if (!selectedKey.empty()) {
            if (fe::AddPalPassive(selectedPalIndex, selectedKey))
                TextDesc(std::format("已添加: {}", selectedKey));
            else
                TextDesc("添加失败 (技能已存在或帕鲁无效)。");
        }
    }
    EndPanel();
}

// ---------------------------------------------------------------------------
// 饱食度
// ---------------------------------------------------------------------------

void DrawPalHungerSection(int selectedPalIndex) {
    BeginPanel("饱食度");

    if (selectedPalIndex < 0) {
        TextDesc("请从上方列表中选择一只帕鲁。");
        EndPanel();
        return;
    }

    float current = 0.f, maximum = 0.f;
    if (!fe::ReadPalHunger(selectedPalIndex, current, maximum)) {
        TextDesc("未找到个体参数。");
        EndPanel();
        return;
    }

    Text(std::format("最大饱食度: {:.0f}", maximum));
    ProgressBar(maximum > 0.f ? current / maximum : 0.f);

    static float edited = -1.f;
    static int lastIndex = -1;
    if (lastIndex != selectedPalIndex || edited < 0.f) {
        lastIndex = selectedPalIndex;
        edited = current;
    }

    Slider("饱食度", &edited, 0.f, maximum > 0.f ? maximum : 10000.f, 1.f);

    if (ButtonFull("应用饱食度")) {
        if (fe::ApplyPalHunger(selectedPalIndex, edited))
            TextDesc("饱食度已更新。");
        else
            TextDesc("饱食度更新失败。");
    }
    EndPanel();
}

} // namespace

void TabPalEditor() {
    using namespace pal::ui;

    // 每帧刷新两只列表 (对应原版 Menu 中 case 3 的调用点)
    fe::cachedTamedPals.clear();
    fe::cachedBaseWorkers.clear();
    fe::GetAllTamedPals(fe::cachedTamedPals);
    fe::GetAllBaseWorkers(fe::cachedBaseWorkers);

    static int selectedPalIndex = -1;
    static std::string selectedPalName;

    DrawPalList(selectedPalIndex, selectedPalName);
    DrawPalInfoSection(selectedPalIndex);
    DrawPalStatsSection(selectedPalIndex);
    DrawPalRanksSection(selectedPalIndex);
    DrawPalWorkSuitabilitySection(selectedPalIndex);
    DrawPalPassiveSection(selectedPalIndex);
    DrawPalHungerSection(selectedPalIndex);

    BeginPanel("调试");
    if (ButtonFull("导出已捕获帕鲁的被动技能到日志"))
        fe::DumpAllPassiveSkills();
    EndPanel();
}

} // namespace pal::ui
