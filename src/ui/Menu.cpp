#include <pch.h>
#include "Menu.hpp"
#include "Tabs.hpp"
#include "Widgets.hpp"
#include "Theme.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"
#include "engine/ConfigManager.hpp"
#include "engine/GameHelper.hpp"
#include "features/CheatState.hpp"

#include <ShadowGui/Shadow.h>

namespace pal::ui {

namespace {

constexpr float kW        = 1180.f;
constexpr float kH        = 720.f;
constexpr float kSidebarW = 240.f;
constexpr float kHeaderH  = 60.f;

struct TabDef {
    std::string_view label;
    void (*draw)();
};

const std::array<TabDef, 10> kTabs{{
    {"自瞄与透视", TabAimbotESP},
    {"在线功能", TabFeatures},
    {"单人功能", TabSinglePlayer},
    {"帕鲁编辑器", TabPalEditor},
    {"帕鲁生成器", TabPalSpawner},
    {"物品生成器", TabItemSpawner},
    {"传送器", TabTeleporter},
    {"人物/周围对象", TabPlayerObjects},
    {"热键与设置", TabSettings},
    {"更新日志", TabChangeLog},
}};

// 侧边栏「热键与设置」页在 kTabs 中的下标 (顶部「配置」按钮跳转用)
constexpr int kSettingsTabIndex = 8;

struct MenuState {
    int   currentTab = 0;
    float scroll = 0.f;   // 内容区滚动偏移 (px)
    float contentHeight = 0.f; // 上一帧内容总高 (用于滚动钳制)
    Shadow::Vec2 winPos{0.f, 0.f}; // 窗口位置 (0,0 = 尚未初始化, 首帧居中)
};
MenuState g_menu;

// 页头拖动: 按住页头空白处移动窗口 (右侧按钮区除外)。
void UpdateWindowDrag(const Shadow::Vec2& winPos) {
    static bool       s_dragging = false;
    static Shadow::Vec2 s_grabOffset{};
    auto& ctx = Shadow::g_Ctx;

    const Shadow::Vec2 m = ctx.MousePos;
    const bool overHeader = m.x >= winPos.x && m.x <= winPos.x + kW &&
                            m.y >= winPos.y && m.y <= winPos.y + kHeaderH;
    // 右侧按钮区 (UID/保存预设/配置) 不作为拖动热区
    const float buttonsLeft = winPos.x + kW - 24.f - 120.f - 12.f - 96.f - 16.f - 230.f;
    const bool overButtons = m.x >= buttonsLeft &&
                             m.y >= winPos.y && m.y <= winPos.y + kHeaderH;

    if (overHeader && !overButtons && ctx.MouseClicked) {
        s_dragging = true;
        s_grabOffset = {m.x - winPos.x, m.y - winPos.y};
    }
    if (!ctx.MouseDown) s_dragging = false;
    if (s_dragging) {
        g_menu.winPos.x = m.x - s_grabOffset.x;
        g_menu.winPos.y = m.y - s_grabOffset.y;
    }
}

// 顶部栏右侧显示本机玩家 UID (原 FormatPlayerUid, 2 秒刷新一次缓存)
std::string PlayerUidText() {
    static std::string cached = "Unknown";
    static unsigned long long lastRefresh = 0;

    const unsigned long long now = GetTickCount64();
    if (now - lastRefresh < 2000) return cached;
    lastRefresh = now;

    cached = "Unknown";
    Helper::Try([&] {
        SDK::APalPlayerState* ps = Helper::GetPalPlayerState();
        if (!ps || !Helper::IsProbablyValidPtr(ps)) return;

        const SDK::FGuid uid = ps->PlayerUId;
        if (uid.A == 0 && uid.B == 0 && uid.C == 0 && uid.D == 0) return;

        cached = std::format("{:08X}-{:04X}-{:04X}-{:04X}-{:04X}{:04X}",
                             static_cast<SDK::uint32>(uid.A),
                             (static_cast<SDK::uint32>(uid.B) >> 16) & 0xFFFFu,
                             static_cast<SDK::uint32>(uid.B) & 0xFFFFu,
                             (static_cast<SDK::uint32>(uid.C) >> 16) & 0xFFFFu,
                             static_cast<SDK::uint32>(uid.C) & 0xFFFFu,
                             static_cast<SDK::uint32>(uid.D));
    });
    return cached;
}

// 侧边栏导航按钮; 返回是否被点击
bool NavItem(std::string_view label, bool active, Shadow::Vec2 pos, float width, float height) {
    auto& style = Shadow::GetStyle();
    auto* dl = Shadow::GetWindowDrawList();

    const bool hovered = Shadow::IsMouseHovering(pos, {width, height});
    if (active || hovered) {
        const Shadow::Color bg = active
            ? style.Colors[Shadow::GuiCol_TabActive]
            : style.Colors[Shadow::GuiCol_TabHovered];
        dl->AddRectFilled(pos, {width, height}, bg); // Shadow: 第二参数是尺寸
    }
    if (active) { // 左侧高亮条
        dl->AddRectFilled(pos, {3.f, height},
                          style.Colors[Shadow::GuiCol_SliderGrab]);
    }

    const Shadow::Vec2 textSize = Shadow::MeasureTextSize(label);
    dl->AddText({pos.x + (width - textSize.x) * 0.5f, pos.y + (height - textSize.y) * 0.5f},
                active ? style.Colors[Shadow::GuiCol_TextHighlight] : style.Colors[Shadow::GuiCol_Text],
                label);

    return hovered && Shadow::g_Ctx.MouseClicked; // 左键点击 (与库交互一致)
}

void DrawHeader(Shadow::Vec2 winPos) {
    auto* dl = Shadow::GetWindowDrawList();
    auto& style = Shadow::GetStyle();

    // 页头只覆盖侧栏右侧区域, 不再遮住侧栏顶部的 PALENGINE 标志
    const Shadow::Vec2 hMin = {winPos.x + kSidebarW, winPos.y};
    const Shadow::Vec2 hMax = {winPos.x + kW, winPos.y + kHeaderH};
    dl->AddRectFilled(hMin, {hMax.x - hMin.x, kHeaderH}, style.Colors[Shadow::GuiCol_TitleBarBg]);
    dl->AddRectFilled({hMin.x + 24.f, hMax.y - 1.f}, {hMax.x - hMin.x - 48.f, 1.f},
                      Shadow::Color{0.008f, 0.55f, 0.98f, 0.25f});

    // 页面标题
    const Shadow::Vec2 titleSize = Shadow::MeasureTextSize(kTabs[g_menu.currentTab].label);
    dl->AddText({hMin.x + 24.f, (kHeaderH - titleSize.y) * 0.5f},
                style.Colors[Shadow::GuiCol_TextHighlight], kTabs[g_menu.currentTab].label);

    // 右侧: 保存预设 / 配置按钮 (绝对定位)
    const std::string uid = PlayerUidText();
    const float uidWidth = Shadow::MeasureTextSize(uid).x + 30.f;
    const float uidX = hMax.x - 24.f - 120.f - 12.f - 96.f - 16.f - uidWidth;
    dl->AddRectFilled({uidX, winPos.y + 14.f}, {uidWidth, 38.f},
                      Shadow::Color{0.008f, 0.55f, 0.98f, 0.08f});
    dl->AddRect({uidX, winPos.y + 14.f}, {uidWidth, 38.f},
                Shadow::Color{0.008f, 0.55f, 0.98f, 0.35f});
    dl->AddText({uidX + 15.f, winPos.y + 14.f + (38.f - Shadow::MeasureTextSize(uid).y) * 0.5f},
                style.Colors[Shadow::GuiCol_TextDisabled], uid);

    Shadow::g_Ctx.Cursor = {hMax.x - 24.f - 120.f - 12.f - 96.f, winPos.y + 13.f};
    if (Shadow::Button("保存预设", {96.f, 34.f}))
        Config::Save("config.json");

    Shadow::g_Ctx.Cursor = {hMax.x - 24.f - 120.f, winPos.y + 13.f};
    if (Shadow::Button("配置", {120.f, 34.f})) {
        g_menu.currentTab = kSettingsTabIndex;
        g_menu.scroll = 0.f;
    }
    Shadow::g_Ctx.Cursor = {hMin.x + kSidebarW + 20.f, hMax.y + 16.f};
}

void DrawSidebar(Shadow::Vec2 winPos) {
    auto* dl = Shadow::GetWindowDrawList();
    auto& style = Shadow::GetStyle();

    // 边栏背景
    dl->AddRectFilled(winPos, {kSidebarW, kH},
                      style.Colors[Shadow::GuiCol_WindowBg]);
    dl->AddRectFilled({winPos.x + kSidebarW - 1.f, winPos.y},
                      {1.f, kH},
                      Shadow::Color{0.008f, 0.55f, 0.98f, 0.15f});

    // Logo
    const Shadow::Vec2 logoSize = Shadow::MeasureTextSize("PALENGINE");
    dl->AddText({winPos.x + (kSidebarW - logoSize.x) * 0.5f, winPos.y + 18.f},
                style.Colors[Shadow::GuiCol_SliderGrab], "PALENGINE");

    // 状态徽标
    const Shadow::Vec2 verSize = Shadow::MeasureTextSize("v1.0 · Ready");
    dl->AddText({winPos.x + (kSidebarW - verSize.x) * 0.5f, winPos.y + 44.f},
                Shadow::Color{0.2f, 0.85f, 0.4f, 0.8f}, "v1.0 · Ready");

    // 分隔线
    dl->AddLine({winPos.x + 16.f, winPos.y + 78.f}, {winPos.x + kSidebarW - 16.f, winPos.y + 78.f},
                style.Colors[Shadow::GuiCol_Separator], 1.f);

    // 导航项
    constexpr float kNavTop = 94.f;
    constexpr float kNavH = 40.f;
    constexpr float kNavGap = 6.f;
    for (int i = 0; i < static_cast<int>(kTabs.size()); ++i) {
        const Shadow::Vec2 itemPos{winPos.x + 14.f, winPos.y + kNavTop + i * (kNavH + kNavGap)};
        if (NavItem(kTabs[i].label, g_menu.currentTab == i, itemPos, kSidebarW - 28.f, kNavH)) {
            if (g_menu.currentTab != i) {
                g_menu.currentTab = i;
                g_menu.scroll = 0.f;
            }
        }
    }
}

void DrawContent(Shadow::Vec2 winPos) {
    auto& style = Shadow::GetStyle();
    auto& ctx = Shadow::g_Ctx;

    const Shadow::Vec2 contentMin{winPos.x + kSidebarW + 20.f, winPos.y + kHeaderH + 16.f};
    const Shadow::Vec2 contentMax{winPos.x + kW - 24.f, winPos.y + kH - 20.f};

    // 滚动: 鼠标滚轮 (Shadow::Input 已累计到 MouseWheel, NewFrame 每帧清零)
    const float viewH = contentMax.y - contentMin.y;
    const float maxScroll = std::max(0.f, g_menu.contentHeight - viewH);
    g_menu.scroll = std::clamp(g_menu.scroll - ctx.MouseWheel * 40.f, 0.f, maxScroll);

    Shadow::PushClipRect(contentMin, contentMax);
    SetRowArea(contentMin.x, contentMax.x - contentMin.x); // 行布局锚定到内容区
    ctx.Cursor = {contentMin.x, contentMin.y - g_menu.scroll};

    const float startY = ctx.Cursor.y;
    kTabs[g_menu.currentTab].draw();
    g_menu.contentHeight = ctx.Cursor.y - startY; // 供下一帧滚动钳制

    Shadow::PopClipRect();
}

} // namespace

void Initialize() {
    ApplyTheme();
    EnsureFont();
    log::Info("[ui] UI build: {} {}", __DATE__, __TIME__); // 用于确认注入的 DLL 版本
}

void DrawMenu() {
    const Shadow::Vec2 screen = Shadow::GetIO().DisplaySize;

    // 菜单重新打开时丢弃上次会话遗留的弹窗状态 (关闭菜单那一刻下拉框可能还开着)
    static unsigned long long s_lastFrameTick = 0;
    const bool reopened = (GetTickCount64() - s_lastFrameTick > 1000);
    s_lastFrameTick = GetTickCount64();
    if (reopened) Shadow::g_Ctx.ActivePopups.clear();

    // 先应用拖动, 再取窗口位置 —— 避免拖动晚一帧才生效
    UpdateWindowDrag(g_menu.winPos);

    // 窗口位置: 首帧居中, 之后记住位置 (可拖动)
    if (g_menu.winPos.x == 0.f && g_menu.winPos.y == 0.f)
        g_menu.winPos = {(screen.x - kW) * 0.5f, (screen.y - kH) * 0.5f};
    g_menu.winPos.x = std::clamp(g_menu.winPos.x, 0.f, std::max(0.f, screen.x - kW));
    g_menu.winPos.y = std::clamp(g_menu.winPos.y, 0.f, std::max(0.f, screen.y - kH));
    const Shadow::Vec2 winPos = g_menu.winPos;

    Shadow::SetNextWindowPos(winPos);
    Shadow::SetNextWindowSize({kW, kH});
    Shadow::SetNextWindowSizeConstraints({kW, kH}, {kW, kH});

    if (Shadow::Begin("##PalEngineMenu", Shadow::ShadowWindowFlags_NoTitleBar |
                                            Shadow::ShadowWindowFlags_NoScrollbar |
                                            Shadow::ShadowWindowFlags_NoMove |
                                            Shadow::ShadowWindowFlags_NoResize)) {
        DrawSidebar(winPos);
        DrawHeader(winPos);
        DrawContent(winPos);
        Shadow::End();
    }
}

} // namespace pal::ui
