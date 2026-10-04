#include <pch.h>
#include "ui/Tabs.hpp"
#include "ui/Widgets.hpp"
#include "features/CheatState.hpp"
#include "engine/CoordinatesLoader.hpp"

#include <ShadowGui/Shadow.h>

#include <algorithm>
#include <cctype>

// ============================================================================
// TabTeleporter —— 传送器。
// 自定义传送点 (添加/传送/移除) + JSON 坐标文件浏览与搜索。
// 业务逻辑 (TeleportPlayerTo / AddWaypointLocation 等) 在 features 层。
// ============================================================================

namespace pal::ui {

namespace {

std::string ToLowerCopy(const std::string& src) {
    std::string out = src;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// 惰性加载坐标文件; 返回数据是否就绪
bool EnsureDataLoaded() {
    static bool s_initialized = false;
    if (!s_initialized) {
        s_initialized = true;
        CoordinatesLoader::Get().Load();
    }
    return CoordinatesLoader::Get().LastStatus() == CoordLoadStatus::Ok;
}

bool CollectFilteredPoints(const std::string& keyword,
                           std::vector<const TeleporterPoint*>& out) {
    const std::string lower = ToLowerCopy(keyword);
    if (lower.empty()) return false;

    out.clear();
    for (const auto& category : CoordinatesLoader::Get().Categories())
        for (const auto& point : category.points)
            if (ToLowerCopy(point.name).find(lower) != std::string::npos)
                out.push_back(&point);
    return true;
}

// 自定义传送点行: 名称 + 传送/移除按钮 (右对齐)
void DrawWaypointRow(size_t index) {
    auto& wp = g_Waypoints[index];

    BeginRow();
    RowLabel(wp.waypointName);

    const float btnW = 64.f;
    Shadow::g_Ctx.Cursor.x = RowRight() - btnW * 2 - Shadow::GetStyle().ItemSpacing.x - 12.f;

    if (Shadow::Button(std::format("传送##{}", index), {btnW, 0.f}))
        TeleportPlayerTo(wp.waypointLocation);
    Shadow::SameLine();
    if (Shadow::Button(std::format("移除##{}", index), {btnW, 0.f})) {
        RemoveWaypointLocationByName(wp.waypointName);
        EndRow();
        return;
    }
    EndRow();
}

} // namespace

void TabTeleporter() {
    using namespace pal::ui;
    static std::string wpName;        // 自定义传送点名称
    static std::string searchText;    // 坐标点搜索
    static int selectedCategory = -1;

    const bool dataReady = EnsureDataLoaded();
    if (!dataReady) {
        BeginPanel("坐标数据");
        TextDesc(std::format("坐标文件加载失败: {}", CoordinatesLoader::Get().LastError()));
        if (ButtonFull("重试加载"))
            CoordinatesLoader::Get().Load();
        EndPanel();
    }

    // === 自定义传送点 ===
    BeginPanel("自定义传送点");
    InputText("传送点名称", wpName);
    if (ButtonFull("添加传送点 (记录当前位置)")) {
        if (!wpName.empty()) {
            AddWaypointLocation(wpName);
            wpName.clear();
        }
    }
    if (ButtonFull("传送回家 (F7)"))
        TeleportPlayerToHome();

    for (size_t i = 0; i < g_Waypoints.size(); ++i)
        DrawWaypointRow(i);
    EndPanel();

    // === JSON 坐标点浏览 / 搜索 ===
    if (!dataReady) return;

    BeginPanel("坐标点传送");
    InputText("搜索坐标点", searchText);

    const auto& categories = CoordinatesLoader::Get().Categories();
    if (categories.empty()) {
        TextDesc("未加载到任何分类。");
        EndPanel();
        return;
    }

    // 分类下标保护: 默认选中第一个非空分类
    if (selectedCategory < 0 || selectedCategory >= static_cast<int>(categories.size())) {
        selectedCategory = 0;
        while (selectedCategory < static_cast<int>(categories.size()) &&
               categories[selectedCategory].points.empty())
            ++selectedCategory;
        if (selectedCategory >= static_cast<int>(categories.size())) selectedCategory = 0;
    }

    std::vector<const TeleporterPoint*> filtered;
    const bool searching = CollectFilteredPoints(searchText, filtered);

    // 分类选择 (Combo)
    std::vector<std::string> categoryNames;
    categoryNames.reserve(categories.size());
    for (const auto& cat : categories) {
        categoryNames.push_back(cat.points.empty()
            ? std::format("{} (空)", cat.name)
            : std::format("{} ({})", cat.name, cat.points.size()));
    }
    if (!searching)
        Combo("分类", &selectedCategory, categoryNames);

    // 坐标点集合: 搜索时为全部命中, 否则为当前分类
    std::vector<const TeleporterPoint*> points;
    if (searching) {
        points = filtered;
    } else {
        points.reserve(categories[selectedCategory].points.size());
        for (const auto& p : categories[selectedCategory].points) points.push_back(&p);
    }

    if (searching && filtered.empty())
        TextDesc("没有匹配的坐标点。");
    else if (!searching && categories[selectedCategory].points.empty())
        TextDesc("该分类暂无坐标点。");

    constexpr int kColumns = 3;
    constexpr float kColumnGap = 8.f;
    int column = 0;
    for (const TeleporterPoint* point : points) {
        if (column == 0) BeginRow(); // 每 3 个按钮共一行卡片 (上一版每项一行导致阶梯状错乱)
        const float btnW = (RowWidth() - kColumnGap * (kColumns - 1)) / kColumns;
        Shadow::g_Ctx.Cursor.x = RowLeft() + column * (btnW + kColumnGap);
        if (Shadow::Button(point->name, {btnW, 30.f}))
            TeleportPlayerTo(SDK::FVector(point->x, point->y, point->z));
        if (++column == kColumns) {
            EndRow();
            column = 0;
        }
    }
    if (column != 0) EndRow(); // 收尾不完整的一行
    EndPanel();
}

} // namespace pal::ui
