#include <pch.h>
#include "core/Log.hpp"

namespace pal::log {

namespace {

// 记录日志是否真正可用 (文件打开成功 / 级别设置), 供 FrameDriver 首帧诊断使用。
bool g_diagFileOpen = false;
Level g_diagLevel = Level::Warn;

struct State {
    std::FILE* file = nullptr;
    Level minLevel = Level::Warn;
    std::mutex mutex;
    bool initialized = false;
    unsigned pendingLines = 0;
};

State& S() {
    static State s;
    return s;
}

constexpr std::string_view LevelName(Level lv) {
    switch (lv) {
    case Level::Debug: return "DBG";
    case Level::Info:  return "INF";
    case Level::Warn:  return "WRN";
    case Level::Error: return "ERR";
    default:           return "???";
    }
}

Level ParseLevel(std::string_view v) {
    if (v == "debug" || v == "0") return Level::Debug;
    if (v == "info"  || v == "1") return Level::Info;
    if (v == "warn"  || v == "2") return Level::Warn;
    if (v == "error" || v == "3") return Level::Error;
    if (v == "off")               return Level::Off;
    return Level::Warn;
}

void Emit(Level level, std::string_view tag, std::string_view msg) {
    State& s = S();
    if (!s.initialized || level < s.minLevel) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);

    const std::string line = std::format(
        "[{:02d}:{:02d}:{:02d}.{:03d}][{:>5}][{}][{}] {}\n",
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentThreadId(), LevelName(level), tag, msg);

    std::lock_guard lock(s.mutex);
    OutputDebugStringA(line.c_str());
    if (s.file) {
        fwrite(line.data(), 1, line.size(), s.file);
        // Error 立即落盘; 其余也立即 flush —— 注入型 DLL 常被杀软/游戏强制卸载,
        // 缓冲日志会整段丢失, 这里的可靠性比吞吐重要 (日志本身是低频的)。
        if (level >= Level::Error) fflush(s.file);
        else s.pendingLines++;
        if (s.pendingLines >= 8) {
            fflush(s.file);
            s.pendingLines = 0;
        }
    }
}

// 解析 DLL 所在目录 (日志/诊断文件都写在这里)。失败返回空。
std::wstring DllDirectory() {
    HMODULE hSelf = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&DllDirectory), &hSelf);
    if (!hSelf) return {};
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(hSelf, path, MAX_PATH)) return {};
    std::wstring dir(path);
    const size_t slash = dir.find_last_of(L"\\/");
    return (slash != std::wstring::npos) ? dir.substr(0, slash + 1) : std::wstring{};
}

std::string NarrowImpl(const std::wstring& w) {
    if (w.empty()) return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), need, nullptr, nullptr);
    return out;
}

// 在 DLL 目录旁写一份“无条件”诊断文件 (stage_boot.log):
// 不经过 stdio、不受日志级别过滤影响, 每次调用重新打开/追加/立即关闭。
// 这是“本进程确实跑到过这一步”的铁证。
void StageWrite(std::string_view text) {
    const std::wstring dir = DllDirectory();
    if (dir.empty()) return;

    HANDLE h = CreateFileW((dir + L"stage_boot.log").c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    const std::string line = std::format("[{:02d}:{:02d}:{:02d}.{:03d}][pid:{}][tid:{}] {}\r\n",
                                         now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                                         GetCurrentProcessId(), GetCurrentThreadId(), text);
    DWORD written = 0;
    WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(h);
}

} // namespace

// 阶段诊断: 与日志级别无关, 始终写盘 (注入型 DLL 排查用)
void Stage(std::string_view text) {
    StageWrite(text);
    const std::string dbg = std::format("[PalEngine] {}\n", text);
    OutputDebugStringA(dbg.c_str());
}

std::string Narrow(const std::wstring& w) { return NarrowImpl(w); }

void Init() {
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (s.initialized) return;

    StageWrite("log::Init 进入");
    // 级别: 环境变量优先, Debug 构建默认 Debug
#ifdef _DEBUG
    s.minLevel = Level::Debug;
#else
    s.minLevel = Level::Warn;
#endif
    char env[16] = {};
    if (GetEnvironmentVariableA("PALENGINE_LOG", env, sizeof(env)))
        s.minLevel = ParseLevel(env);
    else {
        // 环境变量对已启动的游戏无效 (注入时进程环境已固定)。
        // 因此支持 DLL 目录下的 palengine_log_level.txt, 内容形如 "debug"。
        const std::wstring dir = DllDirectory();
        if (!dir.empty()) {
            std::FILE* f = nullptr;
            if (_wfopen_s(&f, (dir + L"palengine_log_level.txt").c_str(), L"r") == 0 && f) {
                char buf[32] = {};
                const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
                fclose(f);
                std::string_view v(buf, n);
                while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' '))
                    v.remove_suffix(1);
                if (!v.empty()) s.minLevel = ParseLevel(v);
            }
        }
    }

    // 日志写到 DLL 所在目录 (游戏工作目录不可控, 不能用相对路径)
    HMODULE hSelf = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&s.initialized), &hSelf);
    if (hSelf) {
        wchar_t path[MAX_PATH] = {};
        if (GetModuleFileNameW(hSelf, path, MAX_PATH)) {
            std::wstring dir(path);
            const size_t slash = dir.find_last_of(L"\\/");
            if (slash != std::wstring::npos) dir.resize(slash + 1);
            else dir.clear();
            if (_wfopen_s(&s.file, (dir + L"palengine.log").c_str(), L"w") != 0)
                s.file = nullptr;
            else
                setvbuf(s.file, nullptr, _IONBF, 0); // 无缓冲: 崩溃/强制卸载也不丢日志
        }
    }
    s.initialized = true;
    g_diagFileOpen = (s.file != nullptr);
    g_diagLevel = s.minLevel;
    StageWrite(std::format("log::Init 完成 文件={} 级别={}",
                           g_diagFileOpen ? "打开" : "失败",
                           static_cast<int>(s.minLevel)));
}

bool IsFileOpen() { return g_diagFileOpen; }
Level CurrentLevel() { return g_diagLevel; }
std::string LogPath() {
    const std::wstring dir = DllDirectory();
    return NarrowImpl(dir + L"palengine.log");
}

void Shutdown() {
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (s.file) {
        fflush(s.file);
        fclose(s.file);
        s.file = nullptr;
    }
    s.initialized = false;
    g_diagFileOpen = false;
}

void WriteRaw(Level level, std::string_view tag, std::string_view message) {
    Emit(level, tag, message);
}

void Write(Level level, std::string_view tag, std::string_view fmt, std::format_args args) {
    Emit(level, tag, std::vformat(fmt, args));
}

void SetCrashLogPath(const std::wstring&) {
    // 兼容占位: 崩溃日志与常规日志共用一个文件
}

} // namespace pal::log
