#include <pch.h>
#include "Widgets.hpp"
#include "Theme.hpp"
#include "engine/ConfigManager.hpp"

#include <ShadowGui/Shadow.h>

namespace pal::ui {

namespace {

struct RowState {
    bool  joinNext = false;
    float basePadX = 0.f;       // 行区域 -> 卡片 的外边距 (进入行之前的 WindowPadding.x)
    Shadow::Vec2 cardPos{};     // 卡片矩形左上角
    float cardW = 0.f;          // 卡片宽度
    Shadow::Vec2 start{};       // 内容区左上角 (标签/控件从这开始)
    float contentW = 0.f;       // 内容区宽度
    size_t cmdIndex = 0;
    float areaLeft  = 0.f;      // 行区域左缘 x (由 SetRowArea 设置)
    float areaWidth = 0.f;      // 行区域宽度
};
RowState g_row;

constexpr float kRowPaddingX = 16.f;  // 卡片内左右内边距 (文字不能贴着描边)
constexpr float kRowPaddingY = 11.f;  // 卡片内上下内边距
constexpr float kRowRadius   = theme::kRadiusRow; // 行卡片圆角 (EVICTED ChildRounding = 7)

std::string HiddenId(std::string_view label) { return std::format("##{}", label); }

std::string_view DisplayName(std::string_view label) {
    const size_t pos = label.find("##");
    return pos == std::string_view::npos ? label : label.substr(0, pos);
}

// 构造一条绘制命令 (脱离库公开 API: 公开 AddRect* 会跟随 TextureStack, 把卡片装饰
// 变成贴图)。clipping 取当前上下文, 与行内容保持一致。
Shadow::ShadowDrawCmd MakeCmd(Shadow::ShadowDrawCmdType type, Shadow::Vec2 pos, Shadow::Vec2 size,
                              Shadow::Color color, float rounding = 0.f,
                              Shadow::Color color2 = {}, float thickness = 1.f) {
    Shadow::ShadowDrawCmd c{};
    c.type = type;
    c.pos = pos;
    c.size = size;
    c.color = color;
    c.color2 = color2;
    c.thickness = thickness;
    c.rounding = rounding;
    c.texture = nullptr;
    c.clippingEnabled = Shadow::g_Ctx.ClippingEnabled;
    c.clipMin = Shadow::g_Ctx.ClipMin;
    c.clipMax = Shadow::g_Ctx.ClipMax;
    return c;
}

// 分组标题/正文使用更小字号时, 安全地压入缩放 (默认字体缺失则跳过)
void PushUiFont(float scale) {
    if (Shadow::DefaultFont) Shadow::PushFont(Shadow::DefaultFont, scale);
}
void PopUiFont() {
    if (Shadow::DefaultFont) Shadow::PopFont();
}

// 主强调按钮的渐变底 (紫 -> 蓝, EVICTED 的强调色组合); ghost = 深色底 + 描边次级按钮
void DrawAccentPlate(Shadow::Vec2 pos, Shadow::Vec2 size, bool hovered, bool ghost, float radius) {
    auto* dl = Shadow::GetWindowDrawList();
    if (ghost) {
        dl->AddRectFilledRounded(pos, size, radius, theme::A(theme::Surface));
        dl->AddRectRounded(pos, size, radius, theme::A(theme::Border), 1.f);
        return;
    }
    const Shadow::Color c1 = theme::A(hovered ? theme::Rgb(0xD65CFF) : theme::Accent);
    const Shadow::Color c2 = theme::A(hovered ? theme::Rgb(0x6478DF) : theme::AccentBlue);
    dl->AddRectFilledGradientRounded(pos, size, radius, c1, c2, false);
}

// 用库内 Button 做交互 (命中/点击/光标推进), 但把它自带的底色与文字压成透明,
// 由上层自行绘制渐变/描边与文字。返回是否被点击。
// 注意: 调用者不应对同一区域再调用 Dummy, Button 自身会推进光标。
bool TransparentButton(std::string_view name, Shadow::Vec2 pos, Shadow::Vec2 size, bool hovered, bool ghost) {
    auto* dl = Shadow::GetWindowDrawList();

    DrawAccentPlate(pos, size, hovered, ghost, theme::kRadiusInput);
    const std::string_view text = DisplayName(name);
    const Shadow::Vec2 ts = Shadow::MeasureTextSize(text);
    dl->AddText({ pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f },
                theme::A(ghost ? theme::Text1 : theme::Rgb(0xFFFFFF)), text);

    Shadow::PushStyleColor(Shadow::GuiCol_Text,          Shadow::Color{ 0.f, 0.f, 0.f, 0.f });
    Shadow::PushStyleColor(Shadow::GuiCol_Button,        Shadow::Color{ 0.f, 0.f, 0.f, 0.f });
    Shadow::PushStyleColor(Shadow::GuiCol_ButtonHovered, Shadow::Color{ 0.f, 0.f, 0.f, 0.f });
    Shadow::PushStyleColor(Shadow::GuiCol_Border,        Shadow::Color{ 0.f, 0.f, 0.f, 0.f });
    const bool clicked = Shadow::Button(name, size);
    Shadow::PopStyleColor(4);
    return clicked;
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
    g_row.basePadX = style.WindowPadding.x;

    // 行区域 -> 卡片: 左右各留 basePadX; 卡片 -> 内容: 再留 kRowPaddingX。
    // 上一版内容起点与卡片左缘重合, 白卡片上文字紧贴描边, 看着很挤。
    const float startY = ctx.Cursor.y;
    g_row.cardPos = { g_row.areaLeft + g_row.basePadX, startY };
    g_row.cardW   = std::max(0.f, g_row.areaWidth - g_row.basePadX * 2.f);
    g_row.start   = { g_row.cardPos.x + kRowPaddingX, startY };
    g_row.contentW = std::max(0.f, g_row.cardW - kRowPaddingX * 2.f);

    // 行内抬高 WindowPadding.x, 让库内控件的右侧对齐也跟着内缩到卡片里
    style.WindowPadding.x = g_row.basePadX + kRowPaddingX;
    ctx.Cursor = { g_row.start.x, startY + kRowPaddingY };
}

void EndRow() {
    auto& ctx = Shadow::g_Ctx;
    auto& style = Shadow::GetStyle();

    ctx.Cursor.y -= style.ItemSpacing.y;
    ctx.Cursor.y += kRowPaddingY;

    const float height = ctx.Cursor.y - g_row.cardPos.y;
    const Shadow::Vec2 cardPos = g_row.cardPos;
    const Shadow::Vec2 cardSize{ g_row.cardW, height };

    // EVICTED 风格卡片装饰: 底 -> accent 微描边 -> 左右竖条 -> 上下 accent 渐变光条。
    // 行高只有 EndRow 时才知道, 因此整块命令在行内容之前插入 (行内容绘制在其上)。
    const Shadow::Color accent = theme::A(theme::Accent);
    const Shadow::Color accentFade = theme::A(theme::WithAlpha(theme::Accent, 0.f));
    const Shadow::Color accentGlow = theme::A(theme::WithAlpha(theme::Accent, 0.5f));

    std::vector<Shadow::ShadowDrawCmd> deco;
    deco.reserve(7);

    // 卡片底
    deco.push_back(MakeCmd(Shadow::ShadowDrawCmdType::RectFilled, cardPos, cardSize,
                           theme::A(theme::Surface), kRowRadius));
    // accent 微描边
    deco.push_back(MakeCmd(Shadow::ShadowDrawCmdType::Rect, cardPos, cardSize,
                           theme::A(theme::WithAlpha(theme::Accent, 0.14f)), kRowRadius,
                           {}, 2.f));
    // 左右竖条
    const float barH = std::max(0.f, height - 14.f);
    deco.push_back(MakeCmd(Shadow::ShadowDrawCmdType::RectFilled,
                           { cardPos.x, cardPos.y + 7.f }, { 4.f, barH },
                           theme::A(theme::CardLine), 10.f));
    deco.push_back(MakeCmd(Shadow::ShadowDrawCmdType::RectFilled,
                           { cardPos.x + cardSize.x - 4.f, cardPos.y + 7.f }, { 4.f, barH },
                           theme::A(theme::CardLine), 10.f));
    // 上下 accent 渐变光条 (两端淡出)
    const float halfW = cardSize.x * 0.5f - 8.f;
    if (halfW > 0.f) {
        for (const float y : { cardPos.y, cardPos.y + cardSize.y - 3.f }) {
            deco.push_back(MakeCmd(Shadow::ShadowDrawCmdType::RectFilledGradient,
                                   { cardPos.x + 8.f, y }, { halfW, 3.f },
                                   accentFade, 2.f, accentGlow));
            deco.push_back(MakeCmd(Shadow::ShadowDrawCmdType::RectFilledGradient,
                                   { cardPos.x + 8.f + halfW, y }, { halfW, 3.f },
                                   accentGlow, 2.f, accentFade));
        }
    }

    auto& cmds = Shadow::GetWindowDrawList()->CmdBuffer;
    cmds.insert(cmds.begin() + static_cast<ptrdiff_t>(g_row.cmdIndex),
                deco.begin(), deco.end());

    style.WindowPadding.x = g_row.basePadX;
    ctx.Cursor.x = g_row.start.x;
    ctx.Cursor.y += style.ItemSpacing.y;
}

void JoinNext() { g_row.joinNext = true; }

void SetRowArea(float leftX, float width) {
    g_row.areaLeft = leftX;
    g_row.areaWidth = width;
}

void BeginPanel(std::string_view title) {
    auto& ctx = Shadow::g_Ctx;
    auto& style = Shadow::GetStyle();
    auto* dl = Shadow::GetWindowDrawList();

    if (g_row.areaWidth <= 0.f) return; // 尚未 SetRowArea

    const float left  = g_row.areaLeft + style.WindowPadding.x;
    const float right = g_row.areaLeft + g_row.areaWidth - style.WindowPadding.x - 6.f;

    ctx.Cursor.x = left;
    ctx.Cursor.y += 8.f;

    if (!title.empty()) {
        PushUiFont(theme::kFontGroup);
        const Shadow::Vec2 ts = Shadow::MeasureTextSize(title);
        dl->AddText({ left, ctx.Cursor.y }, theme::A(theme::Text2), title);

        // 分组标题右侧虚线
        const float lineY = ctx.Cursor.y + ts.y * 0.5f;
        for (float x = left + ts.x + 10.f; x < right; x += 7.f) {
            const float w = std::min(4.f, right - x);
            if (w <= 0.f) break;
            dl->AddRectFilled({ x, lineY }, { w, 1.f }, theme::A(theme::WithAlpha(theme::Border, 0.95f)));
        }
        PopUiFont();
        ctx.Cursor.y += ts.y + 8.f;
    }
}

void EndPanel() { Shadow::g_Ctx.Cursor.y += 12.f; }

// ---------------------------------------------------------------------------
// 行内工具
// ---------------------------------------------------------------------------

// 以下三个统一描述"卡片内容区": 标签/控件都只能在这个范围内摆放,
// 右侧对齐也因此自动内缩到卡片描边以内 (而不是贴着窗口边缘)。
float RowWidth() { return g_row.contentW; }

float RowLeft() { return g_row.start.x; }

float RowRight() { return g_row.start.x + g_row.contentW; }

void RowLabel(std::string_view label) {
    auto& ctx = Shadow::g_Ctx;
    const Shadow::Vec2 ts = Shadow::MeasureTextSize(label);
    // 行高 = 2*内边距 + ItemHeight, 故标签垂直居中于 ItemHeight 区间
    Shadow::GetWindowDrawList()->AddText(
        { ctx.Cursor.x, ctx.Cursor.y + (ctx.ItemHeight - ts.y) * 0.5f },
        theme::A(theme::Text1), DisplayName(label));
}

void SetRowControlX(float widthFromRight) {
    Shadow::g_Ctx.Cursor.x = RowRight() - widthFromRight;
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
    const float rightEdge = RowRight();
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

bool ButtonPrimary(std::string_view label) {
    BeginRow();
    const float w = Shadow::MeasureTextSize(DisplayName(label)).x + 36.f;
    const float h = Shadow::g_Ctx.ItemHeight + 6.f;
    auto& ctx = Shadow::g_Ctx;
    ctx.Cursor.x = RowRight() - w;
    const Shadow::Vec2 pos = ctx.Cursor;
    const bool hovered = Shadow::IsMouseHovering(pos, { w, h });
    const bool ret = TransparentButton(label, pos, { w, h }, hovered, false);
    EndRow();
    return ret;
}

bool ButtonPrimaryFull(std::string_view label) {
    BeginRow();
    auto& ctx = Shadow::g_Ctx;
    const float w = RowWidth();
    const float h = ctx.ItemHeight + 6.f;
    const Shadow::Vec2 pos = ctx.Cursor;
    const bool hovered = Shadow::IsMouseHovering(pos, { w, h });
    const bool ret = TransparentButton(label, pos, { w, h }, hovered, false);
    EndRow();
    return ret;
}

bool ButtonPrimaryAt(Shadow::Vec2 pos, Shadow::Vec2 size, std::string_view label, bool ghost) {
    const bool hovered = Shadow::IsMouseHovering(pos, size);
    return TransparentButton(label, pos, size, hovered, ghost);
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
    dl->AddRectFilledRounded(pos, {width, height}, height * 0.5f, theme::A(theme::Surface2));

    const float filled = std::clamp(fraction, 0.f, 1.f);
    if (filled > 0.f)
        dl->AddRectFilledRounded(pos, {width * filled, height}, height * 0.5f, theme::A(theme::Accent));

    Shadow::Dummy({width, height});
    EndRow();
}

void TextDesc(std::string_view text) {
    auto& ctx = Shadow::g_Ctx;
    auto& style = Shadow::GetStyle();
    if (g_row.areaWidth <= 0.f) return;

    ctx.Cursor.x = g_row.areaLeft + style.WindowPadding.x;
    ctx.Cursor.y += 3.f;
    Shadow::PushTextWrapPos(g_row.areaLeft + g_row.areaWidth - style.WindowPadding.x - 10.f);
    Shadow::TextWrapped(theme::A(theme::Text3), text);
    Shadow::PopTextWrapPos();
    ctx.Cursor.y += 6.f;
}

void Text(std::string_view text) {
    auto& ctx = Shadow::g_Ctx;
    auto& style = Shadow::GetStyle();
    if (g_row.areaWidth <= 0.f) return;

    ctx.Cursor.x = g_row.areaLeft + style.WindowPadding.x;
    ctx.Cursor.y += 3.f;
    Shadow::TextColored(theme::A(theme::Text2), text);
    ctx.Cursor.y += 6.f;
}

void Separator() {
    auto& ctx = Shadow::g_Ctx;
    auto& style = Shadow::GetStyle();
    ctx.Cursor.y += 4.f;
    Shadow::GetWindowDrawList()->AddRectFilled(
        { g_row.areaLeft + style.WindowPadding.x, ctx.Cursor.y },
        { std::max(0.f, g_row.areaWidth - style.WindowPadding.x * 2.f), 1.f },
        theme::A(theme::Border));
    ctx.Cursor.x = g_row.areaLeft + style.WindowPadding.x;
    ctx.Cursor.y += 5.f;
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
