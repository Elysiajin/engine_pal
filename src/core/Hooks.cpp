#include <pch.h>
#include "core/Hooks.hpp"
#include "core/Log.hpp"
#include "engine/GameHelper.hpp"

using namespace SDK;

namespace pal::core {

namespace {

// ---- 原始 ProcessEvent ----
ProcessEventFn g_original = nullptr;

// ---- 监听者 ----
std::vector<ProcessEventListener>& Listeners() {
    static std::vector<ProcessEventListener> s_listeners;
    return s_listeners;
}

// ---- UFunction 全名缓存 ----
// 重构优化: 原实现每次 ProcessEvent 都调用 GetFullName() (构造 FString + 拼
// 字符串, PE 每秒调用数千次, 是纯浪费)。UFunction 的全名与对象生命周期一致
// (类对象常驻), 因此按指针缓存。上限保护防止极端情况无界增长。
struct FuncNameCache {
    std::unordered_map<const void*, std::string> map;
    std::mutex mutex;
    static constexpr size_t kMaxEntries = 20000;

    const std::string& Get(SDK::UFunction* func) {
        {
            std::lock_guard lock(mutex);
            if (auto it = map.find(func); it != map.end())
                return it->second;
        }
        std::string name;
        Helper::Try([&] { name = func->GetFullName(); });
        std::lock_guard lock(mutex);
        if (map.size() < kMaxEntries)
            return map.emplace(func, std::move(name)).first->second;
        return map[func] = std::move(name); // 超上限时允许覆盖, 防御异常情况
    }
};

FuncNameCache& NameCache() {
    static FuncNameCache s_cache;
    return s_cache;
}

// 在对象数组里找一个 Class 指针可读的活对象, 用它的虚表做钩子安装探针。
// (原实现同名逻辑, 保留 SEH 与范围检查)
bool InRangePtr(const void* p) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    return a > 0x10000ULL && a < 0x00007FFFFFFFFFFFULL;
}

SDK::UObject* FindVTableProbe() {
    if (!SDK::UObject::GObjects) return nullptr;
    const int32 n = SDK::UObject::GObjects->Num();
    for (int32 i = 0; i < n; ++i) {
        SDK::UObject* o = SDK::UObject::GObjects->GetByIndex(i);
        if (!o || !o->Class || !InRangePtr(o->Class)) continue;
        if (void** vt = *reinterpret_cast<void***>(o); vt && InRangePtr(vt))
            return o;
    }
    return nullptr;
}

// ---- 钩子本体 ----
// 重入规则: 我们自己在帧内调用 SDK 包装函数时会再次经过 ProcessEvent。
// 原项目因此禁止在 hook 内调用 SDK 函数 (并把每帧 tick 放到后台线程, 反而
// 引入线程安全问题)。重构版统一放行重入调用 (直接透传原始函数), 使功能层
// 可以安全地在游戏线程内调用任意 SDK 函数。
thread_local bool t_inDispatch = false;

void Dispatch(SDK::UObject* obj, SDK::UFunction* func, void* params) {
    if (!func || Listeners().empty()) {
        if (g_original) g_original(obj, func, params);
        return;
    }

    const std::string& full = NameCache().Get(func);

    for (ProcessEventListener listener : Listeners()) {
        listener(obj, func, params, full);
    }
}

void hkProcessEvent(SDK::UObject* obj, SDK::UFunction* func, void* params) {
    if (t_inDispatch || !g_original) {
        if (g_original) g_original(obj, func, params);
        return;
    }

    t_inDispatch = true;
    __try {
        Dispatch(obj, func, params);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // 与原项目同样的原则: SEH 捕获后绝不重复调用原始函数,
        // 丢弃本次事件即可, 让下一次调用继续。
    }
    t_inDispatch = false;
}

} // namespace

void AddProcessEventListener(ProcessEventListener listener) {
    if (listener) Listeners().push_back(listener);
}

ProcessEventFn GetOriginalProcessEvent() { return g_original; }

bool InstallProcessEventHook() {
    if (g_original) return true; // 已安装

    SDK::UObject* probe = FindVTableProbe();
    if (!probe) {
        log::Error("[hooks] 找不到可用于虚表探针的 UObject, 推迟安装");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(probe);
    // 使用 SDK 官方虚表索引 (原项目曾硬编码 0x4B 差一个槽位导致崩溃)
    constexpr int kProcessEventIndex = SDK::Offsets::ProcessEventIdx;

    if (MH_CreateHook(vtable[kProcessEventIndex], &hkProcessEvent,
                      reinterpret_cast<void**>(&g_original)) != MH_OK) {
        log::Error("[hooks] MH_CreateHook(ProcessedEvent) 失败");
        g_original = nullptr;
        return false;
    }
    if (MH_EnableHook(vtable[kProcessEventIndex]) != MH_OK) {
        log::Error("[hooks] MH_EnableHook(ProcessedEvent) 失败");
        return false;
    }
    log::Info("[hooks] ProcessEvent 钩子安装成功");
    return true;
}

namespace hooks {
bool CreateHook(void* target, void* detour, void** original) {
    return target && detour &&
           MH_CreateHook(target, detour, original) == MH_OK &&
           MH_EnableHook(target) == MH_OK;
}
} // namespace hooks

} // namespace pal::core
