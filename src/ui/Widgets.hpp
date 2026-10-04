#pragma once
// ui/Widgets.hpp — 行式控件封装。
// 统一观感: 每个控件占一行卡片, 左侧标签 + 右侧交互控件, 支持卡片上下拼接。
// 基于 Shadow-Gui 公开 API 与上下文 (g_Ctx) 实现, 供各 Tab 使用。

#include <pch.h>
#include <ShadowGui/Shadow.h>

namespace pal::ui {

// ---- 行/卡片布局 ----
void SetRowArea(float leftX, float width); // 设置行区域 (内容区左缘与宽度), 每帧绘制内容前调用
void BeginRow();                          // 开始一行 (记录起始位置与绘制命令插入点)
void EndRow();                            // 结束一行 (补画行底色)
void JoinNext();                          // 下一行与上一行底色无缝拼接 (形成卡片组)
void BeginPanel(std::string_view title);  // 分区标题
void EndPanel();

// ---- 控件 (均按"整行"消费) ----
bool Switch(std::string_view label, bool* value);
void Slider(std::string_view label, float* value, float minVal, float maxVal, float step = 0.f);
bool SliderInt(std::string_view label, int* value, int minVal, int maxVal); // 返回是否变更
bool Combo(std::string_view label, int* currentIndex, const std::vector<std::string>& items);
void ColorPicker(std::string_view label, float rgba[4]);       // Shadow::Color 布局兼容
bool Button(std::string_view label);                           // 按内容自适应宽度
bool ButtonFull(std::string_view label);                       // 占满整行
void HotKey(std::string_view label, int* hotkey);
bool InputText(std::string_view label, std::string& text);
bool InputFloat(std::string_view label, float* value);
bool InputInt(std::string_view label, int* value, int step = 1); // 整数输入框 (返回是否变更)
void ProgressBar(float fraction);                              // 进度条 (0..1)
void TextDesc(std::string_view text);                          // 次要说明文字 (整行)
void Text(std::string_view text);
void Separator();

// 行内小工具 (供特殊布局的 Tab 直接使用)
void RowLabel(std::string_view label);
float RowWidth();                        // 当前行可用宽度
float RowLeft();                         // 行左缘 X (内容坐标系)
float RowRight();                        // 行右缘 X (内容坐标系)
void SetRowControlX(float widthFromRight); // 从行右缘向左定位交互控件

// 浮点值变更检测: 与上一帧值对比, 变化时返回 true 并节流请求配置落盘。
// key 必须是稳定的调用点标识 (用于区分同字段的多处引用)。
bool ValueChanged(float& value, std::string_view key);

} // namespace pal::ui
