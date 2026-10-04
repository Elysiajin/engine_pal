#include <pch.h>
#include "Widgets.hpp"
#include "engine/ConfigManager.hpp"

namespace pal::ui {

namespace {

struct RowState {
    bool  joinNext = false;
    float padX = 0.f;      // 进入行之前的 WindowPadding.x
    Shadow::Vec2 start{};
    size_t cmdIndex = 0;
    float areaLeft  = 0.f; // 行区域左缘 x (内容区左缘, 由 SetRowArea 设置)
    float areaWidth = 0.f; // 行区域宽度
};
RowState g_row;

constexpr float kRowPaddingX = 12.f;  // 行内左右内边距
constexpr float kRowPaddingY = 8.f;   // 行内上下内边距

std::string HiddenId(std::string_view label) { return std::format("##{}", label); }

std::string_view DisplayName(std::string_view label) {
    const size_t pos = label.find("##");
    return pos == std::string_view::npos ? label : label.substr(0, pos);
}

} // namespace

// ---------------------------------------------------------------------------
// 行/卡片布局
// ---------------------------------------------------------------------------

void BeginRow() {
    auto& ctx = Shadow::g_Ctx;
    auto& style = Shadow::GetStyle();

    if (g_row.joinNext) {
        ctx.Cursor.y -= style.ItemSpacing.y; // 与上一行底部重合
        g_row.joinNext = false;
    }
    g_row.cmdIndex = Shadow::GetWindowDrawList()->CmdBuffer.size();
    g_row.padX = style.WindowPadding.x;

    style.WindowPadding.x += kRowPaddingX;
    // 行锚定在行区域 (内容区) 左缘, 而不是窗口左缘 ——
    // 上一版锚到窗口左缘, 行标签落在内容裁剪区之外, 全部不可见。
    ctx.Cursor.x = g_row.areaLeft + style.WindowPadding.x;
    g_row.start = ctx.Cursor;
    ctx.Cursor.y += kRowPaddingY;
}

void EndRow() {
    auto& ctx = Shadow::g_Ctx;
    auto& style = Shadow::GetStyle();

    ctx.Cursor.y -= style.ItemSpacing.y;
    ctx.Cursor.y += kRowPaddingY;

    const float height = ctx.Cursor.y - g_row.start.y;
    Shadow::ShadowDrawCmd bg{};
    bg.type = Shadow::ShadowDrawCmdType::RectFilled;
    bg.pos = g_row.start;
    bg.size = {RowWidth(), height};
    bg.color = style.Colors[Shadow::GuiCol_FrameBg];
    bg.clippingEnabled = ctx.ClippingEnabled;
    bg.clipMin = ctx.ClipMin;
    bg.clipMax = ctx.ClipMax;
    auto& cmds = Shadow::GetWindowDrawList()->CmdBuffer;
    cmds.insert(cmds.begin() + static_cast<ptrdiff_t>(g_row.cmdIndex), bg);

    style.WindowPadding.x = g_row.padX;
    ctx.Cursor.x = g_row.areaLeft + style.WindowPadding.x;
    ctx.Cursor.y += style.ItemSpacing.y;
}

void JoinNext() { g_row.joinNext = true; }

void SetRowArea(float leftX, float width) {
    g_row.areaLeft = leftX;
    g_row.areaWidth = width;
}

void BeginPanel(std::string_view title) {
    Shadow::g_Ctx.Cursor.y += 4.f;
    if (!title.empty()) {
        Shadow::TextDisabled(title);
        Shadow::g_Ctx.Cursor.y += 4.f;
    }
}

void EndPanel() { Shadow::g_Ctx.Cursor.y += 12.f; }

// ---------------------------------------------------------------------------
// 行内工具
// ---------------------------------------------------------------------------

float RowWidth() {
    // 行区域宽度减去两侧内边距 (行内时 WindowPadding.x 已含行内边距)
    return g_row.areaWidth - Shadow::GetStyle().WindowPadding.x * 2.f;
}

float RowLeft() { return g_row.start.x; }

float RowRight() { return RowLeft() + RowWidth(); }

void RowLabel(std::string_view label) {
    Shadow::PushTextOutline();
    Shadow::GetWindowDrawList()->AddText(
        {Shadow::g_Ctx.Cursor.x, Shadow::g_Ctx.Cursor.y + Shadow::GetStyle().FramePadding.y},
        Shadow::GetStyle().Colors[Shadow::GuiCol_Text], DisplayName(label));
    Shadow::PopTextOutline();
}

void SetRowControlX(float widthFromRight) {
    Shadow::g_Ctx.Cursor.x =
        g_row.start.x + RowWidth() - widthFromRight - kRowPaddingX;
}

// ---------------------------------------------------------------------------
// 控件
// ---------------------------------------------------------------------------

bool Switch(std::string_view label, bool* value) {
    BeginRow();
    RowLabel(label);

    const float switchWidth = (Shadow::g_Ctx.ItemHeight - 4.f) * 2.f + 4.f;
    SetRowControlX(switchWidth + 10.f); // 抵消 Shadow::Switch 的无文本间距补偿

    const bool ret = Shadow::Switch(HiddenId(label), value);
    EndRow();
    return ret;
}

void Slider(std::string_view label, float* value, float minVal, float maxVal, float step) {
    BeginRow();
    RowLabel(label);

    // 数值框宽度按 min/max 文本最大宽度自适应
    int precision = 3;
    if (step > 0.f) {
        precision = 0;
        for (float t = step; t < 0.999f && precision < 5; t *= 10.f) ++precision;
    }
    const std::string fmtStr = std::format("{{:.{}f}}", precision);
    const float maxTextW = std::max(Shadow::MeasureTextSize(std::vformat(fmtStr, std::make_format_args(minVal))).x,
                                    Shadow::MeasureTextSize(std::vformat(fmtStr, std::make_format_args(maxVal))).x);
    const float valBoxW = std::max(45.f, maxTextW + Shadow::GetStyle().FramePadding.x * 2.f);

    const float sliderW = RowWidth() * 0.40f;
    const float rightEdge = g_row.start.x + RowWidth() - kRowPaddingX;
    Shadow::g_Ctx.Cursor.x = rightEdge - valBoxW - Shadow::GetStyle().ItemSpacing.x - sliderW;

    Shadow::Slider(HiddenId(label), value, minVal, maxVal, step,
                   Shadow::ShadowSliderFlags_NoRightAlign | Shadow::ShadowSliderFlags_NoText,
                   {sliderW, 0.f});
    EndRow();
}

bool SliderInt(std::string_view label, int* value, int minVal, int maxVal) {
    float tmp = static_cast<float>(*value);
    Slider(label, &tmp, static_cast<float>(minVal), static_cast<float>(maxVal), 1.f);
    const int rounded = static_cast<int>(tmp + (tmp >= 0.f ? 0.5f : -0.5f));
    if (rounded == *value) return false;
    *value = std::clamp(rounded, minVal, maxVal);
    return true;
}

bool Combo(std::string_view label, int* currentIndex, const std::vector<std::string>& items) {
    BeginRow();
    RowLabel(label);

    const float comboW = RowWidth() * 0.40f;
    SetRowControlX(comboW);

    const bool changed = Shadow::Combo(HiddenId(label), currentIndex, items,
                                       Shadow::ShadowComboFlags_NoText |
                                       Shadow::ShadowComboFlags_NoRightAlign,
                                       {comboW, 0.f});
    EndRow();
    return changed;
}

void ColorPicker(std::string_view label, float rgba[4]) {
    BeginRow();
    RowLabel(label);

    const float cpWidth = Shadow::g_Ctx.ItemHeight;
    SetRowControlX(cpWidth);

    Shadow::ColorPicker(HiddenId(label), reinterpret_cast<Shadow::Color*>(rgba),
                        Shadow::ShadowColorPickerFlags_NoText |
                        Shadow::ShadowColorPickerFlags_NoRightAlign,
                        {cpWidth, 0.f});
    EndRow();
}

bool Button(std::string_view label) {
    BeginRow();
    const bool ret = Shadow::Button(label);
    EndRow();
    return ret;
}

bool ButtonFull(std::string_view label) {
    BeginRow();
    const bool ret = Shadow::Button(std::string(label), {RowWidth(), 0.f});
    EndRow();
    return ret;
}

void HotKey(std::string_view label, int* hotkey) {
    BeginRow();
    RowLabel(label);

    const bool assigning = (Shadow::g_Ctx.AssigningHotkey == hotkey);
    const std::string keyName =
        assigning ? "[按键]" : std::format("[{}]", Shadow::GetKeyName(*hotkey));
    const float btnWidth =
        Shadow::MeasureTextSize(keyName).x + Shadow::GetStyle().FramePadding.x * 2.f;
    SetRowControlX(btnWidth);

    Shadow::HotKey(HiddenId(label), hotkey,
                   Shadow::ShadowHotkeyFlags_NoText | Shadow::ShadowHotkeyFlags_NoRightAlign);
    EndRow();
}

bool InputText(std::string_view label, std::string& text) {
    BeginRow();
    RowLabel(label);

    const float inputW = RowWidth() * 0.40f;
    SetRowControlX(inputW);

    const bool ret = Shadow::InputText(HiddenId(label), text,
                                       Shadow::ShadowInputTextFlags_NoName, {inputW, 0.f});
    EndRow();
    return ret;
}

bool InputFloat(std::string_view label, float* value) {
    BeginRow();
    RowLabel(label);

    const float inputW = RowWidth() * 0.40f;
    SetRowControlX(inputW);

    const bool ret = Shadow::InputFloat(HiddenId(label), value, 0.f, 0.f, "{:.2f}",
                                        Shadow::ShadowInputTextFlags_NoName, {inputW, 0.f});
    EndRow();
    return ret;
}

bool InputInt(std::string_view label, int* value, int step) {
    // Shadow 只提供 InputFloat, 这里用整数格式串渲染后四舍五入回整数。
    float tmp = static_cast<float>(*value);
    BeginRow();
    RowLabel(label);

    const float inputW = RowWidth() * 0.40f;
    SetRowControlX(inputW);

    const bool changed = Shadow::InputFloat(HiddenId(label), &tmp,
                                            static_cast<float>(step), static_cast<float>(step) * 10.f,
                                            "{:.0f}", Shadow::ShadowInputTextFlags_NoName, {inputW, 0.f});
    EndRow();

    if (!changed) return false;
    const int rounded = static_cast<int>(tmp + (tmp >= 0.f ? 0.5f : -0.5f));
    if (rounded == *value) return false;
    *value = rounded;
    return true;
}

void ProgressBar(float fraction) {
    BeginRow();

    const float width = RowWidth();
    const float height = Shadow::g_Ctx.ItemHeight * 0.55f;
    const Shadow::Vec2 pos{Shadow::g_Ctx.Cursor.x,
                           Shadow::g_Ctx.Cursor.y + Shadow::GetStyle().FramePadding.y};

    auto* dl = Shadow::GetWindowDrawList();
    dl->AddRectFilled(pos, {width, height}, Shadow::GetStyle().Colors[Shadow::GuiCol_FrameBg]);

    const float filled = std::clamp(fraction, 0.f, 1.f);
    if (filled > 0.f)
        dl->AddRectFilled(pos, {width * filled, height},
                          Shadow::GetStyle().Colors[Shadow::GuiCol_SliderGrab]);

    Shadow::Dummy({width, height});
    EndRow();
}

void TextDesc(std::string_view text) {
    BeginRow();
    Shadow::PushTextWrapPos(RowWidth() - 24.f);
    Shadow::TextWrapped(Shadow::GetStyle().Colors[Shadow::GuiCol_TextDisabled], text);
    Shadow::PopTextWrapPos();
    EndRow();
}

void Text(std::string_view text) {
    BeginRow();
    Shadow::Text(text);
    EndRow();
}

void Separator() {
    Shadow::g_Ctx.Cursor.y += 4.f;
    Shadow::Separator();
    Shadow::g_Ctx.Cursor.y += 4.f;
}

bool ValueChanged(float& value, std::string_view key) {
    struct LastValues {
        std::unordered_map<std::string_view, float> map;
        unsigned long long lastSave = 0;
    };
    static LastValues s_state;

    auto [it, inserted] = s_state.map.try_emplace(key, value);
    if (inserted || it->second == value) return false;
    it->second = value;

    // 配置落盘节流 (最多每 2 秒一次)
    if (const auto now = GetTickCount64(); now - s_state.lastSave >= 2000) {
        s_state.lastSave = now;
        Config::Save("config.json");
    }
    return true;
}

} // namespace pal::ui
