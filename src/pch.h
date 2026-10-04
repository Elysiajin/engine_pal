// pch.h — PalEngine 预编译头
// 仅放"几乎每个翻译单元都需要且极少变动"的内容:
// Windows 头 + 标准库 + MinHook + Palworld SDK。
// UI 层的 Shadow-Gui 不进 PCH (只有 ui/ 目录使用), 避免无关 TU 付出编译成本。

#ifndef PALENGINE_PCH_H
#define PALENGINE_PCH_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <MinHook.h>

// Palworld Dumper-7 SDK (整个反射层)
#include <SDK.hpp>

#endif // PALENGINE_PCH_H
