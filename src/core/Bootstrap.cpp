#include <pch.h>
#include "core/Bootstrap.hpp"
#include "core/Hooks.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"

#include "engine/GameHelper.hpp"
#include "features/FeatureHost.hpp"

namespace pal::core {

namespace {

// SEH 保护外壳: Bootstrap 本体含 C++ 对象 (std::string 等), 不能直接写 __try。
// 单独放一个只做调用的小函数, 保证 _sehp 能正常展开。
DWORD RunGuarded(void (*fn)()) {
    __try {
        fn();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
    return 0;
}

void BootstrapImpl();

} // namespace

void Bootstrap(HMODULE) {
    if (const DWORD code = RunGuarded(&BootstrapImpl); code != 0)
        log::Stage(std::format("Bootstrap 触发异常 code=0x{:08X}", code));
}

namespace {

void BootstrapImpl() {
    log::Stage("Bootstrap 进入");
    log::Init();
    log::Stage("日志系统就绪");
    log::Info("[boot] PalEngine 启动 (v{})", "1.0.0");

    if (MH_Initialize() != MH_OK) {
        log::Stage("MH_Initialize 失败");
        log::Error("[boot] MH_Initialize 失败, 放弃初始化");
        return;
    }
    log::Stage("MinHook 初始化完成, 开始等待游戏就绪");

    // 等 UWorld / GameInstance / LocalPlayer 就绪 (最长无限等待, 与原项目一致)
    int waited = 0;
    while (!Helper::IsGameReady()) {
        if (Helper::Try([] { return Helper::IsGameReadyImpl(); })) break;
        if (waited % 20 == 0) // 每 10 秒留一行心跳, 便于确认"卡在等就绪"
            log::Stage(std::format("等待游戏就绪... {}s (UWorld={})",
                                   waited / 2,
                                   SDK::UWorld::GetWorld() ? "有" : "无"));
        ++waited;
        Sleep(500);
    }
    log::Stage("游戏就绪");
    log::Info("[boot] 游戏就绪, 安装钩子");

    // 监听者注册必须先于钩子安装
    features::Install();
    log::Stage("功能分发已注册");

    if (!InstallProcessEventHook()) {
        log::Stage("ProcessEvent 钩子安装失败");
        log::Error("[boot] ProcessEvent 钩子安装失败");
        return;
    }
    log::Stage("ProcessEvent 钩子安装成功");

    input::Install();
    log::Stage("WndProc 钩子安装完成");
    log::Info("[boot] 初始化完成, 等待 HUD 帧事件");

    // 常驻: 所有后续工作都在游戏线程的帧事件内发生
    for (;;) {
        Sleep(60000);
    }
}

} // namespace

} // namespace pal::core
