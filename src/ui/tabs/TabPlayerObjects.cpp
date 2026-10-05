#include <pch.h>
#include "ui/Tabs.hpp"
#include "ui/Widgets.hpp"
#include "features/CheatState.hpp"
#include "features/Pathfinding.hpp"
#include "features/PalEditor.hpp"
#include "features/PlayerObjects.hpp"
#include "engine/GameHelper.hpp"

#include <ShadowGui/Shadow.h>

#include <algorithm>

// ============================================================================
// TabPlayerObjects —— 人物 / 周围对象。
// 原工程 src/ui/tabs/TabPlayerObjects.cpp 的 Shadow-Gui 重写版:
//   页面切换用 Combo (原 ImGui 子 TabBar), 逻辑与操作完全复用 features 层:
//     - features/PlayerObjects  周围生物 / 物品 / 矿石 / 宝箱 / 蛋 的扫描与操作
//     - features/PalEditor      帕鲁名清洗
//     - CheatState              ChopNearbyTrees / MineNearbyRocks / PickupNearbyItems 等
// ============================================================================

namespace pal::ui {

namespace {

namespace fe = pal::features;
namespace fo = pal::features::objects;

// ---------------------------------------------------------------------------
// 行内右侧按钮组: 从右向左依次摆放, 与行卡片风格一致
// ---------------------------------------------------------------------------
class RightButtons {
public:
    // 内容区已经内缩过 (卡片内边距), 这里不再额外留白, 才能和其他行的控件右缘对齐
    explicit RightButtons(float rightPadding = 0.f) : x_(RowRight() - rightPadding) {}

    bool Add(std::string_view label, std::string_view id, float width = 62.f) {
        if (!first_) Shadow::SameLine();
        first_ = false;
        x_ -= width;
        Shadow::g_Ctx.Cursor.x = x_;
        return Shadow::Button(std::format("{}##{}", label, id), {width, 0.f});
    }

private:
    float x_;
    bool  first_ = true;
};

// 距离文本
std::string FormatDistance(float meters) {
    return meters >= 0.f ? std::format("({:.0f}m)", meters) : std::string("(--)");
}

// 行: 左侧标签 + 右侧按钮 (列表选择用)
bool ListRow(std::string_view label, std::string_view id, bool selected) {
    BeginRow();
    RowLabel(selected ? std::format("▶ {}", label) : std::string(label));

    constexpr float kButtonW = 66.f;
    Shadow::g_Ctx.Cursor.x = RowRight() - kButtonW;
    const bool clicked = Shadow::Button(
        std::format("{}##{}", selected ? "已选" : "选择", id), {kButtonW, 0.f});
    EndRow();
    return clicked;
}

// ---------------------------------------------------------------------------
// 本次会话跟踪的活跃路径 (对应原 g_trackedPathIds)
// ---------------------------------------------------------------------------
std::vector<uint64_t> g_trackedPathIds;

void RefreshTrackedPaths() {
    PathManager& mgr = PathManager::Get();
    for (auto it = g_trackedPathIds.begin(); it != g_trackedPathIds.end(); ) {
        const PathResult* p = mgr.GetPath(*it);
        if (!p || p->status == PathStatus::Invalid || p->status == PathStatus::Cancelled)
            it = g_trackedPathIds.erase(it);
        else
            ++it;
    }
}

void TrackPathId(uint64_t pathId) {
    if (pathId == 0) return;
    RefreshTrackedPaths();
    if (std::find(g_trackedPathIds.begin(), g_trackedPathIds.end(), pathId) == g_trackedPathIds.end())
        g_trackedPathIds.push_back(pathId);
}

uint64_t FindTrackedPathForActor(SDK::AActor* actor) {
    if (!actor || !Helper::IsProbablyValidPtr(actor)) return 0;
    PathManager& mgr = PathManager::Get();
    for (uint64_t id : g_trackedPathIds) {
        const PathResult* p = mgr.GetPath(id);
        if (p && p->targetActor && Helper::IsProbablyValidPtr(p->targetActor) && p->targetActor == actor)
            return id;
    }
    return 0;
}

std::string_view PathStatusLabel(PathStatus status, Shadow::Color& color) {
    switch (status) {
    case PathStatus::Complete:  color = {0.4f, 0.9f, 0.4f, 1.f}; return "就绪";
    case PathStatus::Partial:   color = {1.0f, 0.8f, 0.2f, 1.f}; return "部分可达";
    case PathStatus::Computing: color = {0.8f, 0.8f, 1.0f, 1.f}; return "计算中";
    case PathStatus::Cancelled: color = {0.6f, 0.6f, 0.6f, 1.f}; return "已取消";
    case PathStatus::Stale:     color = {1.0f, 0.6f, 0.2f, 1.f}; return "已过期";
    default:                    color = {1.0f, 0.3f, 0.3f, 1.f}; return "无效";
    }
}

// 对目标发起寻路 (自动打开寻路总开关与可视化)
void StartPathToActor(SDK::AActor* actor, float distanceMeters) {
    cheatState.pathfindingEnabled = true;
    cheatState.pathVisualizationEnabled = true;
    TrackPathId(PathManager::Get().FindPathToNearbyObject(actor, distanceMeters > 0.f ? distanceMeters : 100.f));
}

// ---------------------------------------------------------------------------
// 人物属性修改 (原 DrawPlayerEditor)
// ---------------------------------------------------------------------------
void DrawPlayerEditor() {
    SDK::APalPlayerCharacter* player = Helper::GetPalPlayerCharacter();
    if (!player || !Helper::IsProbablyValidPtr(player)) {
        BeginPanel("本地玩家");
        TextDesc("未找到玩家。");
        EndPanel();
        return;
    }

    SDK::UPalCharacterParameterComponent* params = nullptr;
    if (!Helper::Try([&] { params = player->CharacterParameterComponent; }) ||
        !params || !Helper::IsProbablyValidPtr(params)) {
        BeginPanel("本地玩家");
        TextDesc("玩家参数组件无效。");
        EndPanel();
        return;
    }

    SDK::UPalIndividualCharacterParameter* individual = nullptr;
    Helper::Try([&] { individual = params->GetIndividualParameter(); });

    // ===================== 等级 / 体力 / 生命 / 饱腹 =====================
    BeginPanel("本地玩家");

    {
        int level = cheatState.playerLevel;
        if (SliderInt("等级", &level, 1, 100)) {
            cheatState.playerLevel = level;
            SetPlayerLevel();
        }
    }

    {
        SDK::FFixedPoint64 current{0}, maximum{0};
        Helper::Try([&] { current = params->GetSP(); });
        Helper::Try([&] { maximum = params->GetMaxSP(); });
        const float maxSp = static_cast<float>(maximum.Value);

        Text(std::format("最大体力: {:.0f}", maxSp));
        static float spValue = -1.f;
        if (spValue < 0.f) spValue = static_cast<float>(current.Value);

        Slider("当前体力", &spValue, 0.f, maxSp > 1.f ? maxSp : 10000.f, 1.f);
        if (ValueChanged(spValue, "playerEditSP"))
            Helper::Try([&] { params->SetSP(SDK::FFixedPoint64(static_cast<int64>(spValue))); });
    }

    {
        const float current = static_cast<float>(params->GetHP().Value);
        const float maximum = static_cast<float>(params->GetMaxHP().Value);

        Text(std::format("最大生命值: {:.0f}", maximum));
        ProgressBar(maximum > 0.f ? current / maximum : 0.f);

        static float hpValue = -1.f;
        if (hpValue < 0.f) hpValue = current;

        Slider("当前生命值", &hpValue, 0.f, maximum > 1.f ? maximum : 10000.f, 1.f);
        if (ButtonFull("设置生命值") && hpValue >= 0.f)
            Helper::Try([&] { params->SetHP(SDK::FFixedPoint64(static_cast<int64>(hpValue))); });
    }

    {
        float maxHunger = 0.f, curHunger = 0.f;
        if (individual) {
            Helper::Try([&] { maxHunger = individual->GetMaxFullStomach(); });
            Helper::Try([&] { curHunger = individual->GetFullStomach(); });
        }
        Text(std::format("最大饱腹值: {:.0f}", maxHunger));
        ProgressBar(maxHunger > 0.f ? curHunger / maxHunger : 0.f);

        static float hungerValue = -1.f;
        if (hungerValue < 0.f) hungerValue = curHunger;

        Slider("当前饱腹", &hungerValue, 0.f, maxHunger > 0.f ? maxHunger : 10000.f, 1.f);
        if (ButtonFull("设置饱腹度") && hungerValue >= 0.f && individual)
            Helper::Try([&] { individual->SetFullStomach(hungerValue); });
    }
    EndPanel();

    // ===================== 战斗属性 =====================
    BeginPanel("战斗属性");
    {
        int attack = cheatState.attack;
        if (SliderInt("攻击力", &attack, 1, 5000)) {
            cheatState.attack = attack;
            SetPlayerAttackParam();
        }
        int defence = cheatState.defence;
        if (SliderInt("防御力", &defence, 0, 5000)) {
            cheatState.defence = defence;
            SetPlayerDefenseParam();
        }
    }
    EndPanel();

    // ===================== 移动 / 工作 / 负重 =====================
    BeginPanel("移动 / 工作");
    {
        float speed = cheatState.speedMultiplier;
        Slider("移动速度", &speed, 100.f, 5000.f, 10.f);
        if (ValueChanged(speed, "playerEditSpeed")) {
            cheatState.speedMultiplier = speed;
            SetPlayerSpeed();
        }

        float craft = cheatState.craftingSpeed;
        Slider("工作速度 (倍率)", &craft, 0.1f, 100.f, 0.1f);
        if (ValueChanged(craft, "playerEditCraft")) {
            cheatState.craftingSpeed = craft;
            SetCraftingSpeed();
        }
    }

    {
        SDK::UPalPlayerInventoryData* inventory = Helper::GetInventoryComponent();
        if (inventory && Helper::IsProbablyValidPtr(inventory)) {
            float maxWeight = 0.f, curWeight = 0.f;
            Helper::Try([&] { maxWeight = inventory->MaxInventoryWeight; });
            Helper::Try([&] { curWeight = inventory->PassiveBuffedCurrentWeight; });
            Text(std::format("负重: {:.1f} / {:.1f}", curWeight, maxWeight));
        }

        Slider("最大负重", &cheatState.weight, 0.f, 1000000.f, 100.f);
        if (ButtonFull("设置最大负重"))
            SetPlayerInventoryWeight();
    }
    EndPanel();

    // ===================== 经验 =====================
    BeginPanel("经验");
    {
        const std::int64_t current = fo::GetPlayerExp();
        Text(std::format("当前经验值: {}", current));

        static int expAdd = 1000;
        InputInt("本次添加", &expAdd, 100);
        if (ButtonFull("添加经验") && expAdd > 0)
            fo::AddPlayerExp(expAdd);
        TextDesc("0.7 版本后经验改为按比例递增，建议小步添加。");
    }
    EndPanel();

    // ===================== 属性点 / 强化次数 =====================
    BeginPanel("属性点 / 强化次数");
    {
        int attrPoints = 0;
        int enhanceCount = 0;
        bool haveParams = false;
        Helper::Try([&] {
            if (individual) {
                haveParams = true;
                attrPoints = static_cast<int>(individual->SaveParameter.UnusedStatusPoint);
                enhanceCount = static_cast<int>(individual->SaveParameter.Rank);
            }
        });

        if (!haveParams) {
            TextDesc("属性点 / 强化次数不可用 (个体参数缺失)。");
        } else {
            static int editAttr = 0;
            static int editRank = 0;
            static bool initialized = false;
            if (!initialized) {
                editAttr = attrPoints;
                editRank = enhanceCount;
                initialized = true;
            }

            Text(std::format("当前属性点: {}    强化次数: {}", attrPoints, enhanceCount));
            SliderInt("属性点", &editAttr, 0, 200);
            SliderInt("强化次数", &editRank, 0, 20);

            if (ButtonFull("应用属性点 / 强化次数")) {
                Helper::Try([&] {
                    individual->SaveParameter.UnusedStatusPoint = static_cast<uint16>(editAttr);
                    individual->SaveParameter.Rank = static_cast<uint8>(editRank);
                    params->OnRep_IndividualParameter();
                    individual->OnRep_SaveParameter();
                });
            }
        }
    }
    EndPanel();

    // ===================== 成长强化次数 (StatusPoint) =====================
    BeginPanel("成长强化次数 (属性点 / StatusPoint)");
    TextDesc("通过 SetStatusPoint 修改玩家各项强化等级, 需在游戏内分配属性点后生效。");

    SliderInt("HP 强化次数", &cheatState.spMaxHp, 0, 100);
    SliderInt("体力强化次数", &cheatState.spMaxStamina, 0, 100);
    SliderInt("攻击力强化次数", &cheatState.spAttack, 0, 100);
    SliderInt("防御力强化次数", &cheatState.spDefense, 0, 100);
    SliderInt("工作速度强化次数", &cheatState.spWorkSpeed, 0, 100);
    SliderInt("捕获力强化次数", &cheatState.spCapture, 0, 100);
    SliderInt("负重上限强化次数", &cheatState.spWeight, 0, 100);
    SliderInt("移动速度强化次数", &cheatState.spMoveSpeed, 0, 100);
    SliderInt("游泳能力强化次数", &cheatState.spSwimSpeed, 0, 100);
    SliderInt("跳跃能力强化次数", &cheatState.spJumpPower, 0, 100);
    SliderInt("攀爬能力强化次数", &cheatState.spClimbSpeed, 0, 100);
    SliderInt("滑翔能力强化次数", &cheatState.spGlideSpeed, 0, 100);
    SliderInt("耐饿能力强化次数", &cheatState.spHungerResist, 0, 100);
    SliderInt("食物保存强化次数", &cheatState.spFoodPreserve, 0, 100);
    SliderInt("异常状态抵抗强化次数", &cheatState.spStatusResist, 0, 100);
    SliderInt("帕鲁球追踪强化次数", &cheatState.spSphereTrack, 0, 100);
    SliderInt("虹彩之运强化次数", &cheatState.spFortune, 0, 100);

    if (ButtonFull("应用全部强化次数")) {
        if (!individual) {
            TextDesc("个体参数不可用。");
        } else {
            auto applyStatusPoint = [&](const char* statusName, int value) {
                Helper::Try([&] {
                    const SDK::FName name = Helper::StringToFName(statusName);
                    if (!name.IsNone())
                        individual->SetStatusPoint(name, value);
                });
            };

            // 先给足属性点, 保证强化可被分配
            Helper::Try([&] {
                individual->SaveParameter.UnusedStatusPoint = 9999;
                params->OnRep_IndividualParameter();
                individual->OnRep_SaveParameter();
            });

            applyStatusPoint("AddMaxHP", cheatState.spMaxHp);
            applyStatusPoint("AddMaxSP", cheatState.spMaxStamina);
            applyStatusPoint("AddPower", cheatState.spAttack);
            applyStatusPoint("AddWorkSpeed", cheatState.spWorkSpeed);
            applyStatusPoint("AddCaptureLevel", cheatState.spCapture);
            applyStatusPoint("AddMaxInventoryWeight", cheatState.spWeight);

            Helper::Try([&] {
                params->OnRep_IndividualParameter();
                individual->OnRep_SaveParameter();
            });
        }
    }
    TextDesc("提示: 部分强化次数 (跳跃/攀爬/游泳/滑翔等) 为扩展占位, 实际效果取决于游戏版本。");
    EndPanel();

    BeginPanel("说明");
    TextDesc("等级/攻击/防御/速度/工作速度/体力修改即时生效;");
    TextDesc("HP/饱腹/负重/经验/属性点需点击对应按钮应用。");
    EndPanel();
}

// ---------------------------------------------------------------------------
// 周围帕鲁
// ---------------------------------------------------------------------------
void DrawNearbyPals() {
    static int selectedIndex = -1;
    static float palRangeM = fo::kCreatureMaxDist / 100.f;   // 默认 500 米
    static bool palFullMap = false;

    BeginPanel("周围帕鲁");
    Slider("探测范围 (米)", &palRangeM, 50.f, 3000.f, 10.f);
    Switch("全图探测", &palFullMap);
    if (palFullMap)
        TextDesc("遍历全部已加载帕鲁。");

    static std::vector<SDK::APalCharacter*> creatures;
    static auto lastScan = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastScan).count() >= 500) {
        fo::CollectNearbyCreatures(creatures, palRangeM * 100.f, palFullMap);
        lastScan = now;
    }

    Text(std::format("周围帕鲁: {}", creatures.size()));
    if (creatures.empty()) {
        TextDesc(palFullMap ? "当前没有可编辑的生物。" : "周围没有可编辑的生物。");
        EndPanel();
        return;
    }

    SDK::FVector playerLoc;
    const bool havePlayer = fo::GetPlayerLocation(playerLoc);

    for (int i = 0; i < static_cast<int>(creatures.size()); ++i) {
        SDK::APalCharacter* pal = creatures[static_cast<size_t>(i)];
        if (!pal || !Helper::IsProbablyValidPtr(pal)) continue;

        float dist = -1.f;
        if (havePlayer)
            dist = fo::CalcDistanceToPlayer(pal, playerLoc);

        const std::string label = std::format("{} [Lv {}] {}", fo::CreatureDisplayName(pal),
                                              fo::CreatureLevel(pal), FormatDistance(dist));
        if (ListRow(label, std::format("pal{}", i), selectedIndex == i))
            selectedIndex = i;
    }
    EndPanel();

    // ---- 已选对象详情 ----
    BeginPanel("已选帕鲁");
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(creatures.size())) {
        TextDesc("请从上方列表中选择一个对象。");
        EndPanel();
        return;
    }

    SDK::APalCharacter* pal = creatures[static_cast<size_t>(selectedIndex)];
    SDK::APalPlayerCharacter* player = Helper::GetPalPlayerCharacter();
    if (!pal || !Helper::IsProbablyValidPtr(pal)) {
        TextDesc("对象已失效, 请重新选择。");
        EndPanel();
        return;
    }

    const float dist = (havePlayer ? fo::CalcDistanceToPlayer(pal, playerLoc) : -1.f);
    Text(std::format("对象: {}", fo::CreatureDisplayName(pal)));
    Text(std::format("等级: {}", fo::CreatureLevel(pal)));
    ProgressBar(fo::CreatureHealthFrac(pal));
    if (dist >= 0.f)
        Text(std::format("距离: {:.1f} 米", dist));

    {
        static float editHp = -1.f;
        static int lastSelection = -1;
        if (lastSelection != selectedIndex) {
            lastSelection = selectedIndex;
            editHp = -1.f;   // 切目标后重新读取当前血量
        }

        float currentHp = 0.f;
        Helper::Try([&] {
            if (auto* p = pal->CharacterParameterComponent)
                currentHp = static_cast<float>(p->GetHP().Value);
        });
        if (editHp < 0.f) editHp = currentHp;

        Text(std::format("当前血量: {:.0f}", currentHp));
        Slider("目标血量", &editHp, 0.f, 100000.f, 10.f);
        if (ButtonFull("应用血量修改")) {
            // 同时写组件 HP 与个体参数 SaveParameter.Hp 并触发 OnRep (与帕鲁编辑器同款写法)
            Helper::Try([&] {
                auto* p = pal->CharacterParameterComponent;
                if (!p || !Helper::IsProbablyValidPtr(p)) return;
                p->SetHP(SDK::FFixedPoint64(static_cast<int64>(editHp)));
                if (auto* iv = p->GetIndividualParameter(); iv && Helper::IsProbablyValidPtr(iv)) {
                    iv->SaveParameter.Hp = SDK::FFixedPoint64(static_cast<int64>(editHp));
                    p->OnRep_IndividualParameter();
                    iv->OnRep_SaveParameter();
                } else {
                    p->OnRep_IndividualParameter();
                }
            });
        }
    }

    if (ButtonFull("秒杀该对象"))
        fo::KillPal(pal, player);

    if (ButtonFull("传送该对象到玩家旁"))
        fo::TeleportActorToPlayer(pal);

    if (ButtonFull("传送玩家到该对象旁"))
        fo::TeleportToActor(pal);

    // ---- 寻路 ----
    const uint64_t existing = FindTrackedPathForActor(pal);
    if (existing == 0) {
        if (ButtonFull("寻路到此对象"))
            StartPathToActor(pal, dist);
    } else {
        if (const PathResult* path = PathManager::Get().GetPath(existing)) {
            Shadow::Color statusColor;
            const std::string_view status = PathStatusLabel(path->status, statusColor);
            Text(std::format("路径状态: {}    距离: {:.1f} 米", status, path->lengthCm / 100.f));
        }
        if (ButtonFull("取消寻路")) {
            PathManager::Get().CancelPath(existing);
            RefreshTrackedPaths();
        }
    }
    EndPanel();
}

// ---------------------------------------------------------------------------
// 周围世界对象 (物品 / 矿石 / 宝箱 / 蛋)
// ---------------------------------------------------------------------------

constexpr std::string_view kKindKey(fo::WorldObjectKind kind) {
    switch (kind) {
    case fo::WorldObjectKind::DropItem: return "item";
    case fo::WorldObjectKind::Ore:      return "ore";
    case fo::WorldObjectKind::Chest:    return "chest";
    case fo::WorldObjectKind::Egg:      return "egg";
    }
    return "obj";
}

void DrawNearbyWorldObjects(fo::WorldObjectKind kind) {
    const std::string key(kKindKey(kind));

    static std::array<float, 4>  rangeM{100.f, 100.f, 100.f, 100.f};
    static std::array<bool, 4>   fullMap{false, false, false, false};
    static std::array<std::vector<fo::NearbyWorldObject>, 4> objects;
    static std::array<std::chrono::steady_clock::time_point, 4> lastScan{};

    const int slot = static_cast<int>(kind);
    const bool isItem = (kind == fo::WorldObjectKind::DropItem);
    const bool isOre  = (kind == fo::WorldObjectKind::Ore);
    const bool isChest = (kind == fo::WorldObjectKind::Chest);
    const bool isEgg  = (kind == fo::WorldObjectKind::Egg);

    const std::string_view title =
        isItem ? "周围物品" : isOre ? "周围矿石" : isChest ? "周围宝箱" : "周围帕鲁蛋";

    BeginPanel(title);
    Slider("探测范围 (米)", &rangeM[slot], 50.f, 3000.f, 10.f);
    Switch("全图探测", &fullMap[slot]);

    if (isItem) {
        TextDesc("一键采集复用原功能的批量拾取流程。");
        if (ButtonFull("一键采集全部物品"))
            PickupNearbyItems();
    } else if (isOre) {
        TextDesc("一键破坏采集复用原功能的批量挖矿流程。");
        if (ButtonFull("一键破坏采集全部矿石"))
            MineNearbyRocks();
    }

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastScan[slot]).count() >= 500) {
        fo::CollectNearbyWorldObjects(kind, objects[slot], rangeM[slot] * 100.f, fullMap[slot]);
        lastScan[slot] = now;
    }

    Text(std::format("{}: {}", title, objects[slot].size()));
    if (objects[slot].empty()) {
        TextDesc(fullMap[slot] ? "全图没有可操作对象。" : "周围没有可操作对象。");
        EndPanel();
        return;
    }

    const int limit = static_cast<int>(std::min<size_t>(objects[slot].size(), 200));
    for (int i = 0; i < limit; ++i) {
        auto& object = objects[slot][static_cast<size_t>(i)];
        if (!object.actor || !Helper::IsProbablyValidPtr(object.actor)) continue;

        BeginRow();
        RowLabel(std::format("{} {}", object.label, FormatDistance(object.distance)));

        RightButtons buttons;
        if (isItem) {
            if (buttons.Add("采集", std::format("{}c{}", key, i))) fo::PickupSingleItem(object.actor);
        } else if (isOre) {
            if (buttons.Add("破坏采集", std::format("{}m{}", key, i), 76.f)) fo::DestroySingleOre(object.actor);
        } else if (isChest) {
            if (object.pickupStyleChest) {
                if (buttons.Add("拾取", std::format("{}p{}", key, i))) fo::PickupSingleItem(object.actor);
            } else {
                if (buttons.Add("打开", std::format("{}o{}", key, i))) fo::OpenSingleChest(object.actor);
            }
        } else if (isEgg) {
            if (buttons.Add("拾取", std::format("{}p{}", key, i))) fo::PickupSingleItem(object.actor);
        }

        if (buttons.Add("传送", std::format("{}t{}", key, i)))
            fo::TeleportToActor(object.actor);
        if (buttons.Add("召唤", std::format("{}s{}", key, i)))
            fo::TeleportActorToPlayer(object.actor);

        const uint64_t pathId = FindTrackedPathForActor(object.actor);
        if (buttons.Add(pathId != 0 ? "取消" : "寻路", std::format("{}w{}", key, i))) {
            if (pathId != 0) {
                PathManager::Get().CancelPath(pathId);
                RefreshTrackedPaths();
            } else {
                StartPathToActor(object.actor, object.distance);
            }
        }
        EndRow();
    }
    EndPanel();
}

// ---------------------------------------------------------------------------
// 寻路 (原 DrawPathfindingTab)
// ---------------------------------------------------------------------------
void DrawPathfindingPanel() {
    BeginPanel("寻路");
    Switch("启用寻路系统", &cheatState.pathfindingEnabled);
    Switch("路径可视化", &cheatState.pathVisualizationEnabled);
    TextDesc("在 3D 世界中绘制路径线、路径点、方向箭头和起终点标记。");
    Switch("自动重算 (玩家/目标移动时)", &cheatState.pathAutoRecalc);
    Switch("仅 NavMesh (禁用 A* 回退)", &cheatState.pathNavMeshOnly);

    if (!cheatState.pathfindingEnabled)
        TextDesc("寻路功能未启用。勾选「启用寻路系统」后即可在周围对象列表中发起寻路。");
    EndPanel();

    BeginPanel("路径外观");
    ColorPicker("主路径颜色", cheatState.pathColor);
    ColorPicker("起点颜色", cheatState.pathStartColor);
    ColorPicker("终点颜色", cheatState.pathEndColor);
    ColorPicker("部分路径颜色", cheatState.pathPartialColor);
    Slider("路径线宽 (px)", &cheatState.pathLineThickness, 1.0f, 12.0f, 0.5f);
    Slider("路径点大小 (px)", &cheatState.pathWaypointSize, 0.0f, 20.0f, 0.5f);
    EndPanel();

    BeginPanel("寻路行为");
    Slider("重算阈值 (米)", &cheatState.pathRecalcThreshold, 1.0f, 100.0f, 1.0f);
    SliderInt("最大活跃路径数", &cheatState.pathMaxActive, 1, 50);
    SliderInt("计算间隔 (毫秒)", &cheatState.pathThrottleMs, 10, 5000);
    EndPanel();

    BeginPanel("活跃路径列表");
    RefreshTrackedPaths();

    PathManager& mgr = PathManager::Get();
    Text(std::format("PathManager 活跃路径: {}    本页跟踪: {}",
                     mgr.GetActivePathCount(), g_trackedPathIds.size()));

    if (!g_trackedPathIds.empty() && ButtonFull("取消全部")) {
        mgr.CancelAllPaths();
        RefreshTrackedPaths();
    }

    if (g_trackedPathIds.empty()) {
        TextDesc("没有活跃路径。在周围帕鲁 / 物品 / 矿石 / 宝箱列表中点击「寻路」创建。");
        EndPanel();
        return;
    }

    for (int i = 0; i < static_cast<int>(g_trackedPathIds.size()); ++i) {
        const uint64_t id = g_trackedPathIds[static_cast<size_t>(i)];
        const PathResult* path = mgr.GetPath(id);
        if (!path) {
            g_trackedPathIds.erase(g_trackedPathIds.begin() + i);
            --i;
            continue;
        }

        Shadow::Color statusColor;
        const std::string_view status = PathStatusLabel(path->status, statusColor);

        std::string target = "位置";
        if (path->targetActor && Helper::IsProbablyValidPtr(path->targetActor))
            target = fo::GetActorClassName(path->targetActor);
        if (target.empty()) target = "目标";

        BeginRow();
        RowLabel(std::format("#{} {} [{}] {:.1f}m / {} 点", id, target, status,
                             path->lengthCm / 100.f, path->waypoints.size()));

        RightButtons buttons;
        if (buttons.Add("重算", std::format("pr{}", i)))
            mgr.RecalculatePath(id);
        if (buttons.Add("取消", std::format("pc{}", i))) {
            mgr.CancelPath(id);
            RefreshTrackedPaths();
        }
        EndRow();
    }
    EndPanel();
}

// ---------------------------------------------------------------------------
// 周围对象 (TAB 页: 帕鲁/物品/矿石/宝箱/蛋/寻路, 与原工程 DrawSurroundingObjects 一致)
// ---------------------------------------------------------------------------
void DrawSurroundingObjects() {
    if (Shadow::BeginTabBar("SurroundingTabBar")) {
        if (Shadow::BeginTabItem("周围帕鲁"))      { DrawNearbyPals();                            Shadow::EndTabItem(); }
        if (Shadow::BeginTabItem("周围物品"))      { DrawNearbyWorldObjects(fo::WorldObjectKind::DropItem); Shadow::EndTabItem(); }
        if (Shadow::BeginTabItem("周围矿石"))      { DrawNearbyWorldObjects(fo::WorldObjectKind::Ore);      Shadow::EndTabItem(); }
        if (Shadow::BeginTabItem("周围宝箱"))      { DrawNearbyWorldObjects(fo::WorldObjectKind::Chest);    Shadow::EndTabItem(); }
        if (Shadow::BeginTabItem("周围帕鲁蛋"))    { DrawNearbyWorldObjects(fo::WorldObjectKind::Egg);      Shadow::EndTabItem(); }
        if (Shadow::BeginTabItem("寻路"))          { DrawPathfindingPanel();                      Shadow::EndTabItem(); }
        Shadow::EndTabBar();
    }
}

} // namespace

void TabPlayerObjects() {
    using namespace pal::ui;

    // 顶级 TAB 页 (与原工程 TabPlayerObjectsMain 一致, 上一版误改成了下拉框)
    if (Shadow::BeginTabBar("TabPlayerObjectsMain")) {
        if (Shadow::BeginTabItem("人物属性修改")) { DrawPlayerEditor();       Shadow::EndTabItem(); }
        if (Shadow::BeginTabItem("周围对象"))     { DrawSurroundingObjects(); Shadow::EndTabItem(); }
        Shadow::EndTabBar();
    }
}

} // namespace pal::ui
