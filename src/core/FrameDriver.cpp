#include <pch.h>
#include "core/FrameDriver.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"

#include <ShadowGui/Shadow.h>

#include "engine/GameHelper.hpp"
#include "engine/ConfigManager.hpp"
#include "engine/NameMapper.hpp"
#include "features/CheatState.hpp"
#include "features/FeatureHost.hpp"
#include "ui/Menu.hpp"

// 由 features 层提供: 输出虚表探针统计 (帧锚点定位用)
namespace pal::features { void DumpViewportProbes(); }

namespace pal::core {

bool  FrameDriver::s_initialized = false;
float FrameDriver::s_alpha       = 0.0f;

// 画布合理性校验 (与 FeatureDispatch 里的探针判据保持一致):
// UCanvas::ClipX@0x30 / ClipY@0x34 必须是屏幕量级, ≥320x240。
// 上一版把 75x90 / 90x90 这种默认字号量级的垃圾指针当成了画布。
bool LooksLikeCanvas(const SDK::UCanvas* canvas) {
    if (!canvas || !Helper::IsProbablyValidPtr(canvas)) return false;
    bool ok = false;
    Helper::Try([&] {
        ok = canvas->ClipX >= 320.0f && canvas->ClipX <= 16384.0f &&
             canvas->ClipY >= 240.0f && canvas->ClipY <= 16384.0f;
    });
    return ok;
}

namespace {

// 菜单显隐切换时同步游戏光标显隐。
// 原项目用 ImGui::MouseDrawCursor 画软件光标; Shadow-Gui 不画光标, 改用
// 游戏自身的光标开关, 并在收尾时恢复原始状态。
void UpdateGameCursor() {
    static int  s_savedState   = -1; // -1 = 尚未保存
    static bool s_lastMenuOpen = false;

    const bool menuOpen = input::IsMenuOpen();
    if (menuOpen == s_lastMenuOpen) return;
    s_lastMenuOpen = menuOpen;

    SDK::APalPlayerController* pc = Helper::GetPalPlayerController();
    if (!pc || !Helper::IsProbablyValidPtr(pc)) return;

    if (menuOpen) {
        if (s_savedState < 0)
            s_savedState = pc->bShowMouseCursor ? 1 : 0;
        pc->bShowMouseCursor = true;
    } else if (s_savedState >= 0) {
        pc->bShowMouseCursor = s_savedState != 0;
        s_savedState = -1;
    }
}

// Shadow 自绘光标兜底: 部分游戏状态下 bShowMouseCursor 会被游戏内部重置,
// 画一个小十字确保菜单永远可用。
void DrawSoftwareCursor() {
    const Shadow::Vec2 m = Shadow::g_Ctx.MousePos;
    const Shadow::Color c{1.f, 1.f, 1.f, 0.9f};
    Shadow::GetBackgroundDrawList()->AddLine({m.x - 7.f, m.y}, {m.x + 7.f, m.y}, c, 1.4f);
    Shadow::GetBackgroundDrawList()->AddLine({m.x, m.y - 7.f}, {m.x, m.y + 7.f}, c, 1.4f);
    Shadow::GetBackgroundDrawList()->AddCircleFilled(m, 2.0f, c);
}

} // namespace

namespace {

// 实际帧逻辑。__try 所在函数不允许有需要栈展开的 C++ 对象, 因此把帧体拆到
// 这里 (只用引用参数, 不构造对象), 由 RunFrameGuarded 包 SEH。
// stage 记录"正在执行哪一段", 异常时由调用方写日志。
void DrawSoftwareCursor();

void FrameBody(SDK::UCanvas* canvas, int& stage) {
    // 阶段 1: DeltaTime。Shadow::GetIO() 内部会走 UWorld::GetWorld() +
    // UGameplayStatics::GetRealTimeSeconds, 在渲染线程/时机不对时会 0xC0000005。
    // 这里单独用 SEH 兜住并回退到固定步长 —— 拿不到时间不该让整帧作废。
    stage = 1;
    float dt = 0.016f;
    __try {
        dt = Shadow::GetIO().DeltaTime;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        dt = 0.016f;
    }
    if (!(dt > 0.f) || dt > 0.5f) dt = 0.016f;

    // --- 逻辑 tick (游戏线程, 原项目 Menu::Draw/Loops 两处 tick 在此合并) ---
    stage = 2;
    features::TickGameFrame(dt);

    // --- Shadow 帧 ---
    stage = 3;
    Shadow::NewFrame(canvas);
    stage = 4;
    Shadow::UpdateAllHotkeyStates();

    stage = 5;
    FrameDriver::UpdateFade(dt);
    UpdateGameCursor();

    Shadow::PushStyleVar(Shadow::GuiStyleVar_Alpha, FrameDriver::MenuAlpha());

    if (FrameDriver::MenuAlpha() > 0.001f) {
        stage = 6; // 菜单 UI
        ui::DrawMenu();

        if (input::IsMenuOpen())
            DrawSoftwareCursor();
    }

    // 世界覆盖层 (ESP/小地图/上帝之手/寻路): 菜单关闭时也要绘制
    stage = 7;
    features::DrawOverlays();

    Shadow::PopStyleVar();
    stage = 8; // 提交绘制命令
    Shadow::Render();
}

// 阶段名 (与 FrameBody 里的 stage 编号一一对应, 便于日志直接读)
constexpr const char* kStageNames[] = {
    "未进入",
    "1 取 DeltaTime",
    "2 功能逻辑 TickGameFrame",
    "3 Shadow::NewFrame(画布绑定)",
    "4 UpdateAllHotkeyStates",
    "5 UpdateFade/光标/样式",
    "6 菜单 UI DrawMenu",
    "7 世界覆盖层 DrawOverlays",
    "8 Shadow::Render 提交绘制",
};

const char* StageName(int stage) {
    return (stage >= 0 && stage < static_cast<int>(std::size(kStageNames)))
               ? kStageNames[stage] : "未知";
}

// 日志降频: 同一条消息每秒最多写一行 (上一版日志一秒上千行, 日志本身成了负担)
bool ShouldLog(unsigned long long& lastTick, unsigned intervalMs = 1000) {
    const auto now = GetTickCount64();
    if (now - lastTick < intervalMs) return false;
    lastTick = now;
    return true;
}

void RunFrameGuarded(SDK::UCanvas* canvas, int& stage, DWORD& code) {
    __try {
        FrameBody(canvas, stage);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        code = GetExceptionCode();
    }
}

// 首帧初始化 (EnsureInitialized) 的 SEH 外壳
void RunInitGuarded(int& stage, DWORD& code) {
    __try {
        stage = 1;
        FrameDriver::EnsureInitialized();
        stage = 2;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        code = GetExceptionCode();
    }
}

} // namespace

void FrameDriver::EnsureInitialized() {
    if (s_initialized) return;
    s_initialized = true;

    log::Stage("帧驱动: 首帧初始化开始");
    log::Info("[frame] 首帧初始化开始");
    log::Info("[frame] 日志文件: {} (打开={} 级别={})",
              log::LogPath(),
              log::IsFileOpen() ? "是" : "否",
              static_cast<int>(log::CurrentLevel()));

    // 输入放行白名单: 常用移动键 (对齐 Shadow-Gui 官方示例)
    Shadow::SetAllowedKeys({'W', 'A', 'S', 'D', VK_SPACE, VK_SHIFT});

    // 配置 + 名称映射 (DLL 目录)
    log::Stage("帧驱动: 读取配置");
    Config::Load("config.json");

    wchar_t dllDir[MAX_PATH] = {};
    HMODULE hSelf = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&dllDir), &hSelf);
    if (hSelf && GetModuleFileNameW(hSelf, dllDir, MAX_PATH)) {
        std::wstring dir(dllDir);
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash + 1);
        log::Stage(std::format("帧驱动: 加载名称映射 {}", log::Narrow(dir + L"名称映射.txt")));
        NameMapper::Get().LoadFromFileW(dir + L"名称映射.txt");
    }

    // 菜单切换键来自配置
    input::SetMenuToggleKey(cheatState.menuToggleKey);

    log::Stage("帧驱动: UI 初始化");
    ui::Initialize();
    log::Stage("帧驱动: 功能层初始化");
    features::InitializeFrame();
    log::Stage("帧驱动: 初始化完成");
    log::Info("[frame] 帧驱动初始化完成");
}

bool FrameDriver::IsInitialized() { return s_initialized; }

void FrameDriver::UpdateFade(float dt) {
    constexpr float kFadeSpeed = 8.0f;
    const float target = input::IsMenuOpen() ? 1.0f : 0.0f;
    if (s_alpha < target) s_alpha = std::min(target, s_alpha + kFadeSpeed * dt);
    else                  s_alpha = std::max(target, s_alpha - kFadeSpeed * dt);
}

float FrameDriver::MenuAlpha() { return s_alpha; }

void FrameDriver::OnFrame(SDK::AHUD* hud) {
    // 兼容入口: 用 HUD 对象自己的 Canvas (仅当它确实有效时)
    if (!hud) return;
    SDK::UCanvas* canvas = nullptr;
    Helper::Try([&] { canvas = hud->Canvas; });
    OnFrameWithCanvas(canvas);
}

void FrameDriver::OnFrameWithCanvas(SDK::UCanvas* canvas) {
    if (!canvas) return;
    if (!LooksLikeCanvas(canvas)) return; // 假画布直接丢弃 (见 FeatureDispatch 的探针说明)

    static unsigned long long s_frames = 0;
    ++s_frames;

    // 首帧时做一次性初始化。注意: EnsureInitialized 内部只做配置/字体/UI,
    // 不碰画布, 若它自身抛异常也要能定位 —— 因此单独包一层。
    if (!s_initialized) {
        int initStage = 0;
        DWORD initCode = 0;
        RunInitGuarded(initStage, initCode);
        if (initCode != 0) {
            static unsigned long long s_lastInitLog = 0;
            if (ShouldLog(s_lastInitLog)) {
                log::Stage(std::format("!!! 首帧初始化阶段 {} 触发异常 code=0x{:08X}",
                                       initStage, static_cast<unsigned long>(initCode)));
                log::Error("[frame] 首帧初始化阶段 {} 异常 (code=0x{:08X})",
                           initStage, static_cast<unsigned long>(initCode));
            }
            if (!s_initialized) return; // 初始化没完成, 本帧不能再画
        }
    }

    // 绘制阶段追踪: 抛 SEH (访问违例) 时记录崩在哪一段。
    int stage = 0;
    DWORD code = 0;
    RunFrameGuarded(canvas, stage, code);

    if (code != 0) {
        // 降频: 同一阶段每秒最多报一次, 避免刷爆日志
        static unsigned long long s_lastErrLog = 0;
        static int s_lastErrStage = -1;
        if (s_lastErrStage != stage || ShouldLog(s_lastErrLog)) {
            s_lastErrStage = stage;
            const char* where = StageName(stage);
            log::Stage(std::format("!!! 绘制阶段 [{}] 触发异常 code=0x{:08X}", where,
                                   static_cast<unsigned long>(code)));
            log::Error("[frame] 绘制阶段 [{}] 异常 (code=0x{:08X}), 本帧已跳过",
                       where, static_cast<unsigned long>(code));
        }
    }

    // 心跳: 每秒一行 (既报阶段, 也顺便 dump 一次虚表探针统计)
    static unsigned long long s_lastBeat = 0;
    static unsigned long long s_lastProbeDump = 0;
    if (ShouldLog(s_lastBeat)) {
        log::Stage(std::format("心跳: 第 {} 帧 完成阶段={} 菜单={} 画布={:.0f}x{:.0f}",
                               s_frames, StageName(stage),
                               input::IsMenuOpen() ? "开" : "关",
                               canvas->ClipX, canvas->ClipY));
    }
    // 探针统计只在开头几分钟有价值, 之后停掉
    if (s_frames < 20000 && ShouldLog(s_lastProbeDump, 5000))
        pal::features::DumpViewportProbes();
}

} // namespace pal::core
