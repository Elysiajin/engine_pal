#include <pch.h>
#include "ui/Tabs.hpp"
#include "ui/Widgets.hpp"
#include "features/CheatState.hpp"
#include "features/FreeRide.hpp"

#include <ShadowGui/Shadow.h>

namespace pal::ui {

void TabFeatures() {
    using namespace pal::ui;

    // ===== 玩家功能 =====
    BeginPanel("玩家功能");
    Switch("无限体力", &cheatState.infStamina);

    Slider("世界速度", &cheatState.worldSpeed, 1.0f, 20.0f, 0.1f);
    if (ValueChanged(cheatState.worldSpeed, "worldSpeed"))
        ChangeWorldSpeed(cheatState.worldSpeed);

    if (SliderInt("攻击力", &cheatState.attack, 1, 5000))
        SetPlayerAttackParam();

    Slider("负重", &cheatState.weight, 1.0f, 100000.0f, 1.0f);
    if (ValueChanged(cheatState.weight, "weight"))
        SetPlayerInventoryWeight();

    if (ButtonFull("收集翠叶鼠雕像"))
        CollectAllRelicsInMap();
    if (ButtonFull("清除地图迷雾 [需重新加入服务器]"))
        RevealMapAroundPlayer();
    EndPanel();

    // ===== 武器功能 =====
    BeginPanel("武器功能");
    TextDesc("提高对树木、岩石等资源的伤害。");
    if (SliderInt("资源伤害", &cheatState.weaponDamage, 1, 400))
        SetWeaponDamage();

    if (ButtonFull("当前武器无限耐久"))
        IncreaseAllDurability();
    Switch("无限弹药", &cheatState.infAmmo);
    Switch("无限弹匣", &cheatState.infMag);
    EndPanel();

    // ===== 镜头 =====
    BeginPanel("镜头");
    Slider("视野范围", &cheatState.cameraFov, 25.0f, 170.0f, 1.0f);
    if (ValueChanged(cheatState.cameraFov, "cameraFov"))
        SetCameraFov();

    Slider("亮度", &cheatState.cameraBrightness, 0.0f, 5.0f, 0.01f);
    if (ValueChanged(cheatState.cameraBrightness, "cameraBrightness"))
        SetCameraBrightness();
    EndPanel();

    // ===== 自由乘骑 =====
    BeginPanel("自由乘骑");
    Switch("启用", &freeRide.enabled);
    HotKey("乘骑热键", &freeRide.rideHotkey);
    Switch("跳过动画", &freeRide.skipAnimation);

    if (freeRide.isRiding && freeRide.currentTarget)
        Text(std::format("正在乘骑: {}", freeRide.targetName));
    else
        TextDesc("未在乘骑状态。");

    if (ButtonFull("立即乘骑光标目标"))
        RideTargetAtMouse();
    if (ButtonFull("强制下马"))
        ForceGetOff();

    TextDesc("将光标指向帕鲁/NPC 并按热键 (默认鼠标中键) 即可快速乘骑: 绕过鞍具/驯服等条件限制。");
    TextDesc("联机时优先走游戏原生 RideTo (服务器权威同步), 兜底走 RiderComponent::Ride 并通知服务器。");
    TextDesc("如需下马, 再次按下热键或点击空白处。");
    EndPanel();
}

} // namespace pal::ui
