#include <pch.h>
#include "ui/Tabs.hpp"
#include "ui/Widgets.hpp"
#include "features/CheatState.hpp"
#include "features/Hotkeys.hpp"
#include "core/Input.hpp"
#include "engine/ConfigManager.hpp"

#include <ShadowGui/Shadow.h>

namespace pal::ui {

void TabAimbotESP() {
    using namespace pal::ui; // 行式控件

    // ===== 自动瞄准 =====
    BeginPanel("自动瞄准");
    Switch("启用自动瞄准", &cheatState.aimbotEnabled);
    HotKey("快捷键", &cheatState.aimbotHotkey);
    Switch("显示瞄准范围", &cheatState.aimbotDrawFOV);
    Switch("可见性检查", &cheatState.aimbotVisibilityCheck);
    Slider("瞄准范围", &cheatState.aimbotFov, 1.0f, 280.0f, 1.0f);
    Slider("平滑度", &cheatState.aimbotSmooth, 0.0f, 1.0f, 0.01f);
    Combo("瞄准部位", &cheatState.aimbotAimPart, {"头部", "身体", "腿部"});
    EndPanel();

    // ===== 透视 =====
    BeginPanel("透视");
    Switch("启用透视", &cheatState.espEnabled);
    Switch("显示方框 (2D)", &cheatState.espBoxes);
    Switch("3D 方框", &cheatState.espBoxes3D);
    Switch("骨骼可视化", &cheatState.espSkeleton);
    Switch("热能透视 (热成像/穿墙)", &cheatState.espThermal);
    Switch("显示名称", &cheatState.espShowNames);
    Switch("显示距离", &cheatState.espShowDistance);
    Switch("显示生命值", &cheatState.espShowPalHealth);
    Slider("绘制距离", &cheatState.espDistance, 200.0f, 40000.0f, 100.0f);

    ColorPicker("方框颜色", cheatState.espColorBox);
    ColorPicker("3D 方框颜色", cheatState.espColorBox3D);
    ColorPicker("骨骼颜色", cheatState.espColorSkeleton);
    ColorPicker("名称颜色", cheatState.espColorName);
    EndPanel();

    // ===== 透视筛选 =====
    BeginPanel("透视筛选");
    Switch("显示帕鲁", &cheatState.espShowPals);
    Switch("显示翠叶鼠雕像", &cheatState.espShowRelics);
    EndPanel();

    // ===== 小地图雷达 =====
    BeginPanel("小地图雷达");
    Switch("启用小地图雷达", &cheatState.minimapEnabled);

    float rangeMeters = cheatState.minimapRange / 100.0f;
    Slider("探测范围 (米)", &rangeMeters, 10.0f, 1000.0f, 10.0f);
    cheatState.minimapRange = rangeMeters * 100.0f;

    Slider("雷达半径 (px)", &cheatState.minimapRadius, 40.0f, 400.0f, 1.0f);

    int orientation = cheatState.minimapRotationUp ? 0 : 1;
    if (Combo("方位模式", &orientation, {"Rotation-Up (玩家朝向朝上)", "North-Up (北方朝上)"}))
        cheatState.minimapRotationUp = (orientation == 0);

    Switch("显示实体标签", &cheatState.minimapShowLabels);

    Text("雷达显示分类");
    Switch("野生帕鲁", &cheatState.minimapShowWildPals);
    Switch("已驯服帕鲁", &cheatState.minimapShowTamedPals);
    Switch("其他玩家", &cheatState.minimapShowPlayers);
    Switch("NPC", &cheatState.minimapShowNPC);
    Switch("矿石", &cheatState.minimapShowOre);
    Switch("蛋", &cheatState.minimapShowEggs);
    Switch("宝箱", &cheatState.minimapShowTreasure);
    Switch("翠叶鼠雕像", &cheatState.minimapShowRelics);
    Switch("传送点", &cheatState.minimapShowFastTravel);

    Slider("水平位置", &cheatState.minimapPosX, 0.0f, 1.0f, 0.01f);
    Slider("垂直位置", &cheatState.minimapPosY, 0.0f, 1.0f, 0.01f);
    EndPanel();
}

void TabSettings() {
    using namespace pal::ui;

    // ===== 菜单 =====
    BeginPanel("菜单");
    HotKey("菜单显隐切换键", &cheatState.menuToggleKey);
    // 每帧同步到输入层 (修改即时生效)
    pal::core::input::SetMenuToggleKey(cheatState.menuToggleKey);
    TextDesc("默认 Insert。菜单打开时鼠标由菜单独占, WASD/空格/Shift 仍会透传给游戏。");
    EndPanel();

    // ===== 全局热键 =====
    BeginPanel("全局热键 (菜单关闭时同样生效)");
    HotKey(std::format("世界速度 1:10  [{}]", key.worldSpeedToggled ? "开启" : "关闭"),
           &key.hotkeyToggleWorldSpeed);
    HotKey(std::format("无限体力  [{}]", key.staminaToggled ? "开启" : "关闭"),
           &key.hotkeyStamina);
    HotKey(std::format("帕鲁透视  [{}]", key.espToggled ? "开启" : "关闭"),
           &key.hotkeyToggleESP);
    HotKey(std::format("雕像透视  [{}]", key.relicToggled ? "开启" : "关闭"),
           &key.hotkeyToggleRelic);
    HotKey(std::format("攻击力 1:90000  [{}]", key.attackToggled ? "开启" : "关闭"),
           &key.hotkeyToggleAttack);
    HotKey("修复当前武器", &key.hotkeyRepairWeapon);
    HotKey("传送回家", &key.hotkeyTeleportHome);
    HotKey("刷新负重", &key.hotkeyRefreshWeight);
    TextDesc("即使菜单关闭, 快捷键仍然有效。");
    EndPanel();

    // ===== 显示 =====
    BeginPanel("显示设置");
    Switch("使用英文名称 (不勾选则显示中文)", &cheatState.useEnglishNames);
    TextDesc("勾选后物品生成器和帕鲁生成器列表显示英文原名; 取消勾选则显示中文名称。");
    EndPanel();

    // ===== 配置 =====
    BeginPanel("配置");
    if (ButtonFull("保存配置"))
        Config::Save("config.json");
    if (ButtonFull("读取配置"))
        Config::Load("config.json");
    EndPanel();
}

void TabChangeLog() {
    using namespace pal::ui;

    BeginPanel("更新日志");
    Text("v1.0.0");
    TextDesc("PalEngine 重构版: Shadow-Gui 界面 + CMake/C++23 工程。");
    TextDesc("基于 PalworldInternal (DX11-Base) 功能全集重构。");
    EndPanel();
}

} // namespace pal::ui
