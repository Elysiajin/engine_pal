#include <pch.h>
#include "Theme.hpp"
#include "core/Log.hpp"

#include <ShadowGui/Shadow.h>

namespace pal::ui {

using theme::Rgb;
using theme::WithAlpha;

namespace {

// 全部 GuiCol_ 一次性铺满。Shadow-Gui 的 GuiStyle::Colors 没有默认值,
// 只设置子集会让其余槽位保持未初始化的随机值 (之前版本即如此)。
//
// 配色源自 EVICTED (recode saphire): 深灰底 + 深紫侧栏 + 紫强调 + 蓝紫选中。
void ApplyColors(Shadow::Color (&c)[Shadow::GuiCol_COUNT]) {
    using namespace theme;

    // 背景层
    c[Shadow::GuiCol_WindowBg]      = BgBase;
    c[Shadow::GuiCol_TitleBarBg]    = BgSoft;
    c[Shadow::GuiCol_PopupBg]       = Popup;
    c[Shadow::GuiCol_PopupBorder]   = Border;
    c[Shadow::GuiCol_Transparent]   = { 0.f, 0.f, 0.f, 0.f };

    // 文本 (深色底: 主文字近白, 次要中灰, 禁用压暗)
    c[Shadow::GuiCol_Text]          = Text1;
    c[Shadow::GuiCol_TextDisabled]  = TextDim;
    c[Shadow::GuiCol_TextHighlight] = Accent;
    c[Shadow::GuiCol_ErrorText]     = Danger;
    c[Shadow::GuiCol_TextShadow]    = WithAlpha(Rgb(0x000000), 0.55f);
    c[Shadow::GuiCol_TextOutline]   = WithAlpha(Rgb(0x000000), 0.0f); // 深色主题不做白描边

    // 控件底 (输入框 / 下拉框 / 滑块轨道 / 滚动条轨道): 比卡片略亮, 靠明度区分
    c[Shadow::GuiCol_FrameBg]        = Surface2;
    c[Shadow::GuiCol_FrameBgHovered] = Rgb(0x26262C);
    c[Shadow::GuiCol_ControlDisabled]= WithAlpha(Surface2, 0.55f);

    // 按钮: 扁平深底 (EVICTED button #1C1E23)
    c[Shadow::GuiCol_Button]         = Rgb(0x1C1E23);
    c[Shadow::GuiCol_ButtonHovered]  = Rgb(0x24262D);

    // 滑块 / 复选
    c[Shadow::GuiCol_SliderGrab]     = Accent;
    c[Shadow::GuiCol_SliderKnob]     = Text1;        // 白色圆形手柄
    c[Shadow::GuiCol_CheckMark]      = Accent;
    c[Shadow::GuiCol_ActiveIndicator]= Accent;
    c[Shadow::GuiCol_InactiveIndicator] = TextDim;

    // 开关 (深色轨道 + 白点; 开 = 紫)
    c[Shadow::GuiCol_SwitchBg]              = Rgb(0x2A2C33);
    c[Shadow::GuiCol_SwitchBgHovered]       = Rgb(0x33353D);
    c[Shadow::GuiCol_SwitchBgActive]        = Accent;
    c[Shadow::GuiCol_SwitchBgActiveHovered]= Rgb(0xD65CFF);
    c[Shadow::GuiCol_SwitchKnob]            = Rgb(0xFFFFFF);

    // 边界 / 分割 / 手柄
    c[Shadow::GuiCol_Separator]      = SeparatorCol;
    c[Shadow::GuiCol_Border]         = Border;
    c[Shadow::GuiCol_ResizeGrip]         = Surface2;
    c[Shadow::GuiCol_ResizeGripHovered]  = Accent;
    c[Shadow::GuiCol_ResizeGripActive]   = AccentBlue;

    // 标签页 / 下拉选中 (选中用蓝色调, 与侧栏 Tab 一致)
    c[Shadow::GuiCol_Tab]         = { 0.f, 0.f, 0.f, 0.f };
    c[Shadow::GuiCol_TabHovered]  = Rgb(0x24232A);
    c[Shadow::GuiCol_TabActive]   = WithAlpha(AccentBlue, 0.22f);
    c[Shadow::GuiCol_DropdownActive] = WithAlpha(AccentBlue, 0.18f);

    // 颜色选择器 (弹层内保持可用的中性色)
    c[Shadow::GuiCol_ColorPickerDark]   = Rgb(0x0F0F12);
    c[Shadow::GuiCol_ColorPickerLight]  = Rgb(0xE9E9EE);
    c[Shadow::GuiCol_CheckerboardLight] = Rgb(0xE9E9EE);
    c[Shadow::GuiCol_CheckerboardDark]  = Rgb(0x2A2A30);
    c[Shadow::GuiCol_ColorPickerShadow] = { 0.f, 0.f, 0.f, 1.f };
}

} // namespace

// 注意: 本函数会被每帧调用, 必须保持幂等且足够便宜 (仅写 ~40 个颜色 + 若干尺寸)。
//
// 为什么不能只应用一次: Shadow 的 NewFrame 里有
//     if (!g_Ctx.StyleInitialized) { StyleColorsOcean(); g_Ctx.StyleInitialized = true; }
// 而 ui::Initialize() 发生在首帧 NewFrame **之前**, 所以一次性应用会被这个惰性
// 默认主题整表覆盖 —— 现象就是窗口底色/滑块/滚动条停在深海蓝, 而 App 层自绘的
// 深紫元素照常生效, 整个菜单配色撕裂。
void ApplyTheme() {
    auto& style = Shadow::GetStyle();
    auto& colors = style.Colors;

    // 抢先标记已初始化, 从根源上阻止库自己套用 StyleColorsOcean
    Shadow::g_Ctx.StyleInitialized = true;

    ApplyColors(colors);

    // ---- 尺寸与间距 (EVICTED: 紧凑、圆角小) ----
    style.WindowPadding  = { 18.f, 16.f };
    style.FramePadding   = { 10.f, 4.f };
    style.ItemSpacing    = { 10.f, 8.f };
    style.WindowMinSize  = { 420.f, 300.f };

    style.ScrollbarSize   = 4.f;
    style.ScrollbarMargin = 4.f;

    style.TabExtraWidth = 24.f;
    style.SwitchPadding = 3.f;
    style.LabelSpacing  = 10.f;

    // ---- 圆角 (EVICTED: window 8 / child 7 / popup 5 / frame 4) ----
    style.WindowRounding    = theme::kRadiusWindow;
    style.FrameRounding     = theme::kRadiusInput;
    style.ButtonRounding    = theme::kRadiusInput;
    style.SwitchRounding    = theme::kRadiusPill;
    style.SliderRounding    = theme::kRadiusPill;
    style.ScrollbarRounding = theme::kRadiusPill;
    style.PopupRounding     = 5.f;
    style.TabRounding       = theme::kRadiusInput;
    style.ItemRounding      = theme::kRadiusInput;
    style.SmallRounding     = theme::kRadiusTag;

    // ---- 描边与文本对齐 (深色主题: 控件靠明度区分, 不描边) ----
    style.FrameBorderThickness  = 0.f;
    style.ButtonBorderThickness = 0.f;
    style.ButtonTextCenter      = true;
    style.SliderKnobRingThickness = 0.f;

    // ---- 字体缩放基准 ----
    style.FontScaleDpi = 1.0f;
}

void EnsureFont() {
    static bool s_tried = false;
    if (s_tried) return;
    s_tried = true;

    // 中文 UI 需要含 CJK 字形的字体: 优先微软雅黑, 退化到系统黑体
    for (const wchar_t* path : {L"C:\\Windows\\Fonts\\msyh.ttc",
                                L"C:\\Windows\\Fonts\\msyh.ttf",
                                L"C:\\Windows\\Fonts\\simhei.ttf"}) {
        if (std::filesystem::exists(path)) {
            if (SDK::UFont* font = Shadow::LoadFontFromFile(path)) {
                Shadow::DefaultFont = font;
                log::Info("[ui] 字体加载成功: {}", std::filesystem::path(path).filename().string());
                return;
            }
        }
    }
    log::Warn("[ui] 系统中文字体加载失败, 中文文本可能显示异常 (使用引擎默认字体)");
}

} // namespace pal::ui
