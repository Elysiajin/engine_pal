#pragma once
// core/Gfx.hpp — 世界覆盖层绘制门面。
// 功能层 (features) 不直接接触 Shadow-Gui, 一律通过本门面画 ESP/小地图等
// 世界覆盖层。仅可在 FrameDriver 的 NewFrame..Render 区间内调用。

#include <pch.h>
#include <ShadowGui/Shadow.h>

namespace pal::gfx {

using Vec2  = Shadow::Vec2;   // {x, y}
using Color = Shadow::Color;  // {r, g, b, a} 0..1

// 后台画布类型别名 (功能层不直接依赖 Shadow, 需要传画布时用这两个名字)
using DrawList       = Shadow::ShadowDrawList;
using ShadowDrawList = Shadow::ShadowDrawList;

// 后台画布 (绘制在游戏 UI 之下、菜单之下)
inline Shadow::ShadowDrawList* Overlay() { return Shadow::GetBackgroundDrawList(); }

inline float ScreenWidth()  { return Shadow::GetIO().DisplaySize.x; }
inline float ScreenHeight() { return Shadow::GetIO().DisplaySize.y; }
inline Vec2  ScreenSize()   { return Shadow::GetIO().DisplaySize; }

inline Vec2 TextSize(std::string_view text) { return Shadow::MeasureTextSize(text); }

inline void Line(Vec2 a, Vec2 b, Color c, float thickness = 1.0f) {
    Overlay()->AddLine(a, b, c, thickness);
}
inline void Rect(Vec2 pos, Vec2 size, Color c, float thickness = 1.0f) {
    Overlay()->AddRect(pos, size, c, thickness);
}
inline void RectFilled(Vec2 pos, Vec2 size, Color c) {
    Overlay()->AddRectFilled(pos, size, c);
}
// 角点 (min,max) 形式, 便于从坐标对直接绘制
inline void RectMinMax(Vec2 min, Vec2 max, Color c, float thickness = 1.0f) {
    Rect(min, {max.x - min.x, max.y - min.y}, c, thickness);
}
inline void RectFilledMinMax(Vec2 min, Vec2 max, Color c) {
    RectFilled(min, {max.x - min.x, max.y - min.y}, c);
}
// segments 仅为兼容调用点的写法 (Shadow 的实心圆不需要分段数)
inline void CircleFilled(Vec2 center, float radius, Color c, int /*segments*/ = 0) {
    Overlay()->AddCircleFilled(center, radius, c);
}
inline void TriangleFilled(Vec2 p1, Vec2 p2, Vec2 p3, Color c) {
    Overlay()->AddTriangleFilled(p1, p2, p3, c);
}
inline void Text(Vec2 pos, Color c, std::string_view text) {
    Overlay()->AddText(pos, c, text);
}

// 带底色衬底的居中文本 (ESP 标签样式)
inline void TaggedText(Vec2 center, Color textColor, std::string_view text,
                       Color bg = Color{0.f, 0.f, 0.f, 0.55f}) {
    const Vec2 size = TextSize(text);
    const Vec2 pos{center.x - size.x * 0.5f, center.y};
    RectFilled({pos.x - 4.f, pos.y - 2.f}, {size.x + 8.f, size.y + 4.f}, bg);
    Text({pos.x + 1.f, pos.y + 1.f}, Color{0.f, 0.f, 0.f, 0.78f}, text);
    Text(pos, textColor, text);
}

} // namespace pal::gfx
