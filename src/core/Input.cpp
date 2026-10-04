#include <pch.h>
#include "core/Input.hpp"
#include "core/Log.hpp"

#include <ShadowGui/Shadow.h>

namespace pal::core::input {

namespace {

constexpr int kDefaultMenuKey = VK_INSERT;

HWND    g_hwnd         = nullptr;
WNDPROC g_originalProc = nullptr;
int     g_menuKey      = kDefaultMenuKey;
bool    g_menuOpen     = false;
float   g_wheelDelta   = 0.0f;

// 是否独占键鼠输入。
// 菜单打开期间一律独占: 鼠标点击不能穿透到游戏 (否则点菜单会同时触发游戏交互)。
// 切换键自身的 WM_KEYDOWN/WM_KEYUP 在 HookedWndProc 里已显式放行给游戏,
// 不存在 "松开 Insert 游戏收不到 KEYUP 导致按键卡死" 的问题, 无需探测物理状态。
bool InputLocked() {
    return g_menuOpen;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;

    const LONG style = GetWindowLongPtr(hwnd, GWL_STYLE);
    if (style & (WS_CAPTION | WS_POPUP)) {
        g_hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

// 菜单打开时游戏必须收到一个"虚拟"的输入形态:
//   - WM_INPUT (Raw Input) 拦下, 否则 UE 的 FPS 摄像机绕过普通鼠标消息继续转
//     (原项目踩过的坑, 见其 WndProc 注释)
//   - 键盘/鼠标先喂 Shadow::Input, 再按白名单决定放行
LRESULT APIENTRY HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!g_originalProc)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    // 全局热键始终处理 (菜单关闭时也要响应)
    Shadow::ProcessGlobalHotkeys(hwnd, msg, wParam, lParam);

    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL)
        g_wheelDelta += GET_WHEEL_DELTA_WPARAM(wParam) / static_cast<float>(WHEEL_DELTA);

    if (msg == WM_KEYDOWN && wParam == static_cast<WPARAM>(g_menuKey) &&
        !Shadow::g_Ctx.AssigningHotkey) {
        const bool firstPress = (lParam & (1 << 30)) == 0;
        if (firstPress) {
            ToggleMenu();
            (void)GetAsyncKeyState(g_menuKey); // 清掉可能残留的按下位
            log::Info("[input] 菜单切换: {}", g_menuOpen ? "打开" : "关闭");
        }
        // 切换键本身交给游戏 (与原项目一致); 该键被按住并不影响
        // WM_INPUT 的放行逻辑, 见下面的 InputLocked()。
        return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);
    }

    // 菜单打开时仍然要把切换键喂给 Shadow (供 UI 读取按键状态),
    // 但绝不能拦下它的 WM_KEYUP —— 否则它松开后消息被吃掉, 菜单无法关闭。
    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
        if (wParam == static_cast<WPARAM>(g_menuKey)) {
            Shadow::Input(hwnd, msg, wParam, lParam);
            return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);
        }
    }

    // 鼠标消息先喂给 Shadow (菜单交互需要), 再走下面的放行/拦截判定
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
        Shadow::Input(hwnd, msg, wParam, lParam);

    if (!InputLocked())
        return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);

    // ---- 菜单打开: 独占输入 ----
    // WM_INPUT (Raw Input) 必须按鼠标按键状态放行:
    //   UE 的 FPS 视角完全依赖 Raw Input。如果无穷拦截, 鼠标会彻底失效 ——
    //   更糟的是按下 Insert 时产生的"最后一条"鼠标按键事件丢失, 游戏会认为
    //   按键始终处于按下状态 (FPS 模板里按住右键 = 锁定视角旋转), 于是出现
    //   "关掉菜单后镜头仍然一直自己转"的现象。
    //   这里只在鼠标左/右/中键按下期间拦截, 其余时刻放行 (此时菜单打开,
    //   游戏通常处于暂停/失焦 UI 状态, 不需要靠吞 Raw Input 来挡视角)。
    if (msg == WM_INPUT) {
        const bool anyMouseDown = Shadow::g_Ctx.MouseDown ||
                                  Shadow::g_Ctx.RightMouseDown ||
                                  Shadow::g_Ctx.MiddleMouseDown;
        if (anyMouseDown) return 1; // 菜单内拖动: 阻断镜头穿透
        return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_ACTIVATE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_SIZE:
    case WM_MOVE:
        // 系统消息照常转发, 保证 UE 焦点/捕获状态正常, 关菜单后可恢复
        return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);
    default:
        break;
    }

    const bool isKeyboardMsg =
        msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN ||
        msg == WM_SYSKEYUP || msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_IME_CHAR;

    if (isKeyboardMsg) {
        if (Shadow::IsHotkeyRegistered(static_cast<int>(wParam))) {
            // 全局热键: 状态已在 ProcessGlobalHotkeys 中更新, 放行
            return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);
        }
        Shadow::Input(hwnd, msg, wParam, lParam);
        if (Shadow::IsKeyAllowed(static_cast<int>(wParam)))
            return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);
        return 1; // 非白名单按键被菜单吃掉
    }

    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
        // 上面已喂过 Shadow::Input
        if (Shadow::IsMouseMsgAllowed(msg))
            return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);
        return 1;
    }

    return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);
}

} // namespace

void Install() {
    if (g_originalProc) return; // 已安装

    EnumWindows(EnumWindowsProc, 0);
    if (!g_hwnd) {
        log::Warn("[input] 未找到游戏主窗口, WndProc 钩子未安装");
        return;
    }

    g_originalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc)));
    log::Info("[input] WndProc 钩子安装成功 (hwnd={:#x})", reinterpret_cast<uintptr_t>(g_hwnd));
}

bool IsMenuOpen() { return g_menuOpen; }

void SetMenuOpen(bool open) { g_menuOpen = open; }

void ToggleMenu() { g_menuOpen = !g_menuOpen; }

void SetMenuToggleKey(int vk) { g_menuKey = vk ? vk : kDefaultMenuKey; }

int GetMenuToggleKey() { return g_menuKey; }

float ConsumeWheelDelta() {
    const float delta = g_wheelDelta;
    g_wheelDelta = 0.0f;
    return delta;
}

} // namespace pal::core::input
