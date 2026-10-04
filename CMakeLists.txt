cmake_minimum_required(VERSION 3.24)

project(PalEngine
    VERSION 1.0.0
    DESCRIPTION "Palworld internal mod with Shadow-Gui"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 注入型 DLL: 静态 CRT, 不依赖目标机 VC 运行库
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MSVC 并行编译进程数 (/MP)。
# Dumper-7 SDK 的单个翻译单元可达数百 MB, 裸 /MP (按核数) 会让多个 cl.exe
# 同时吃满内存并触发 C1060 (编译器的堆空间不足), 因此默认限制为 4 个;
# 内存充裕 (>=32GB) 的机器可 -DPALENGINE_MP_JOBS=8 提升编译速度。
set(PALENGINE_MP_JOBS 4 CACHE STRING "每个项目的 MSVC /MP 并行编译进程数")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "PalEngine 仅支持 x64 (Palworld 是 64 位进程)")
endif()

# ---------------------------------------------------------------------------
# MinHook (C)
# ---------------------------------------------------------------------------
add_library(minhook STATIC
    libs/MinHook/src/buffer.c
    libs/MinHook/src/hook.c
    libs/MinHook/src/trampoline.c
    libs/MinHook/src/hde/hde32.c
    libs/MinHook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC libs/MinHook/include)
target_compile_definitions(minhook PRIVATE WIN32_LEAN_AND_MEAN)

# ---------------------------------------------------------------------------
# Palworld Dumper-7 SDK (反射层, 预编译成静态库避免反复编译)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PALWORLD_SDK_SOURCES CONFIGURE_DEPENDS libs/SDK/*.cpp)
add_library(palworld_sdk STATIC ${PALWORLD_SDK_SOURCES})
target_include_directories(palworld_sdk PUBLIC libs)
target_compile_options(palworld_sdk PRIVATE /bigobj /W3 /MP${PALENGINE_MP_JOBS} /Gy /Gw)
target_compile_definitions(palworld_sdk PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# 未引用的 SDK 函数 (数万个 ProcessEvent 转发 thunk) 在链接期被剔除,
# 保证最终 DLL 体积与"只编译用到的模块"相当 (1~2MB 量级)
target_link_options(palworld_sdk INTERFACE
    $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
    $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>
)

# ---------------------------------------------------------------------------
# PalEngine 主 DLL
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PALENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(PalEngine SHARED ${PALENGINE_SOURCES})

target_precompile_headers(PalEngine PRIVATE src/pch.h)

target_include_directories(PalEngine PRIVATE
    src
    libs
    libs/json/include
)

target_link_libraries(PalEngine PRIVATE minhook palworld_sdk)

target_compile_definitions(PalEngine PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    PALWORLD_MOD
)

target_compile_options(PalEngine PRIVATE
    /MP${PALENGINE_MP_JOBS}   # 并行编译 (进程数见 PALENGINE_MP_JOBS)
    /bigobj      # SDK 头文件巨大, 段数超限
    /Gy /Gw      # 函数/数据级 COMDAT, 配合 /OPT:REF 剔除未引用代码
    /W3
    /utf-8       # 源码与字符串字面量均为 UTF-8
)

set_target_properties(PalEngine PROPERTIES
    OUTPUT_NAME "PalEngine"
    PREFIX ""
)
