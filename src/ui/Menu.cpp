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

using namespace theme;

constexpr float kW        = 1180.f;
constexpr float kH        = 720.f;
constexpr float kSidebarW = 240.f;
constexpr float kHeaderH  = 60.f;

constexpr float kWinPad      = 8.f;    // 窗口内边距 (侧栏与内容的外框留白)
constexpr float kNavTop      = 104.f;  // 首个导航项相对窗口顶部的偏移
constexpr float kNavH        = 38.f;
constexpr float kNavGap      = 4.f;
constexpr float kBtnH        = 36.f;
constexpr float kBtnPrimaryW = 104.f;
constexpr float kBtnGhostW   = 80.f;
constexpr float kBtnGap      = 10.f;
constexpr float kRightPad    = 22.f;

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
    float navIndicatorY = -1.f;    // 侧栏选中胶囊的动画 Y (<0 = 尚未初始化)
    float contentFade = 0.f;       // 内容区淡入进度 (切页 / 开菜单时 0 -> 1)
};
MenuState g_menu;

// 颜色线性插值 (胶囊滑过时让文字颜色平滑过渡)
Shadow::Color LerpColor(Shadow::Color a, Shadow::Color b, float t) {
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
             a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}

// 帧率无关的指数趋近: 本帧朝目标推进的比例
float EaseStep(float dt, float tau) {
    return 1.f - std::exp(-std::max(0.0001f, dt) / tau);
}

// 中文 UI 字号需要缩放时安全压栈
void PushUiFont(float scale) { if (Shadow::DefaultFont) Shadow::PushFont(Shadow::DefaultFont, scale); }
void PopUiFont() { if (Shadow::DefaultFont) Shadow::PopFont(); }

// 窗口投影 + 外框: 深色主题用纯黑多层扩散, 最后压一道 1.5px 描边 (EVICTED 的窗口边)。
// 背景层必须在 Begin 之前发射; 显式关闭裁剪, 避免上一帧遗留的裁剪框吃掉投影。
void DrawWindowShadow(Shadow::Vec2 winPos) {
    auto* dl = Shadow::GetBackgroundDrawList();
    for (int i = 6; i >= 1; --i) {
        const float grow = static_cast<float>(i) * 2.2f;
        const float alpha = 0.34f * (1.f - static_cast<float>(i - 1) / 6.f);
        dl->AddRectFilledRounded({ winPos.x - grow, winPos.y - grow + 4.f },
                                 { kW + grow * 2.f, kH + grow * 2.f },
                                 kRadiusWindow + grow,
                                 A(WithAlpha(ShadowTint, alpha)));
        dl->CmdBuffer.back().clippingEnabled = false;
    }

    // 窗口外框描边
    dl->AddRectRounded(winPos, { kW, kH }, kRadiusWindow, A(Border), 1.5f);
    dl->CmdBuffer.back().clippingEnabled = false;
}

// 中央竖排文本
void DrawCenteredText(Shadow::Vec2 pos, Shadow::Vec2 size, Shadow::Color color, std::string_view text) {
    const Shadow::Vec2 ts = Shadow::MeasureTextSize(text);
    Shadow::GetWindowDrawList()->AddText({ pos.x + (size.x - ts.x) * 0.5f,
                                           pos.y + (size.y - ts.y) * 0.5f }, color, text);
}

// 页头拖动: 按住页头空白处移动窗口 (右侧按钮区除外)。
void UpdateWindowDrag(const Shadow::Vec2& winPos) {
    static bool       s_dragging = false;
    static Shadow::Vec2 s_grabOffset{};
    auto& ctx = Shadow::g_Ctx;

    const Shadow::Vec2 m = ctx.MousePos;
    const bool overHeader = m.x >= winPos.x && m.x <= winPos.x + kW &&
                            m.y >= winPos.y && m.y <= winPos.y + kHeaderH;
    // 右侧按钮区 (UID/保存预设/配置) 不作为拖动热区
    const float buttonsLeft = winPos.x + kW - kRightPad - kBtnGhostW - kBtnGap - kBtnPrimaryW - 210.f;
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

// 侧边栏导航项。选中胶囊由 DrawSidebar 统一绘制 (这样它才能在两项之间滑动),
// 这里只画悬停底色 + 圆点 + 文字; cover = 胶囊覆盖本项的程度 0..1,
// 让文字在胶囊滑过时从深色平滑过渡到白色。
bool NavItem(std::string_view label, bool active, float cover, Shadow::Vec2 pos, float width, float height) {
    auto* dl = Shadow::GetWindowDrawList();
    const bool hovered = Shadow::IsMouseHovering(pos, {width, height});

    if (!active && hovered)
        dl->AddRectFilledRounded(pos, {width, height}, kRadiusInput, A(Rgb(0x2B2836)));

    // 前导小圆点
    dl->AddRectFilledRounded({ pos.x + 12.f, pos.y + (height - 6.f) * 0.5f }, { 6.f, 6.f }, 3.f,
                             LerpColor(A(TextDim), A(Rgb(0xFFFFFF)), cover));

    const Shadow::Vec2 ts = Shadow::MeasureTextSize(label);
    dl->AddText({ pos.x + 28.f, pos.y + (height - ts.y) * 0.5f },
                LerpColor(A(Text2), A(Rgb(0xFFFFFF)), cover), label);

    return hovered && Shadow::g_Ctx.MouseClicked; // 左键点击 (与库交互一致)
}

void DrawHeader(Shadow::Vec2 winPos) {
    auto* dl = Shadow::GetWindowDrawList();

    const float left = winPos.x + kSidebarW + 18.f;
    const float cy = winPos.y + kHeaderH * 0.5f;

    // 页面标题 (16px 主标题)
    PushUiFont(kFontTitle);
    const Shadow::Vec2 titleSize = Shadow::MeasureTextSize(kTabs[g_menu.currentTab].label);
    dl->AddText({ left, cy - titleSize.y * 0.5f }, A(Text1), kTabs[g_menu.currentTab].label);
    PopUiFont();

    // 页头底部分隔线: 左段为 accent 渐变淡出 (EVICTED 的强调线), 右段为普通描边
    {
        const float sepY = winPos.y + kHeaderH - 1.f;
        const float sepW = kW - kSidebarW;
        const float glowW = sepW * 0.35f;
        dl->AddRectFilledGradientRounded({ winPos.x + kSidebarW, sepY }, { glowW, 1.f }, 0.f,
                                         A(Accent), A(WithAlpha(Accent, 0.f)), false);
        dl->AddRectFilled({ winPos.x + kSidebarW + glowW, sepY }, { sepW - glowW, 1.f },
                          A(WithAlpha(Border, 0.9f)));
    }

    // ---- 右侧控件: [UID 胶囊] [保存预设] [配置] ----
    const float btnY = winPos.y + (kHeaderH - kBtnH) * 0.5f;
    const float ghostX = winPos.x + kW - kRightPad - kBtnGhostW;
    const float primaryX = ghostX - kBtnGap - kBtnPrimaryW;

    const std::string uid = PlayerUidText();
    const float uidW = Shadow::MeasureTextSize(uid).x + 24.f;
    const float uidX = primaryX - kBtnGap - uidW;
    dl->AddRectFilledRounded({ uidX, btnY }, { uidW, kBtnH }, kRadiusInput, A(Surface));
    dl->AddRectRounded({ uidX, btnY }, { uidW, kBtnH }, kRadiusInput, A(Border), 1.f);
    DrawCenteredText({ uidX, btnY }, { uidW, kBtnH }, A(Text2), uid);

    // 主强调按钮
    Shadow::g_Ctx.Cursor = { primaryX, btnY };
    if (ButtonPrimaryAt({ primaryX, btnY }, { kBtnPrimaryW, kBtnH }, "保存预设"))
        Config::Save("config.json");

    // 次级 (幽灵) 按钮
    Shadow::g_Ctx.Cursor = { ghostX, btnY };
    if (ButtonPrimaryAt({ ghostX, btnY }, { kBtnGhostW, kBtnH }, "配置", /*ghost=*/true)) {
        g_menu.currentTab = kSettingsTabIndex;
        g_menu.scroll = 0.f;
        g_menu.contentFade = 0.f; // 内容区重新淡入
    }

    Shadow::g_Ctx.Cursor = { left, winPos.y + kHeaderH + 16.f };
}

void DrawSidebar(Shadow::Vec2 winPos) {
    auto* dl = Shadow::GetWindowDrawList();

    const Shadow::Vec2 panelPos{ winPos.x + kWinPad, winPos.y + kWinPad };
    const Shadow::Vec2 panelSize{ kSidebarW - kWinPad * 2.f, kH - kWinPad * 2.f };

    // 侧栏圆角面板 (bg-soft)
    dl->AddRectFilledRounded(panelPos, panelSize, kRadiusCard, A(BgSoft));

    const float panelCenterX = panelPos.x + panelSize.x * 0.5f;

    // Logo + accent 渐变下划线 (EVICTED 风格的强调线: 两端淡出)
    PushUiFont(kFontTitle);
    const Shadow::Vec2 logoSize = Shadow::MeasureTextSize("PALENGINE");
    dl->AddText({ panelCenterX - logoSize.x * 0.5f, panelPos.y + 20.f }, A(Text1), "PALENGINE");
    PopUiFont();

    {
        const float barW = std::min(logoSize.x, 130.f);
        const float barX = panelCenterX - barW * 0.5f;
        const float barY = panelPos.y + 41.f;
        dl->AddRectFilledGradientRounded({ barX, barY }, { barW * 0.5f, 2.f }, 1.f,
                                         A(WithAlpha(Accent, 0.f)), A(Accent), false);
        dl->AddRectFilledGradientRounded({ barX + barW * 0.5f, barY }, { barW * 0.5f, 2.f }, 1.f,
                                         A(Accent), A(WithAlpha(Accent, 0.f)), false);
    }

    // 版本 + 状态点
    {
        const std::string ver = "v1.0 · Ready";
        const Shadow::Vec2 vs = Shadow::MeasureTextSize(ver);
        const float dot = 6.f;
        const float totalW = vs.x + dot + 6.f;
        const float x = panelCenterX - totalW * 0.5f;
        dl->AddRectFilledRounded({ x, panelPos.y + 50.f + (vs.y - dot) * 0.5f }, { dot, dot }, dot * 0.5f, A(Mint));
        dl->AddText({ x + dot + 6.f, panelPos.y + 50.f }, A(Text2), ver);
    }

    // 分隔线
    dl->AddRectFilled({ panelPos.x + 14.f, panelPos.y + 80.f }, { panelSize.x - 28.f, 1.f },
                      A(WithAlpha(Border, 0.9f)));

    // ---- 导航项 ----
    const float itemX = panelPos.x + 8.f;
    const float itemW = panelSize.x - 16.f;

    // 选中胶囊的滑动动画: 位置指数趋近当前项 —— 打开菜单/切页都是"滑过去"而不是瞬移
    const float dt = static_cast<float>(Shadow::GetIO().DeltaTime);
    const float targetY = winPos.y + kNavTop + g_menu.currentTab * (kNavH + kNavGap);
    if (g_menu.navIndicatorY < 0.f) g_menu.navIndicatorY = targetY;
    g_menu.navIndicatorY += (targetY - g_menu.navIndicatorY) * EaseStep(dt, 0.075f);
    if (std::abs(targetY - g_menu.navIndicatorY) < 0.4f) g_menu.navIndicatorY = targetY;

    // 先画胶囊, 再画各项 (圆点/文字压在胶囊之上)。选中胶囊用蓝紫渐变 (EVICTED Tab)
    dl->AddRectFilledGradientRounded({ itemX, g_menu.navIndicatorY }, { itemW, kNavH },
                                     kRadiusInput, A(AccentBlue), A(AccentDeep), false);
    // 选中项左侧强调竖条
    dl->AddRectFilledRounded({ itemX + 2.f, g_menu.navIndicatorY + 9.f }, { 3.f, kNavH - 18.f },
                             kRadiusPill, A(Accent));

    for (int i = 0; i < static_cast<int>(kTabs.size()); ++i) {
        const float itemY = winPos.y + kNavTop + i * (kNavH + kNavGap);
        // 胶囊覆盖本项的程度, 用于文字颜色交叉淡出
        const float cover = std::clamp(1.f - std::abs(itemY - g_menu.navIndicatorY) / kNavH, 0.f, 1.f);
        if (NavItem(kTabs[i].label, g_menu.currentTab == i, cover, { itemX, itemY }, itemW, kNavH)) {
            if (g_menu.currentTab != i) {
                g_menu.currentTab = i;
                g_menu.scroll = 0.f;
                g_menu.contentFade = 0.f; // 内容区重新淡入
            }
        }
    }

    // 底部状态胶囊: 注入正常
    {
        const std::string status = "注入正常";
        const Shadow::Vec2 ss = Shadow::MeasureTextSize(status);
        const float pillW = ss.x + 40.f;
        const float pillH = 28.f;
        const Shadow::Vec2 pillPos{ panelCenterX - pillW * 0.5f,
                                    panelPos.y + panelSize.y - pillH - 14.f };
        dl->AddRectFilledRounded(pillPos, { pillW, pillH }, kRadiusPill, A(Surface));
        dl->AddRectRounded(pillPos, { pillW, pillH }, kRadiusPill, A(Border), 1.f);
        dl->AddRectFilledRounded({ pillPos.x + 14.f, pillPos.y + (pillH - 6.f) * 0.5f }, { 6.f, 6.f }, 3.f, A(Mint));
        dl->AddText({ pillPos.x + 26.f, pillPos.y + (pillH - ss.y) * 0.5f }, A(Mint), status);
    }
}

void DrawContent(Shadow::Vec2 winPos) {
    auto& ctx = Shadow::g_Ctx;

    const Shadow::Vec2 contentMin{winPos.x + kSidebarW + 18.f, winPos.y + kHeaderH + 16.f};
    const Shadow::Vec2 contentMax{winPos.x + kW - 22.f, winPos.y + kH - 18.f};

    // 滚动: 鼠标滚轮 (Shadow::Input 已累计到 MouseWheel, NewFrame 每帧清零)
    const float viewH = contentMax.y - contentMin.y;
    const float maxScroll = std::max(0.f, g_menu.contentHeight - viewH);
    g_menu.scroll = std::clamp(g_menu.scroll - ctx.MouseWheel * 40.f, 0.f, maxScroll);

    // 内容淡入: 打开菜单 / 切页时从透明渐显, 避免内容"啪"地跳出来
    g_menu.contentFade += (1.f - g_menu.contentFade) * EaseStep(static_cast<float>(Shadow::GetIO().DeltaTime), 0.05f);
    if (g_menu.contentFade > 0.999f) g_menu.contentFade = 1.f;

    Shadow::PushClipRect(contentMin, contentMax);
    // 只对内容区下调全局 Alpha (全局显隐淡入淡出在 FrameDriver 里已经压过一层)
    Shadow::PushStyleVar(Shadow::GuiStyleVar_Alpha, Shadow::GetStyle().Alpha * g_menu.contentFade);

    SetRowArea(contentMin.x, contentMax.x - contentMin.x); // 行布局锚定到内容区
    ctx.Cursor = {contentMin.x, contentMin.y - g_menu.scroll};

    const float startY = ctx.Cursor.y;
    kTabs[g_menu.currentTab].draw();
    g_menu.contentHeight = ctx.Cursor.y - startY; // 供下一帧滚动钳制

    Shadow::PopStyleVar();
    Shadow::PopClipRect();
}

} // namespace

void Initialize() {
    ApplyTheme();
    EnsureFont();
    log::Info("[ui] UI build: {} {}", __DATE__, __TIME__); // 用于确认注入的 DLL 版本
}

void DrawMenu() {
    // 每帧重申主题: 防止任何一方 (含库内部) 在帧中途改回默认配色
    ApplyTheme();

    const Shadow::Vec2 screen = Shadow::GetIO().DisplaySize;

    // 菜单重新打开时丢弃上次会话遗留的弹窗状态 (关闭菜单那一刻下拉框可能还开着)
    static unsigned long long s_lastFrameTick = 0;
    const bool reopened = (GetTickCount64() - s_lastFrameTick > 1000);
    s_lastFrameTick = GetTickCount64();
    if (reopened) {
        Shadow::g_Ctx.ActivePopups.clear();
        g_menu.contentFade = 0.f;     // 每次打开菜单, 内容重新淡入
        g_menu.navIndicatorY = -1.f;  // 胶囊直接就位, 不走"从上往下滑"的初始动画
    }

    // 先应用拖动, 再取窗口位置 —— 避免拖动晚一帧才生效
    UpdateWindowDrag(g_menu.winPos);

    // 窗口位置: 首帧居中, 之后记住位置 (可拖动)
    if (g_menu.winPos.x == 0.f && g_menu.winPos.y == 0.f)
        g_menu.winPos = {(screen.x - kW) * 0.5f, (screen.y - kH) * 0.5f};
    g_menu.winPos.x = std::clamp(g_menu.winPos.x, 0.f, std::max(0.f, screen.x - kW));
    g_menu.winPos.y = std::clamp(g_menu.winPos.y, 0.f, std::max(0.f, screen.y - kH));
    const Shadow::Vec2 winPos = g_menu.winPos;

    // 投影画在窗口之前的背景层, 因此必须在 Begin 之前发射
    DrawWindowShadow(winPos);

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
