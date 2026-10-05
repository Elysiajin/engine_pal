#include <pch.h>
#include "ui/Tabs.hpp"
#include "ui/Widgets.hpp"
#include "features/CheatState.hpp"
#include "features/Database.hpp"
#include "features/PalSpawner.hpp"
#include "engine/GameHelper.hpp"
#include "engine/NameMapper.hpp"

#include <ShadowGui/Shadow.h>

#include <algorithm>
#include <unordered_map>

// ============================================================================
// TabPalSpawner —— 帕鲁生成器。
// 原工程 src/ui/tabs/TabPalSpawner.cpp 的 Shadow-Gui 重写版:
//   搜索/选择帕鲁 -> 编辑生成参数 (等级/强化/个体值/生命/被动/主动技能)
//   -> 生成到队伍或世界。功能逻辑在 features/PalSpawner。
// ============================================================================

namespace pal::ui {

namespace {

namespace fe = pal::features;
using fe::palspawn::PalSpawnOptions;

constexpr float kListButtonW = 66.f;

bool ListRow(std::string_view label, std::string_view id, bool selected) {
    BeginRow();
    RowLabel(selected ? std::format("▶ {}", label) : std::string(label));

    Shadow::g_Ctx.Cursor.x = RowRight() - kListButtonW;
    const bool clicked = Shadow::Button(
        std::format("{}##{}", selected ? "已选" : "选择", id), {kListButtonW, 0.f});
    EndRow();
    return clicked;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 常用主动技能 (EPalWazaID -> 显示名), 与原版列表一致
constexpr std::pair<int, std::string_view> kCommonWazas[] = {
    {10, "HyperBeam(破坏光线)"}, {11, "PowerShot(能量弹)"}, {12, "PowerBall(能量球)"},
    {22, "AirCanon(空气炮)"}, {33, "SelfDestruct(自爆)"}, {39, "RadiantBarrage(光辉连击)"},
    {40, "FireBlast(火焰放射)"}, {41, "Flamethrower(喷火器)"}, {42, "FireBall(火球)"},
    {43, "FlareArrow(火箭)"}, {44, "FireSeed(火种)"}, {46, "FlareTornado(烈焰龙卷)"},
    {47, "Inferno(炼狱)"}, {52, "Eruption(火山爆发)"}, {53, "FlameWall(火墙)"},
    {54, "FlameFunnel(火焰飞弹)"}, {57, "WaterGun(水枪)"}, {58, "WaterWave(水波)"},
    {59, "HydroPump(高压水泵)"}, {60, "WaterBall(水球)"}, {61, "TidalWave(潮汐波)"},
    {62, "AquaJet(水枪突进)"}, {64, "AcidRain(酸雨)"}, {65, "SeaGush(海啸喷发)"},
    {66, "RipTide(回流)"}, {85, "WindCutter(风刃)"}, {86, "GrassTornado(草龙卷)"},
    {87, "SolarBeam(阳光烈焰)"}, {88, "SeedMachinegun(种子机关枪)"}, {89, "SeedMine(种子地雷)"},
    {90, "RootAttack(藤蔓缠绕)"}, {91, "SpecialCutter(飞叶快刀)"}, {92, "CrossWind(交叉风)"},
    {93, "ReflectiveShuriken(回旋手里剑)"}, {94, "HealingTree(治疗之树)"}, {96, "ThunderRain(雷雨)"},
    {97, "ThunderBall(雷电球)"}, {98, "LineThunder(直线雷击)"}, {99, "CrossThunder(交叉雷击)"},
    {100, "ThreeThunder(三重雷击)"}, {101, "ElecWave(电磁波)"}, {102, "Thunderbolt(雷击)"},
    {103, "ThunderFunnel(雷电飞弹)"}, {104, "SpreadPulse(扩散脉冲)"}, {105, "LockonLaser(锁定激光)"},
    {106, "LightningStrike(天雷)"}, {113, "IceMissile(冰弹)"}, {114, "BlizzardLance(暴风雪矛)"},
    {115, "SnowStorm(暴风雪)"}, {116, "IcicleThrow(冰锥)"}, {117, "IceBlade(冰刃)"},
    {121, "SandTornado(沙龙卷)"}, {122, "ThrowRock(投石)"}, {123, "RockLance(岩矛)"},
    {124, "MudShot(泥弹)"}, {125, "StoneShotgun(岩块散弹)"}, {131, "DarkLaser(暗黑激光)"},
    {132, "DarkWave(暗黑波)"}, {133, "ShadowBall(暗影球)"}, {134, "Psychokinesis(念力)"},
    {135, "PoisonShot(毒弹)"}, {136, "GhostFlame(鬼火)"}, {137, "GravityShot(重力弹)"},
    {145, "DragonMeteor(龙星群)"}, {146, "DragonBreath(龙息)"}, {147, "DragonWave(龙波)"},
    {148, "DragonCanon(龙炮)"}, {153, "StardustArrow(星尘箭)"}, {154, "Tremor(地裂)"},
    {155, "FrostBreath(冰霜吐息)"}, {156, "DiamondFall(钻石雨)"}, {157, "BeamSlicer(光束斩击)"},
};

// 简中名兜底表 (名称映射表未命中且游戏本地化不可用时使用)
std::string TranslatePalName(const std::string& palName) {
    static const std::unordered_map<std::string, std::string> kNames = {
        {"Alpaca", "棉悠悠"}, {"AmaterasuWolf", "天照狼"}, {"Anubis", "阿努比斯"},
        {"BadCatgirl", "捣蛋猫"}, {"Baphomet", "波霸牛"}, {"Baphomet_Dark", "黑夜魔蝠"},
        {"Bastet", "瞅什魔"}, {"Bastet_Ice", "瞅什魔·冰"}, {"BerryGoat", "灌木羊"},
        {"BirdDragon", "火箭雀"}, {"BirdDragon_Ice", "寒霜鸟"}, {"Boar", "紫霞鹿"},
        {"CaptainPenguin", "企丸王"}, {"Carbunclo", "灼热犬"}, {"CatBat", "猫蝠怪"},
        {"ChickenPal", "皮皮鸡"}, {"CowPal", "奶牛"}, {"Cattiva", "喵斯特"},
        {"Chillet", "疾旋鼬"}, {"Daedream", "寐魔"}, {"Direhowl", "猎狼"},
        {"Foxparks", "火绒狐"}, {"Galeclaw", "疾风隼"}, {"Grizzbolt", "雷冥鸟"},
        {"Lamball", "毛掸儿"}, {"Lifmunk", "新叶猿"}, {"Mau", "伏特喵"},
        {"Pengullet", "企丸丸"}, {"Pyrin", "燎火鹿"}, {"Relaxaurus", "水灵龙"},
        {"Robinquill", "羽箭射手"}, {"Rushoar", "冲浪鸭"}, {"Sibelyx", "雪绒狐"},
        {"Sparkit", "电棘鼠"}, {"Tanzee", "翠叶鼠"}, {"Univolt", "雷角马"},
        {"Wixen", "焰巫狐"}, {"Woolipop", "绸笠蛾"}, {"Jetragon", "空涡龙"},
        {"Frostallion", "冰帝美露帕"}, {"Necromus", "混沌骑士"}, {"Paladius", "圣光骑士"},
        {"Shadowbeak", "暗巫猫"}, {"Suzaku", "朱雀"}, {"Suzaku_Aqua", "清雀"},
        {"KingAlpaca", "君王美露帕"}, {"Mimog", "米露菲"}, {"NightFox", "暗夜魔蝠"},
    };
    const auto it = kNames.find(palName);
    return it != kNames.end() ? it->second : "帕鲁(" + palName + ")";
}

// 显示名: 勾选"使用英文名称"时用原名, 否则映射表 -> 本地化 -> 兜底表
std::string GetLocalizedPalName(const std::string& palName) {
    if (cheatState.useEnglishNames)
        return palName;

    static std::unordered_map<std::string, std::string> cache;
    if (const auto it = cache.find(palName); it != cache.end())
        return it->second;

    std::string result;
    if (NameMapper::Get().IsLoaded() && NameMapper::Get().GetPalChineseName(palName, result))
        return cache.emplace(palName, result).first->second;

    result = TranslatePalName(palName);

    Helper::Try([&] {
        SDK::UWorld* world = SDK::UWorld::GetWorld();
        if (!world) return;
        const std::string characterId = palName.rfind("Pal_", 0) == 0 ? palName : "Pal_" + palName;
        const SDK::FName id = Helper::StringToFName(characterId);
        SDK::UPalDatabaseCharacterParameter* database = SDK::UPalUtility::GetDatabaseCharacterParameter(world);
        if (!database) return;
        SDK::FText text{};
        database->GetLocalizedCharacterName(id, &text);
        if (text.TextData) {
            const std::string value = text.ToString();
            if (!value.empty()) result = value;
        }
    });

    return cache.emplace(palName, result).first->second;
}

// ---------------------------------------------------------------------------
// 列表行: 多选 (被动/主动技能)
// ---------------------------------------------------------------------------

bool ToggleRow(std::string_view label, std::string_view id, bool selected) {
    BeginRow();
    RowLabel(selected ? std::format("[x] {}", label) : std::format("[ ] {}", label));

    Shadow::g_Ctx.Cursor.x = RowRight() - kListButtonW;
    const bool clicked = Shadow::Button(
        std::format("{}##{}", selected ? "取消" : "选择", id), {kListButtonW, 0.f});
    EndRow();
    return clicked;
}

// ---------------------------------------------------------------------------
// 生成参数编辑
// ---------------------------------------------------------------------------

void DrawSpawnOptions(PalSpawnOptions& opts, std::string& nickname) {
    BeginPanel("基础属性");
    SliderInt("等级", &opts.level, 1, 50);
    SliderInt("强化 (星级)", &opts.rank, 1, 5);

    static const std::vector<std::string> kGenders{"随机", "雄性", "雌性"};
    Combo("性别", &opts.gender, kGenders);
    InputText("昵称 (可选)", nickname);
    EndPanel();

    BeginPanel("强化属性加成 (0-255)");
    SliderInt("生命", &opts.rankHP, 0, 255);
    SliderInt("攻击", &opts.rankAtk, 0, 255);
    SliderInt("防御", &opts.rankDef, 0, 255);
    SliderInt("制作", &opts.rankCraft, 0, 255);
    EndPanel();

    BeginPanel("个体值 / 生命值");
    Switch("自定义个体值 (天赋)", &opts.setTalents);
    if (opts.setTalents) {
        SliderInt("天赋 - 生命", &opts.talentHP, 0, 100);
        SliderInt("天赋 - 攻击", &opts.talentAtk, 0, 100);
        SliderInt("天赋 - 防御", &opts.talentDef, 0, 100);
    }

    Switch("设置精确生命值 (取消则满血)", &opts.setExactHp);
    if (opts.setExactHp)
        InputFloat("生命值", &opts.hpValue);
    EndPanel();

    BeginPanel(std::format("被动技能 (已选 {})", opts.passives.size()));
    static std::string passiveSearch;
    InputText("搜索被动技能", passiveSearch);
    const std::string needle = ToLower(passiveSearch);

    int shown = 0;
    for (const auto& [key, label] : database::PassiveSkillDatabase) {
        if (!needle.empty() && ToLower(label).find(needle) == std::string::npos)
            continue;
        ++shown;

        const bool contains = std::find(opts.passives.begin(), opts.passives.end(), key) != opts.passives.end();
        if (ToggleRow(label, key, contains)) {
            if (contains)
                std::erase(opts.passives, key);
            else
                opts.passives.push_back(key);
        }
    }
    if (shown == 0)
        TextDesc("没有匹配的被动技能。");

    if (ButtonFull("清空被动技能"))
        opts.passives.clear();
    EndPanel();

    BeginPanel(std::format("主动技能 (已选 {})", opts.wazas.size()));
    for (const auto& [wazaId, name] : kCommonWazas) {
        const bool contains = std::find(opts.wazas.begin(), opts.wazas.end(), wazaId) != opts.wazas.end();
        if (ToggleRow(name, std::format("w{}", wazaId), contains)) {
            if (contains)
                std::erase(opts.wazas, wazaId);
            else
                opts.wazas.push_back(wazaId);
        }
    }

    static int customWazaId = 0;
    InputInt("自定义技能 ID", &customWazaId, 1);
    if (ButtonFull("添加自定义技能 ID")) {
        if (customWazaId > 0 &&
            std::find(opts.wazas.begin(), opts.wazas.end(), customWazaId) == opts.wazas.end())
            opts.wazas.push_back(customWazaId);
    }
    if (ButtonFull("清空主动技能"))
        opts.wazas.clear();
    EndPanel();
}

} // namespace

void TabPalSpawner() {
    using namespace pal::ui;

    static std::string search;
    static std::string selectedPalID;
    static PalSpawnOptions opts;
    static std::string nickname;

    BeginPanel("选择帕鲁");
    InputText("搜索帕鲁", search);

    const std::string needle = ToLower(search);
    int shown = 0;
    for (const char* rawName : database::palNames) {
        if (!rawName) continue;
        const std::string palName = rawName;
        const std::string lowercase = ToLower(palName);
        const std::string localized = GetLocalizedPalName(palName);

        // 同时支持按英文 ID 与中文名搜索
        bool matched = needle.empty() || lowercase.find(needle) != std::string::npos;
        if (!matched && !cheatState.useEnglishNames)
            matched = localized.find(search) != std::string::npos;
        if (!matched) continue;

        ++shown;
        if (ListRow(localized, palName, selectedPalID == palName))
            selectedPalID = palName;
    }
    if (shown == 0)
        TextDesc("没有匹配的帕鲁。");
    EndPanel();

    if (selectedPalID.empty()) {
        BeginPanel("生成帕鲁");
        TextDesc("请从上方列表中选择一只帕鲁。");
        EndPanel();
        return;
    }

    BeginPanel("已选帕鲁");
    Text(std::format("{}  (ID: Pal_{})", GetLocalizedPalName(selectedPalID), selectedPalID));
    EndPanel();

    DrawSpawnOptions(opts, nickname);
    opts.nickname = nickname;

    BeginPanel("生成");
    if (ButtonFull("生成帕鲁 (加入队伍)")) {
        opts.addToParty = true;
        fe::palspawn::SpawnPalWithOptions(selectedPalID, opts);
    }
    if (ButtonFull("生成帕鲁 (仅生成到世界)")) {
        opts.addToParty = false;
        fe::palspawn::SpawnPalWithOptions(selectedPalID, opts);
    }
    EndPanel();

    BeginPanel("快速生成");
    if (ButtonFull("1 级普通帕鲁 (满被动/满个体/满强化)")) {
        PalSpawnOptions quick;
        quick.level = 1;
        quick.rank = 5;
        quick.setTalents = true;
        quick.passives = {"PAL_ALLAttack_up3", "Deffence_up3", "CraftSpeed_up3", "PAL_ALLAttack_up2"};
        quick.wazas = {40, 59, 87, 106};
        quick.addToParty = true;
        fe::palspawn::SpawnPalWithOptions(selectedPalID, quick);
    }
    if (ButtonFull("50 级满配帕鲁 (满被动/满个体/满强化)")) {
        PalSpawnOptions quick;
        quick.level = 50;
        quick.rank = 5;
        quick.rankHP = 255; quick.rankAtk = 255; quick.rankDef = 255; quick.rankCraft = 255;
        quick.setTalents = true;
        quick.passives = {"PAL_ALLAttack_up3", "Deffence_up3", "CraftSpeed_up3", "PAL_ALLAttack_up2"};
        quick.wazas = {40, 59, 87, 106};
        quick.addToParty = true;
        fe::palspawn::SpawnPalWithOptions(selectedPalID, quick);
    }
    EndPanel();

    BeginPanel("说明");
    TextDesc("生成帕鲁到你的队伍或世界中。可自定义等级、强化、个体值(天赋)、生命值、被动词条与主动技能。");
    TextDesc("注意: 部分服务器会校验帕鲁数据, 联机时可能失效。");
    EndPanel();
}

} // namespace pal::ui
