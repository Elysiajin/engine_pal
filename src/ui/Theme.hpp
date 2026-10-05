#pragma once
// ui/Theme.hpp — 「EVICTED」深色紫调 设计规范 token。
//
// 参考 D:/My Projects/imgui_menu/Misterio evicted dx9/recode saphire (colors.h / main.cpp):
//   深灰窗口底 #15151A + 深紫侧栏 #23202D + 紫强调 #BC00FF + 蓝紫选中 #4F66D1/#32428A。
//
// 本文件同时是 UI 层的"设计 token 单一来源":
//   - theme:: 命名空间下的常量供 Menu / Widgets 直接使用;
//   - ApplyTheme() 把它们写进 Shadow-Gui 的全局样式。

#include <pch.h>
#include <ShadowGui/Shadow.h>

namespace pal::ui {

// ---------------------------------------------------------------------------
// 配色 token (取色自 EVICTED / recode saphire)
// ---------------------------------------------------------------------------
namespace theme {

// 0xRRGGBB + alpha -> Shadow::Color (0..1)
constexpr Shadow::Color Rgb(unsigned rgb, float a = 1.0f) {
    return Shadow::Color{ static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                          static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(rgb & 0xFF) / 255.0f,
                          a };
}
constexpr Shadow::Color WithAlpha(Shadow::Color c, float a) { return Shadow::Color{ c.r, c.g, c.b, a }; }

// 自绘元素一律用它取色: 只有经过 Shadow::GetColor 才能吃到框架的全局 Alpha
// (菜单淡入淡出) 与禁用变暗。直接用常量会绕过这一层, 结果是淡入时只有一半 UI 在渐变。
inline Shadow::Color A(Shadow::Color c) { return Shadow::GetColor(c); }

// ---- 面板层 ----
inline constexpr unsigned kWindowHex    = 0x15151A; // 窗口内容底  (21,21,26)
inline constexpr unsigned kSidebarHex   = 0x23202D; // 侧栏面板    (35,32,45)
inline constexpr unsigned kSurfaceHex   = 0x1A1A20; // 卡片底 (EVICTED 21,21,23 略提亮以便与窗口底区分)
inline constexpr unsigned kSurface2Hex  = 0x1E1E22; // 内嵌控件底 / 滑块轨道
inline constexpr unsigned kPopupHex     = 0x111015; // 弹层底      (17,16,21)
inline constexpr unsigned kBorderHex    = 0x27262D; // 窗口外框 / 卡片描边 (39,38,45)
inline constexpr unsigned kCardLineHex  = 0x16151A; // 卡片左右竖条 (22,21,26)
inline constexpr unsigned kSeparatorHex = 0x24232A; // 分割线

// ---- 文本 ----
inline constexpr unsigned kText1Hex   = 0xE9E9EE; // 主文字
inline constexpr unsigned kText2Hex   = 0x9A9CAB; // 次要文字
inline constexpr unsigned kText3Hex   = 0x6A6C7C; // 说明文字
inline constexpr unsigned kTextDimHex = 0x4D5061; // 禁用 / 分组标题 (77,80,97)

// ---- 强调 ----
inline constexpr unsigned kAccentHex     = 0xBC00FF; // 主强调 (紫, 188,0,255)
inline constexpr unsigned kAccentBlueHex = 0x4F66D1; // 次强调 (蓝, 79,102,209)
inline constexpr unsigned kAccentDeepHex = 0x32428A; // 蓝渐变终点 (50,66,138)
inline constexpr unsigned kMintHex       = 0x4ADE80; // 成功 / 在线
inline constexpr unsigned kWarningHex    = 0xFDC177; // 注意
inline constexpr unsigned kDangerHex     = 0xF27A7A; // 危险

inline constexpr Shadow::Color BgBase        = Rgb(kWindowHex);
inline constexpr Shadow::Color BgSoft        = Rgb(kSidebarHex);
inline constexpr Shadow::Color Surface       = Rgb(kSurfaceHex);
inline constexpr Shadow::Color Surface2      = Rgb(kSurface2Hex);
inline constexpr Shadow::Color Popup         = Rgb(kPopupHex);
inline constexpr Shadow::Color Border        = Rgb(kBorderHex);
inline constexpr Shadow::Color CardLine      = Rgb(kCardLineHex);
inline constexpr Shadow::Color SeparatorCol  = Rgb(kSeparatorHex);
inline constexpr Shadow::Color Text1         = Rgb(kText1Hex);
inline constexpr Shadow::Color Text2         = Rgb(kText2Hex);
inline constexpr Shadow::Color Text3         = Rgb(kText3Hex);
inline constexpr Shadow::Color TextDim       = Rgb(kTextDimHex);

inline constexpr Shadow::Color Accent     = Rgb(kAccentHex);
inline constexpr Shadow::Color AccentBlue = Rgb(kAccentBlueHex);
inline constexpr Shadow::Color AccentDeep = Rgb(kAccentDeepHex);
inline constexpr Shadow::Color Mint       = Rgb(kMintHex);
inline constexpr Shadow::Color Warning    = Rgb(kWarningHex);
inline constexpr Shadow::Color Danger     = Rgb(kDangerHex);

// 兼容旧 token 名 (历史引用): Pink = 主强调, Purple = 次强调
inline constexpr Shadow::Color Pink   = Accent;
inline constexpr Shadow::Color Purple = AccentBlue;

// 深色主题的投影用纯黑
inline constexpr Shadow::Color ShadowTint = Rgb(0x000000);

// ---------------------------------------------------------------------------
// 圆角 / 间距 / 字号 (圆角对齐 EVICTED: window 8 / child 7 / popup 5 / frame 4)
// ---------------------------------------------------------------------------
inline constexpr float kRadiusTag    = 3.f;   // 标签
inline constexpr float kRadiusInput  = 4.f;   // 输入框 / 按钮
inline constexpr float kRadiusCard   = 8.f;   // 侧栏等大面板
inline constexpr float kRadiusRow    = 7.f;   // 行卡片 (EVICTED ChildRounding)
inline constexpr float kRadiusWindow = 8.f;   // 窗口
inline constexpr float kRadiusPill   = Shadow::ShadowRoundingMax; // 胶囊 (99px)

inline constexpr float kGapXs = 4.f;
inline constexpr float kGapSm = 8.f;
inline constexpr float kGapMd = 12.f;
inline constexpr float kGapLg = 16.f;
inline constexpr float kGapXl = 20.f;

inline constexpr float kFontRow   = 1.00f;  // 13px 功能行
inline constexpr float kFontTitle = 1.18f;  // 16px 页面标题
inline constexpr float kFontGroup = 0.88f;  // 12px 分组标题

} // namespace theme

// 一次性应用主题配色与尺寸参数 (需在 Shadow 帧内调用, 幂等)
void ApplyTheme();

// 加载字体 (系统 msyh.ttc, 失败则保持 Shadow 运行时默认字体)。幂等。
void EnsureFont();

} // namespace pal::ui
