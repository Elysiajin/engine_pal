#pragma once
// core/Log.hpp — 统一日志设施。
// 重构说明: 原项目混用 printf / CaptureLog / CaptureLogThrottled 三套输出,
// Release 下 printf 不可见、日志直接写盘无开关。这里收敛为分级日志:
//   - OutputDebugString + 可选文件落盘 (DLL 所在目录 palengine.log)
//   - 内建节流 (同一条目每 N 毫秒最多输出一次), 热路径安全
// 日志默认级别 Release 为 Warn, Debug 为 Debug; 可用环境变量 PALENGINE_LOG 覆盖。

#include <pch.h>

namespace pal::log {

enum class Level : int {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    Off   = 4,
};

// 初始化: 确定日志文件路径 (DLL 同目录) 与级别。幂等。
void Init();

// 关闭: flush 并关闭文件句柄。DLL 卸载时调用。
void Shutdown();

// 基础输出。fmt 使用 std::format 语法。
// 说明: std::format_args 不携带格式串, 因此 fmt 一并传入。
//       MSVC 的 std::basic_format_string 不提供到 string_view 的隐式转换,
//       统一用 get() 取出格式串。
void Write(Level level, std::string_view tag, std::string_view fmt, std::format_args args);
void WriteRaw(Level level, std::string_view tag, std::string_view message);

template <typename... Args>
void Debug(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Debug, "core", fmt.get(), std::format_args{ std::make_format_args(args...) });
}
template <typename... Args>
void Info(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Info, "core", fmt.get(), std::format_args{ std::make_format_args(args...) });
}
template <typename... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Warn, "core", fmt.get(), std::format_args{ std::make_format_args(args...) });
}
template <typename... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Error, "core", fmt.get(), std::format_args{ std::make_format_args(args...) });
}

// 带独立 tag 的输出 (功能模块名)。
template <typename... Args>
void Log(Level level, std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    Write(level, tag, fmt.get(), std::format_args{ std::make_format_args(args...) });
}

// 节流输出: 以 key 为索引, 同一 key 每 intervalMs 毫秒最多输出一次。
// 用于每帧可能触发的路径。key 必须是稳定字符串字面量 (地址作哈希键)。
template <typename... Args>
void Throttled(Level level, std::string_view tag, std::string_view key,
               unsigned intervalMs, std::format_string<Args...> fmt, Args&&... args) {
    static constexpr size_t kSlots = 64;
    struct Slot { std::string_view key; unsigned long long lastMs; };
    static Slot s_slots[kSlots] = {};
    static std::mutex s_mutex;

    unsigned long long h = 1469598103934665603ULL;
    for (char c : key) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL;

    const auto now = GetTickCount64();
    {
        std::lock_guard lock(s_mutex);
        Slot& slot = s_slots[h % kSlots];
        if (slot.key == key && now - slot.lastMs < intervalMs)
            return;
        slot.key = key;
        slot.lastMs = now;
    }
    Write(level, tag, fmt.get(), std::format_args{ std::make_format_args(args...) });
}

// 是否启用 SEH 崩溃兜底日志 (true 时访问违例会落盘一行)
void SetCrashLogPath(const std::wstring& path);

// 诊断: 日志文件是否打开成功 / 当前级别 / 日志文件完整路径。
// 用于"注入后看不到日志"的排查 (FrameDriver 首帧打印一次)。
bool IsFileOpen();
Level CurrentLevel();
std::string LogPath();

// 阶段诊断: 不经过日志级别过滤与 stdio 缓冲, 直接追加写 DLL 目录下的
// stage_boot.log。用于定位"注入成功但什么都没发生"这类问题。
void Stage(std::string_view text);

// UTF-16 -> UTF-8 (日志里打印含中文的路径用)
std::string Narrow(const std::wstring& w);

} // namespace pal::log
