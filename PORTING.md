# PalEngine 移植规范

从 `D:\My Projects\PalworldInternal-main` 移植功能代码到本仓库时, 必须遵守:

## 硬性要求

1. **现代 C++23**: 禁止 printf/fprintf/va_list/C 风格可变参数; 日志一律用 `pal::log` 的
   `std::format` 模板接口。字符串用 `std::string/std::string_view`; 集合用 STL 容器;
   格式化用 `std::format`。禁止 C 数组传参风格的新增代码 (原有 float[4] 颜色字段保持兼容)。
2. **禁止 ImGui**: 原工程的 ImGui 调用按下面的映射表替换。
3. **SEH 安全边界**: 访问 SDK 指针的代码段用 `Helper::Try([&]{...})` 包裹 (声明在
   `src/engine/GameHelper.hpp`), 指针用 `Helper::IsProbablyValidPtr()` 校验。保留原有语义。
4. **不做行为改动**: 功能语义、偏移、调用顺序与原工程保持一致; 只改表达方式。

## 日志 (pal::log, 头文件 src/core/Log.hpp)

```cpp
#include "core/Log.hpp"          // 命名空间 pal::log
log::Info("加载完成: {} 条", n);          // std::format 语法
log::Warn("..."); log::Error("...");
log::Debug("...");
log::Log(pal::log::Level::Info, "esp", "...");        // 带模块 tag
log::Throttled(pal::log::Level::Debug, "esp", "KEY",  // 节流: key + 间隔毫秒
               1000, "每秒最多一次 {}", x);
```
原 `printf(...)` → `log::Debug("...")`(format 串改 `{}`); `CaptureLog("[CAP] %s", x)` →
`log::Log(Level::Debug, "cap", "[CAP] {}", x)`; `CaptureLogThrottled(tag,ms,fmt,...)` →
`log::Throttled(Level::Debug, tag, tag, ms, ...)`。

## 绘制 (pal::gfx, 头文件 src/core/Gfx.hpp)

功能模块**不得直接 include Shadow-Gui**。世界覆盖层绘制 (ESP/小地图/路径等) 用:

```cpp
#include "core/Gfx.hpp"          // 命名空间 pal::gfx
gfx::Vec2{x, y};  gfx::Color{r, g, b, a};    // 0..1 浮点
gfx::Overlay()                    // 后台画布 ShadowDrawList* (有 AddLine/AddRect 等)
gfx::Line(a, b, c, thickness);
gfx::Rect(pos, size, c, t);  gfx::RectFilled(pos, size, c);
gfx::RectMinMax(min, max, c, t);  gfx::RectFilledMinMax(min, max, c);  // 注意: ImGui 用 min/max, Shadow 用 pos/size!
gfx::CircleFilled(center, r, c);  gfx::TriangleFilled(...);  gfx::Text(pos, c, s);
gfx::TextSize(s);  gfx::ScreenWidth/ScreenHeight/ScreenSize();
```
替换映射: `ImGui::GetBackgroundDrawList()->AddLine(...)` → `gfx::Line(...)`(直接传参);
`ImGui::CalcTextSize` → `gfx::TextSize`; `ImVec2` → `pal::gfx::Vec2`;
`IM_COL32(r,g,b,a)` → `pal::gfx::Color{r/255.f, g/255.f, b/255.f, a/255.f}`(常量直接算好);
`ImU32` → `pal::gfx::Color`; `ImGui::GetIO().DisplaySize` → `gfx::ScreenSize()`。

**注意语义差异**: Shadow 的 `AddRect(pos,size)`/`AddRectFilled(pos,size)` 第二参数是尺寸,
不是 ImGui 的 max 角点。凡原代码传 (min,max) 两个点的, 必须改用 `gfx::RectMinMax` /
`gfx::RectFilledMinMax`, 或改为 `{x, y, w, h}` 尺寸形式。Shadow 的 AddRectFilled 无圆角参数。
Shadow 无 AddCircle 描边圆 (只有 AddCircleFilled), 需要描边圆时用分段 Line。

## 输入 (pal::core::input, 头文件 src/core/Input.hpp)

`input::IsMenuOpen()` / `input::ConsumeWheelDelta()`(读后清零, 格数, 正=上滚)。
鼠标位置: UI 内部代码可用 `Shadow::g_Ctx.MousePos`(仅 ui 层); 功能层如需光标位置,
用 SDK: `GetPalPlayerController()->GetMousePosition()` 或继续用原有实现方式, 并在 PR 说明。
`GetAsyncKeyState` 轮询保留原语义。

## 每帧入口

- `features::TickGameFrame(dt)` — 逻辑 tick (游戏线程, 无绘制)
- `features::DrawOverlays()` — 覆盖层绘制 (Shadow NewFrame 与 Render 之间)
- 原 `Menu::Draw`/`Menu::Loops` 中的 tick 全部并入上述两处, 不再开后台线程。

## UI 面板函数 (DrawXxxPanel)

原工程里用 ImGui 画的面板 (如 DrawFreeRidePanel/DrawGodHandPanel) 不再由功能层负责,
功能层只保留逻辑与 tick; UI 由 `src/ui` 的 Shadow-Gui 重新实现。移植时删除面板函数,
并在头文件去掉相应声明 (调用点在 ui 层重写)。

## 目录与命名

- core/ 框架层, engine/ SDK 访问层 (namespace Helper), features/ 功能逻辑, ui/ Shadow-Gui 菜单
- 头文件 .hpp, 与 .cpp 同目录; include 用项目相对路径 (`#include "core/Gfx.hpp"`)
- 文件内可 `using namespace SDK; using namespace Helper;` (仅 cpp)
- pch 已含 Windows/STL/MinHook/SDK.hpp, 不要重复 include
