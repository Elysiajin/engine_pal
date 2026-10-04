#include <pch.h>
#include "Theme.hpp"
#include "core/Log.hpp"

#include <ShadowGui/Shadow.h>

namespace pal::ui {

void ApplyTheme() {
    static bool s_applied = false;
    if (s_applied) return;
    s_applied = true;

    auto& colors = Shadow::GetStyle().Colors;

    // 主色: 电光蓝
    const Shadow::Color accent{0.050f, 0.600f, 1.000f, 1.0f};

    // 背景: 深海蓝黑, 略有层次而不是死黑
    colors[Shadow::GuiCol_WindowBg]      = {0.016f, 0.018f, 0.026f, 0.985f};
    colors[Shadow::GuiCol_TitleBarBg]    = {0.012f, 0.014f, 0.021f, 1.00f};
    colors[Shadow::GuiCol_PopupBg]       = {0.020f, 0.023f, 0.032f, 0.99f};
    colors[Shadow::GuiCol_PopupBorder]   = {0.060f, 0.100f, 0.150f, 0.80f};

    // 文本: 提高说明文字亮度 (上一版 0.14 几乎不可读)
    colors[Shadow::GuiCol_Text]          = {0.88f, 0.90f, 0.94f, 1.00f};
    colors[Shadow::GuiCol_TextHighlight] = {0.55f, 0.80f, 1.00f, 1.00f};
    colors[Shadow::GuiCol_TextDisabled]  = {0.52f, 0.56f, 0.64f, 1.00f};
    colors[Shadow::GuiCol_TextShadow]    = {0.00f, 0.00f, 0.00f, 0.45f};
    colors[Shadow::GuiCol_TextOutline]   = {0.00f, 0.00f, 0.00f, 0.55f};

    // 控件: 行卡片比窗口背景亮一档, 悬停再亮一档
    colors[Shadow::GuiCol_FrameBg]        = {0.034f, 0.039f, 0.054f, 1.00f};
    colors[Shadow::GuiCol_FrameBgHovered] = {0.052f, 0.060f, 0.082f, 1.00f};
    colors[Shadow::GuiCol_Button]         = {0.060f, 0.068f, 0.092f, 1.00f};
    colors[Shadow::GuiCol_ButtonHovered]  = {0.100f, 0.115f, 0.155f, 1.00f};
    colors[Shadow::GuiCol_SwitchBg]              = {0.055f, 0.062f, 0.085f, 1.00f};
    colors[Shadow::GuiCol_SwitchBgHovered]       = {0.075f, 0.085f, 0.115f, 1.00f};
    colors[Shadow::GuiCol_SwitchBgActive]        = accent;
    colors[Shadow::GuiCol_SwitchBgActiveHovered] = {0.100f, 0.680f, 1.000f, 1.00f};

    colors[Shadow::GuiCol_SliderGrab]      = accent;
    colors[Shadow::GuiCol_CheckMark]       = accent;
    colors[Shadow::GuiCol_ActiveIndicator] = accent;
    colors[Shadow::GuiCol_ResizeGrip]        = accent;
    colors[Shadow::GuiCol_ResizeGripHovered] = {0.150f, 0.680f, 1.00f, 1.0f};

    // 标签页: 激活页用主色淡染
    colors[Shadow::GuiCol_Tab]        = {0.000f, 0.000f, 0.000f, 0.000f};
    colors[Shadow::GuiCol_TabHovered] = {0.050f, 0.058f, 0.080f, 1.00f};
    colors[Shadow::GuiCol_TabActive]  = {0.050f, 0.600f, 1.000f, 0.16f};

    // 分隔/边框: 微微透出主色
    colors[Shadow::GuiCol_Separator] = {0.070f, 0.080f, 0.105f, 1.00f};
    colors[Shadow::GuiCol_Border]    = {0.050f, 0.250f, 0.400f, 0.45f};

    auto& style = Shadow::GetStyle();
    style.WindowPadding  = {16.f, 14.f};
    style.FramePadding   = {9.f, 4.f};
    style.ItemSpacing    = {10.f, 7.f};
    style.WindowMinSize  = {420.f, 300.f};
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
