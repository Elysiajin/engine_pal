#pragma once
// core/Hooks.hpp — ProcessEvent 虚表钩子的安装与分发骨架。
// 职责边界: 本模块只负责"挂上钩、把事件交给监听者", 不含任何游戏逻辑。
// 功能层 (features) 与帧驱动 (FrameDriver) 通过注册回调接入。

#include <pch.h>

namespace pal::core {

using ProcessEventFn = void (*)(SDK::UObject*, SDK::UFunction*, void*);

// 功能层回调。约定:
//   - 收到回调后由实现者决定是否调用原始 ProcessEvent (通过 GetOriginal)
//   - full 是 func 的全名 (已缓存, 调用方不需要再取)
using ProcessEventListener = void (*)(SDK::UObject* obj, SDK::UFunction* func,
                                      void* params, const std::string& full);

// 注册事件监听者 (先注册先调用)。必须在 InstallProcessEventHook 之前调用。
void AddProcessEventListener(ProcessEventListener listener);

// 安装 ProcessEvent 虚表钩子。要求 GObjects/SDK 已就绪 (主线程等 UWorld 就绪后再调)。
// 内部使用 SDK 官方虚表索引 Offsets::ProcessEventIdx, 不再硬编码。
bool InstallProcessEventHook();

// 获取原始 ProcessEvent (监听者透传用)。
ProcessEventFn GetOriginalProcessEvent();

// 通用 MinHook 包装 (native 函数钩子用)。
namespace hooks {
bool CreateHook(void* target, void* detour, void** original);
}

} // namespace pal::core
