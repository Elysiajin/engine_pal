#include <pch.h>
#include "ui/Tabs.hpp"
#include "ui/Widgets.hpp"
#include "features/CheatState.hpp"
#include "features/ItemList.hpp"
#include "features/DynamicItemList.hpp"
#include "features/ItemDumpUtil.hpp"
#include "engine/NameMapper.hpp"
#include "engine/GameHelper.hpp"
#include "engine/ConfigManager.hpp"
#include "core/Log.hpp"

#include <ShadowGui/Shadow.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

// ============================================================================
// TabItemSpawner —— 物品生成器。
// 功能与原版一致: 分类/搜索/选择物品、本地或服务器请求添加、快速列表、
// 批量按 ID 添加、物品导出调试、背包数量修改。
// 视觉上统一为本项目的行卡片风格; 大列表按可见区剔除以保持性能。
// ============================================================================

namespace pal::ui {

namespace {

// ---- 动态物品列表缓存 ----
std::vector<std::string> g_dynamicItems;
std::vector<std::string> g_dynamicCategories;
bool g_usingDynamicList = false;
bool g_autoLoadAttempted = false;

void TryAutoLoad() {
    if (g_autoLoadAttempted) return;
    g_autoLoadAttempted = true;

    if (itemlist::DynamicItemList::Get().LoadFromJson("items.json")) {
        g_dynamicItems = itemlist::DynamicItemList::Get().GetAllItems();
        g_dynamicCategories = itemlist::DynamicItemList::Get().GetCategories();
        g_usingDynamicList = true;
        log::Log(pal::log::Level::Info, "items", "items.json 自动加载 {} 条", g_dynamicItems.size());
    }
}

// ---- 物品条目解析 (原版逻辑, 加一层解析缓存避免每帧重解析) ----
struct ItemEntry {
    std::string id;
    std::string name;
    std::string category;
};

bool LooksLikeItemId(const std::string& value) {
    return value.find('_') != std::string::npos ||
           value.starts_with("Item") || value.starts_with("Weapon") ||
           value.starts_with("Armor") || value.starts_with("Accessory") ||
           value.starts_with("Ammo") || value.starts_with("Food") ||
           value.starts_with("Material") || value.starts_with("Pal");
}

std::string TranslateItemName(std::string name) {
    static constexpr std::pair<std::string_view, std::string_view> kTranslations[] = {
        {"Attack Pendant", "攻击吊坠"}, {"Defense Pendant", "防御吊坠"},
        {"Life Pendant", "生命吊坠"},   {"Pendant Of Diligence", "勤奋吊坠"},
        {"Ring Of Water Resistance", "水属性抗性戒指"}, {"Ring Of Dark Resistance", "暗属性抗性戒指"},
        {"Ring Of Dragon Resistance", "龙属性抗性戒指"}, {"Ring Of Earth Resistance", "地属性抗性戒指"},
        {"Ring Of Flame Resistance", "火属性抗性戒指"}, {"Ring Of Ice Resistance", "冰属性抗性戒指"},
        {"Ring Of Grass Resistance", "草属性抗性戒指"}, {"Ring Of Lightning Resistance", "雷属性抗性戒指"},
        {"Ring Of Resistance", "抗性戒指"}, {"Thermal Underwear", "隔热内衣"},
        {"Heat Resistant Underwear", "耐热内衣"}, {"Air Dash Boots", "空中冲刺靴"},
        {"Double Jump Boots", "二段跳靴"}, {"Triple Jump Boots", "三段跳靴"},
        {"Quad Jump Boots", "四段跳靴"}, {"Anti-Gravity Belt", "反重力腰带"},
        {"Small Expansion Backpack", "小型扩容背包"}, {"Medium Expansion Backpack", "中型扩容背包"},
        {"Large Expansion Backpack", "大型扩容背包"}, {"Giant Expansion Backpack", "巨型扩容背包"},
        {"Fire Element Whistle", "火属性哨子"}, {"Water Element Whistle", "水属性哨子"},
        {"Electric Element Whistle", "雷属性哨子"}, {"Ice Element Whistle", "冰属性哨子"},
        {"Leaf Element Whistle", "草属性哨子"}, {"Neutral Element Whistle", "无属性哨子"},
        {"Growth Bell", "成长铃"}, {"Defense Support Whistle", "防御支援哨子"},
        {"Attack", "攻击"}, {"Defense", "防御"}, {"Health", "生命"},
        {"Weapon", "武器"}, {"Armor", "护甲"}, {"Material", "材料"},
        {"Food", "食物"}, {"Ammo", "弹药"}, {"Potion", "药剂"},
        {"Common", "普通"}, {"Uncommon", "优秀"}, {"Rare", "稀有"}, {"Legendary", "传奇"},
    };

    for (const auto& [english, chinese] : kTranslations) {
        size_t pos = 0;
        while ((pos = name.find(english, pos)) != std::string::npos) {
            name.replace(pos, english.size(), chinese);
            pos += chinese.size();
        }
    }
    return name;
}

// 从游戏本地化数据取物品显示名 (带缓存)
std::string GetGameItemName(const std::string& itemId) {
    static std::unordered_map<std::string, std::string> s_cache;
    if (auto it = s_cache.find(itemId); it != s_cache.end())
        return it->second;

    std::string localizedName;
    Helper::Try([&] {
        SDK::UWorld* world = SDK::UWorld::GetWorld();
        if (!world) return;
        const std::wstring wideId(itemId.begin(), itemId.end());
        const SDK::FName staticItemId = SDK::BasicFilesImpleUtils::StringToName(wideId.c_str());
        SDK::UPalItemIDManager* manager = SDK::UPalUtility::GetItemIDManager(world);
        if (!manager) return;
        if (SDK::UPalStaticItemDataBase* data = manager->GetStaticItemData(staticItemId)) {
            SDK::FName nameMsgId;
            data->GetNameMsgId(&nameMsgId);
            if (!nameMsgId.IsNone()) {
                SDK::FText text = SDK::UPalMasterDataTablesUtility::GetLocalizedText(
                    world, SDK::EPalLocalizeTextCategory::ItemName, nameMsgId);
                if (text.TextData)
                    localizedName = text.ToString();
            }
        }
    });

    s_cache.emplace(itemId, localizedName);
    return localizedName;
}

std::string TranslateItemCategory(const std::string& category) {
    static const std::unordered_map<std::string, std::string> kCategories = {
        {"Accessories", "饰品"}, {"Ammo", "弹药"}, {"Armor", "护甲"},
        {"Blueprints", "蓝图"}, {"Crafting Materials", "制作材料"}, {"Eggs", "蛋"},
        {"Food", "食物"}, {"Hats", "帽子"}, {"Medicine", "药品"},
        {"Money", "货币"}, {"Other", "其他"}, {"Pal Spheres", "帕鲁球"},
        {"Seeds", "种子"}, {"Tools", "工具"}, {"Weapons", "武器"},
    };
    if (auto it = kCategories.find(category); it != kCategories.end()) return it->second;
    return category;
}

ItemEntry ParseEntry(const std::string& raw, const std::string& category) {
    static std::unordered_map<std::string, ItemEntry> s_cache;
    if (auto it = s_cache.find(raw); it != s_cache.end()) return it->second;

    ItemEntry e;
    std::string_view str = raw;
    const size_t sep = str.find(" | ");
    if (sep != std::string_view::npos) {
        const std::string first(str.substr(0, sep));
        const std::string second(str.substr(sep + 3));
        if (LooksLikeItemId(second) && !LooksLikeItemId(first)) {
            e.name = first;
            e.id = second;
        } else {
            e.id = first;
            e.name = second;
        }
    } else {
        e.id = raw;
        e.name = raw;
    }

    const std::string gameName = GetGameItemName(e.id);
    std::string mappedName;
    if (!cheatState.useEnglishNames &&
        NameMapper::Get().IsLoaded() && NameMapper::Get().GetItemChineseName(e.id, mappedName))
        e.name = mappedName;
    else if (!gameName.empty())
        e.name = gameName;
    else
        e.name = TranslateItemName(e.name);

    e.category = category;
    return s_cache.emplace(raw, std::move(e)).first->second;
}

// 当前行的裁剪剔除: 不可见时跳过绘制 (大列表性能保障)
bool RowVisible() {
    const auto& ctx = Shadow::g_Ctx;
    if (!ctx.ClippingEnabled) return true;
    return ctx.Cursor.y + 48.f >= ctx.ClipMin.y && ctx.Cursor.y <= ctx.ClipMax.y;
}

// ---- 背包修改 ----
struct InvSlot {
    std::string id;
    int count = 0;
    SDK::UPalItemSlot* slot = nullptr;
};

void DrawInventoryEditor() {
    BeginPanel("背包修改 (当前物品列表)");

    SDK::APalPlayerState* ps = Helper::GetPalPlayerState();
    if (!ps) {
        TextDesc("未找到玩家状态。");
        EndPanel();
        return;
    }
    SDK::UPalPlayerInventoryData* inv = nullptr;
    if (!Helper::Try([&] { inv = ps->GetInventoryData(); }) || !Helper::IsProbablyValidPtr(inv)) {
        TextDesc("未找到背包数据。");
        EndPanel();
        return;
    }

    static int s_refreshTick = 0;
    static std::vector<InvSlot> s_slots;
    static int s_lastRefreshTick = -1;
    if (ButtonFull("刷新背包列表"))
        ++s_refreshTick;

    if (s_lastRefreshTick != s_refreshTick) {
        s_lastRefreshTick = s_refreshTick;
        s_slots.clear();
        Helper::Try([&] {
            SDK::UPalItemContainerMultiHelper* helper = inv->InventoryMultiHelper;
            if (!helper || !Helper::IsProbablyValidPtr(helper)) return;
            const auto& containers = helper->Containers;
            for (int c = 0; c < containers.Num(); ++c) {
                SDK::UPalItemContainer* container = containers[c];
                if (!container || !Helper::IsProbablyValidPtr(container)) continue;
                const auto& slots = container->ItemSlotArray;
                for (int s = 0; s < slots.Num(); ++s) {
                    SDK::UPalItemSlot* slot = slots[s];
                    if (!slot || !Helper::IsProbablyValidPtr(slot)) continue;
                    bool empty = false;
                    if (!Helper::Try([&] { empty = slot->IsEmpty(); }) || empty) continue;
                    SDK::FName staticId;
                    if (!Helper::Try([&] { staticId = slot->ItemId.StaticId; })) continue;
                    int cnt = 0;
                    if (!Helper::Try([&] { cnt = slot->StackCount; })) continue;
                    s_slots.push_back({staticId.ToString(), cnt, slot});
                }
            }
        });
    }

    if (s_slots.empty()) {
        TextDesc("背包为空或尚未读取。点击上方「刷新背包列表」。");
        EndPanel();
        return;
    }

    static int s_targetCount = 1;
    if (SliderInt("目标数量", &s_targetCount, 1, 9999)) {}

    for (size_t i = 0; i < s_slots.size(); ++i) {
        auto& e = s_slots[i];
        if (!RowVisible()) {
            // 跳过不可见行, 但仍需消费布局高度以保持滚动条稳定
            Shadow::g_Ctx.Cursor.y += 34.f;
            continue;
        }
        BeginRow();
        RowLabel(e.id);
        const float btnW = 64.f;
        Shadow::g_Ctx.Cursor.x = RowRight() - btnW - 12.f;
        if (Shadow::Button(std::format("修改##{}", i), {btnW, 0.f})) {
            Helper::Try([&] {
                e.slot->StackCount = s_targetCount;
                e.slot->OnRep_StackCount();
            });
            e.count = s_targetCount;
            Helper::Try([&] { inv->RequestForceMarkAllDirty_ToServer(true); });
        }
        EndRow();
    }
    TextDesc("提示: 修改后调用 OnRep 实时刷新本地背包显示, 并标记服务器同步。");
    EndPanel();
}

} // namespace

void TabItemSpawner() {
    using namespace pal::ui;
    TryAutoLoad();

    static int selectedCategoryIndex = 0;
    static std::string searchText;
    static std::string selectedItemID;
    static std::string selectedItemName;
    static std::string selectedItemCategory;
    static int spawnCount = 1;
    static bool useServerRequest = false;
    static std::string batchInput;
    static int batchCount = 1;
    static bool refreshRequested = false;

    // ---- 分类列表 ----
    std::vector<std::string> categoryNames;
    if (g_usingDynamicList && !g_dynamicCategories.empty()) {
        categoryNames.reserve(g_dynamicCategories.size() + 1);
        categoryNames.push_back("全部");
        for (const auto& cat : g_dynamicCategories)
            categoryNames.push_back(TranslateItemCategory(cat));
    } else {
        categoryNames.reserve(itemlist::itemCategories.size() + 1);
        categoryNames.push_back("全部");
        for (const auto& [name, _] : itemlist::itemCategories)
            categoryNames.push_back(TranslateItemCategory(name));
    }
    if (selectedCategoryIndex >= static_cast<int>(categoryNames.size()))
        selectedCategoryIndex = 0;

    // ---- 物品条目 ----
    std::vector<ItemEntry> entries;
    if (g_usingDynamicList && !g_dynamicItems.empty()) {
        if (selectedCategoryIndex == 0) {
            for (const auto& cat : g_dynamicCategories)
                for (const auto& raw : itemlist::DynamicItemList::Get().GetItemsByCategory(cat))
                    entries.push_back(ParseEntry(raw, cat));
        } else {
            const auto& catName = g_dynamicCategories[selectedCategoryIndex - 1];
            for (const auto& raw : itemlist::DynamicItemList::Get().GetItemsByCategory(catName))
                entries.push_back(ParseEntry(raw, catName));
        }
    } else {
        if (selectedCategoryIndex == 0) {
            for (const auto& [cat, items] : itemlist::itemCategories)
                for (const char* item : *items)
                    entries.push_back(ParseEntry(item, cat));
        } else {
            auto catName = itemlist::itemCategories.begin();
            std::advance(catName, selectedCategoryIndex - 1);
            for (const char* item : *catName->second)
                entries.push_back(ParseEntry(item, catName->first));
        }
    }

    // ---- 添加物品 ----
    BeginPanel("添加物品");
    Switch("联机服务器请求模式", &useServerRequest);
    TextDesc(useServerRequest
        ? "联机模式: 通过服务器请求添加物品 (持久化, 不会被同步移除)。"
        : "注意: 这些是客户端临时物品, 背包同步或重新登录时服务器可能替换或移除它们。");

    Combo("分类", &selectedCategoryIndex, categoryNames);
    InputText("搜索", searchText);

    std::string searchLower = searchText;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    int visible = 0;
    for (const auto& e : entries) {
        std::string idLower = e.id, nameLower = e.name;
        std::transform(idLower.begin(), idLower.end(), idLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (idLower.find(searchLower) == std::string::npos &&
            nameLower.find(searchLower) == std::string::npos)
            continue;

        if (!RowVisible()) {
            Shadow::g_Ctx.Cursor.y += 34.f; // 保持滚动高度
            continue;
        }
        ++visible;
        BeginRow();
        RowLabel(e.name);
        const float btnW = 64.f;
        Shadow::g_Ctx.Cursor.x = RowRight() - btnW - 12.f;
        if (Shadow::Button(std::format("选择##{}", e.id), {btnW, 0.f})) {
            selectedItemID = e.id;
            selectedItemName = e.name;
            selectedItemCategory = e.category;
        }
        EndRow();
    }
    if (visible == 0)
        TextDesc("没有匹配的物品。");
    EndPanel();

    // ---- 已选物品 ----
    if (!selectedItemID.empty()) {
        BeginPanel("已选物品");
        Text(std::format("{}  [{}]", selectedItemName,
                         TranslateItemCategory(selectedItemCategory)));
        Text(selectedItemID);
        SliderInt("数量", &spawnCount, 1, 9999);
        if (ButtonFull(useServerRequest ? "通过服务器请求添加" : "添加到背包 (本地)")) {
            if (useServerRequest)
                AddItemToInventoryByName_ToServer(selectedItemID, spawnCount);
            else
                AddItemToInventoryByName(selectedItemID, spawnCount);
        }
        if (ButtonFull("添加到快速列表")) {
            bool merged = false;
            for (auto& it : cheatState.bulkItems) {
                if (it.id == selectedItemID) {
                    it.count += spawnCount;
                    merged = true;
                    break;
                }
            }
            if (!merged) cheatState.bulkItems.push_back({selectedItemID, spawnCount});
        }
        EndPanel();
    }

    // ---- 快速列表 ----
    BeginPanel("快速列表 (临时)");
    TextDesc("客户端临时物品不会保存到服务器, 背包同步后可能被移除; 此列表用于重新登录后快速补回常用物品。");
    for (size_t i = 0; i < cheatState.bulkItems.size(); ++i) {
        auto& it = cheatState.bulkItems[i];
        SliderInt(std::format("数量: {}##{}", it.id, i), &it.count, 1, 9999);
        BeginRow();
        RowLabel(it.id);
        const float btnW = 56.f;
        Shadow::g_Ctx.Cursor.x = RowRight() - btnW * 2 - Shadow::GetStyle().ItemSpacing.x - 12.f;
        if (Shadow::Button(std::format("获取##{}", i), {btnW, 0.f})) {
            if (useServerRequest)
                AddItemToInventoryByName_ToServer(it.id, it.count);
            else
                AddItemToInventoryByName(it.id, it.count);
        }
        Shadow::SameLine();
        if (Shadow::Button(std::format("移除##{}", i), {btnW, 0.f})) {
            cheatState.bulkItems.erase(cheatState.bulkItems.begin() + static_cast<ptrdiff_t>(i));
            EndRow();
            break;
        }
        EndRow();
    }
    if (!cheatState.bulkItems.empty() && ButtonFull("获取全部快速列表物品")) {
        for (const auto& it : cheatState.bulkItems) {
            if (useServerRequest)
                AddItemToInventoryByName_ToServer(it.id, it.count);
            else
                AddItemToInventoryByName(it.id, it.count);
        }
        Config::Save("config.json");
    }
    EndPanel();

    // ---- 批量添加 ----
    BeginPanel("批量添加物品 (按 ID)");
    InputText("物品 ID 列表", batchInput);
    TextDesc("每行一个物品 ID, 或用逗号分隔。例如: Item_Wood, Item_Stone, Item_PalSphere");
    SliderInt("批量数量", &batchCount, 1, 9999);
    if (ButtonFull("批量添加")) {
        int added = 0;
        std::istringstream stream(batchInput);
        std::string line;
        while (std::getline(stream, line, '\n')) {
            std::istringstream lineStream(line);
            std::string token;
            while (std::getline(lineStream, token, ',')) {
                while (!token.empty() && (token.front() == ' ' || token.front() == '\r' || token.front() == '\t'))
                    token.erase(token.begin());
                while (!token.empty() && (token.back() == ' ' || token.back() == '\r' || token.back() == '\t'))
                    token.pop_back();
                if (token.empty()) continue;
                if (useServerRequest)
                    AddItemToInventoryByName_ToServer(token, batchCount);
                else
                    AddItemToInventoryByName(token, batchCount);
                ++added;
            }
        }
        log::Log(pal::log::Level::Info, "items", "批量添加完成: {} 个物品 x {}", added, batchCount);
    }
    EndPanel();

    // ---- 调试导出 ----
    BeginPanel("调试: 从游戏导出物品");
    TextDesc("从游戏 SDK 数据中提取所有物品 ID, 保存到 CSV 或控制台输出, 用于更新物品列表。");
    if (ButtonFull("Refresh Item List from Game"))
        refreshRequested = true;
    if (refreshRequested) {
        if (itemlist::DynamicItemList::Get().RefreshFromGame()) {
            g_dynamicItems = itemlist::DynamicItemList::Get().GetAllItems();
            g_dynamicCategories = itemlist::DynamicItemList::Get().GetCategories();
            g_usingDynamicList = true;
            refreshRequested = false;
        }
    }
    Text(g_usingDynamicList
             ? std::format("使用动态列表: {} 条", g_dynamicItems.size())
             : "使用静态列表");
    if (ButtonFull("Dump All Items to File (Items.csv)")) {
        const std::string path = ItemDumpUtil::DumpAllItemsToFile("Items.csv");
        if (!path.empty()) log::Log(pal::log::Level::Info, "items", "物品导出完成: {}", path);
        else log::Error("items: 物品导出失败");
    }
    if (ButtonFull("Dump to Console"))
        ItemDumpUtil::DumpAllItemsToConsole();
    if (g_usingDynamicList && ButtonFull("Save Current List to JSON")) {
        if (itemlist::DynamicItemList::Get().SaveToJson("items.json"))
            log::Log(pal::log::Level::Info, "items", "已保存到 items.json");
    }
    EndPanel();

    // ---- 背包修改 ----
    DrawInventoryEditor();
}

} // namespace pal::ui
