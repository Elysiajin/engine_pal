// dllmain.cpp — PalEngine 入口
#include <pch.h>
#include "core/Bootstrap.hpp"
#include "core/Log.hpp"

namespace {
HMODULE g_module = nullptr;
}

DWORD WINAPI MainThread_Initialize(LPVOID) {
    // 无条件阶段诊断: 这几行与日志级别无关, 只要 DLL 被加载就一定会写盘。
    // 用于区分 "DLL 没被加载 / DllMain 没跑到 / 主线程没起来 / 卡在等游戏就绪"。
    pal::log::Stage("DllMain 之后: 初始化线程已启动");
    pal::core::Bootstrap(g_module); // Bootstrap 内部自带 SEH 保护并记录阶段
    pal::log::Stage("初始化线程结束 (不应出现)");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_module = hModule;
        DisableThreadLibraryCalls(hModule);
        // DllMain 内只允许做最小工作: 注意 log::Stage 会写文件 (CreateFileW),
        // 在 loader lock 下打开文件有死锁风险, 这里只做最轻量的一次。
        if (HANDLE h = CreateThread(nullptr, 0, MainThread_Initialize, hModule, 0, nullptr))
            CloseHandle(h);
        break;
    case DLL_PROCESS_DETACH:
        pal::log::Shutdown();
        break;
    }
    return TRUE;
}
