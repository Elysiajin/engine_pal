#include <pch.h>
#include "ui/Tabs.hpp"
#include "ui/Widgets.hpp"
#include "features/CheatState.hpp"
#include "features/GodHand.hpp"
#include "engine/ConfigManager.hpp"

#include <ShadowGui/Shadow.h>

namespace pal::ui {

namespace {

// 建造"无视"单项开关集合 (供全选/清除)
std::array<bool*, 22> BuildIgnoreFlags() {
    return {
        &cheatState.buildIgnoreRequirements,  &cheatState.buildNoConsumeMaterial,
        &cheatState.buildIgnoreGroundPlacement, &cheatState.buildAllowOverlapTerminal,
        &cheatState.buildIgnoreNearBoss,      &cheatState.buildIgnoreOil,
        &cheatState.buildIgnoreBaseLimit,     &cheatState.buildIgnoreCampLimit,
        &cheatState.buildIgnoreObstacle,      &cheatState.buildIgnoreOtherGuild,
        &cheatState.buildIgnoreSupport,       &cheatState.buildIgnoreCeiling,
        &cheatState.buildIgnoreOverlap,       &cheatState.buildIgnoreGroundContact,
        &cheatState.buildIgnoreNearCamp,      &cheatState.buildIgnoreUnderSea,
        &cheatState.buildIgnoreHighPlace,     &cheatState.buildIgnoreSlope,
        &cheatState.buildIgnoreBaseRange,     &cheatState.buildIgnoreIndoor,
        &cheatState.buildIgnoreConnect,       &cheatState.buildIgnoreWall,
    };
}

} // namespace

void TabSinglePlayer() {
    using namespace pal::ui;

    // ========== 移动与飞行 ==========
    BeginPanel("移动与飞行");
    if (Switch("飞行 (自由飞行, 无惯性)", &cheatState.isFly))
        ExploitFly();
    Switch("无限跳跃", &cheatState.infJump);

    if (cheatState.isFly) {
        Slider("飞行速度", &cheatState.flySpeed, 300.0f, 6000.0f, 10.0f);
        TextDesc("操作: WASD 水平移动, 空格上升, Shift 下降, 松键立即停止");
    }

    if (SliderInt("等级", &cheatState.playerLevel, 1, 100))
        SetPlayerLevel();
    Slider("移动速度", &cheatState.speedMultiplier, 100.0f, 5000.0f, 10.0f);
    if (ValueChanged(cheatState.speedMultiplier, "speedMultiplier"))
        SetPlayerSpeed();
    EndPanel();

    // ========== 战斗属性 ==========
    BeginPanel("战斗属性");
    if (SliderInt("防御力", &cheatState.defence, 0, 5000))
        SetPlayerDefenseParam();
    Slider("制作速度", &cheatState.craftingSpeed, 0.1f, 100.0f, 0.1f);
    if (ValueChanged(cheatState.craftingSpeed, "craftingSpeed"))
        SetCraftingSpeed();

    if (Switch("人物无敌", &cheatState.isInvicity))
        SetPlayerInvicity();
    Switch("隐身 (不被察觉/不触发犯罪/不反击)", &cheatState.invisible);
    Switch("免疫异常状态 (寒冷/炎热/中毒/负重过高等)", &cheatState.statusImmune);
    EndPanel();

    // ========== 科技点 ==========
    BeginPanel("科技点");
    if (SliderInt("增加科技点", &cheatState.addTechPoints, 0, 99999) || Button("添加"))
        AddTechPoints();
    if (SliderInt("增加古代科技点", &cheatState.addAncientTechPoints, 0, 99999) || Button("增加古代科技点"))
        AddAncientTechPoints();
    if (SliderInt("减少科技点", &cheatState.removeTechPoints, 0, 99999) || Button("移除"))
        RemoveTechPoints();
    if (SliderInt("减少古代科技点", &cheatState.removeAncientTechPoints, 0, 99999) || Button("移除古代科技点"))
        RemoveAncientTechPoint();
    EndPanel();

    // ========== 保鲜与时间 ==========
    BeginPanel("保鲜与时间");
    Switch("食物永不腐烂", &cheatState.foodNeverSpoil);
    Switch("时间静止 (世界暂停, 仅玩家可动)", &cheatState.timeFreeze);

    Text("切换时段");
    if (ButtonFull("早上 (6:00)"))  SetWorldTimePreset(6);
    if (ButtonFull("中午 (12:00)")) SetWorldTimePreset(12);
    if (ButtonFull("傍晚 (18:00))")) SetWorldTimePreset(18);
    if (ButtonFull("夜晚 (23:00)")) SetWorldTimePreset(23);

    Text("时间编辑器 (自由设置存档天数/时刻)");
    {
        // 当前时间显示 (每 0.5s 刷新)
        static int32_t curDay = 0, curHour = 0, curMinute = 0;
        static float refreshTimer = 0.f;
        refreshTimer += Shadow::GetIO().DeltaTime;
        if (refreshTimer > 0.5f) {
            refreshTimer = 0.f;
            GetWorldTimeInfo(curDay, curHour, curMinute);
        }
        Text(std::format("当前: 第 {} 天  {:02d}:{:02d}", curDay, curHour, curMinute));
    }

    // 编辑输入 (首次以当前时间初始化)
    static bool s_timeInit = false;
    static int editDay = 1, editHour = 6, editMinute = 0;
    if (!s_timeInit) {
        int32_t d = 0, h = 0, m = 0;
        if (GetWorldTimeInfo(d, h, m)) {
            editDay = d; editHour = h; editMinute = m;
            s_timeInit = true;
        }
    }
    SliderInt("天", &editDay, 1, 99999);
    SliderInt("时", &editHour, 0, 23);
    SliderInt("分", &editMinute, 0, 59);
    if (ButtonFull("应用时间设置"))
        SetWorldTimeCustom(editDay, editHour, editMinute);
    TextDesc("可直接编辑存档的天数与时刻; 单人模式立即生效。");

    if (ButtonFull("解锁所有传送塔")) {
        cheatState.allFastTravelUnlocked = true; // 激活 hook 拦截查询
        UnlockAllFastTravelPoints();             // 双保险: 也尝试写 flag
    }
    Switch("地图随处传送 (无需站在传送石像/终端旁)", &cheatState.mapFreeTeleport);
    EndPanel();

    // ========== 捕获辅助 ==========
    BeginPanel("捕获辅助");
    Switch("捕获概率 100% (必中)", &cheatState.palCapture100);
    Switch("可以抓塔主 (联机)", &cheatState.canCatchTowerBoss);
    EndPanel();

    // ========== 建造/制作解锁 ==========
    BeginPanel("建造/制作解锁 (临时, 重启还原)");
    Switch("【总开关】启用全部建造解锁", &cheatState.buildUnlockEnabled);
    Switch("快速建造/制作", &cheatState.buildFastBuild);
    Switch("放置后禁止破坏", &cheatState.buildNoDismantle);

    if (Button("全选无视")) {
        for (bool* flag : BuildIgnoreFlags()) *flag = true;
    }
    if (Button("清除无视")) {
        for (bool* flag : BuildIgnoreFlags()) *flag = false;
    }

    Separator();

    Switch("制作和建造无视需求 (解锁全建造/制作样式)", &cheatState.buildIgnoreRequirements);
    Switch("建造和制作不消耗材料", &cheatState.buildNoConsumeMaterial);
    Switch("无视'未放置在地面上'", &cheatState.buildIgnoreGroundPlacement);
    Switch("允许重叠终端", &cheatState.buildAllowOverlapTerminal);
    Switch("无视'距离特殊头目或设施过近'", &cheatState.buildIgnoreNearBoss);
    Switch("无视'需放置于可采集原油的位置'", &cheatState.buildIgnoreOil);
    Switch("无视'基地已达建筑上限'", &cheatState.buildIgnoreBaseLimit);
    Switch("无视'无法建造更多据点'", &cheatState.buildIgnoreCampLimit);
    Switch("无视'路径存在障碍物'", &cheatState.buildIgnoreObstacle);
    Switch("无视'无法在其他公会基地建造'", &cheatState.buildIgnoreOtherGuild);
    Switch("无视'支撑不足'", &cheatState.buildIgnoreSupport);
    Switch("无视'放置天花板'", &cheatState.buildIgnoreCeiling);
    Switch("无视'与其他物体重叠'", &cheatState.buildIgnoreOverlap);
    Switch("无视'所有地板必须与地面接触'", &cheatState.buildIgnoreGroundContact);
    Switch("无视'与其他据点过近'", &cheatState.buildIgnoreNearCamp);
    Switch("无视'无法在海平面以下建造'", &cheatState.buildIgnoreUnderSea);
    Switch("无视'无法在如此高处建造'", &cheatState.buildIgnoreHighPlace);
    Switch("无视'接触地面的斜面过度倾斜'", &cheatState.buildIgnoreSlope);
    Switch("无视'必须建造在基地范围内'", &cheatState.buildIgnoreBaseRange);
    Switch("无视'建造在室内'", &cheatState.buildIgnoreIndoor);
    Switch("无视'未连接至建筑'", &cheatState.buildIgnoreConnect);
    Switch("无视'放置墙体'", &cheatState.buildIgnoreWall);
    TextDesc("提示: 总开关开启后强制所有放置/上色检查返回成功; 单项开关精确屏蔽对应限制; 重启游戏后自动还原。");
    EndPanel();

    // ========== 附近世界操作 ==========
    BeginPanel("附近世界操作");
    if (ButtonFull("一键砍伐附近树木")) ChopNearbyTrees();
    if (ButtonFull("一键挖矿附近矿石")) MineNearbyRocks();
    if (ButtonFull("一键拾取附近物品")) PickupNearbyItems();

    Text("一键秒杀 (全生物)");
    Slider("秒杀半径 (米)", &cheatState.killRange, 1.0f, 400.0f, 1.0f);
    if (ButtonFull("一键秒杀附近所有生物 (保留本队帕鲁)"))
        KillAllCreaturesNearby(cheatState.killRange * 100.0f);
    TextDesc("对半径内所有非本队帕鲁的生物 (含野生帕鲁、NPC、敌对玩家) 造成秒杀, 本队已收服帕鲁不受影响。");

    Text("秒杀准星所指对象");
    TextDesc("对准星 (屏幕中心) 最近的生物或地图物件造成秒杀。生物: 保留掉落归属, 本队帕鲁不杀; 物件: 走正规采集掉落。");
    if (ButtonFull("秒杀准星所指对象"))
        KillTargetUnderCrosshair();
    EndPanel();

    // ========== 上帝之手 ==========
    BeginPanel("上帝之手 (光标目标控制)");
    Switch("启用", &godHand.enabled);

    TextDesc("1. 勾选开关 → 关闭菜单");
    TextDesc("2. 准星对准目标 → 按住热键 (默认鼠标中键)");
    TextDesc("3. 转动视角 → 目标在球面上跟随; 滚轮 → 调半径");
    TextDesc("4. 松开热键 → 目标留在原地");

    HotKey("抓取热键", &godHand.grabHotkey);

    {
        float radiusM = godHand.sphereRadius / 100.f;
        Slider("球面半径 (米)", &radiusM, godHand.minRadius / 100.f, godHand.maxRadius / 100.f, 1.0f);
        godHand.sphereRadius = radiusM * 100.f;

        float wheelM = godHand.wheelSensitivity / 100.f;
        Slider("滚轮灵敏度 (米/格)", &wheelM, 0.1f, 10.0f, 0.1f);
        godHand.wheelSensitivity = wheelM * 100.f;
    }

    if (godHand.isGrabbing && godHand.grabbedTarget) {
        Text(std::format("正在控制: {}  ({:.0f} cm / {:.1f} m)",
                         godHand.targetName, godHand.sphereRadius, godHand.sphereRadius / 100.f));

        if (ButtonFull("释放目标"))
            ReleaseGrabbed();
    } else {
        TextDesc("未在控制目标。按住热键抓取准星目标。");
        if (ButtonFull("抓取光标下目标"))
            GrabAtMouse();
    }

    Text("精确偏移 (输入后点应用)");
    Slider("X 偏移 (cm)", &godHand.offsetX, -10000.f, 10000.f, 10.f);
    if (ButtonFull("应用 X 偏移"))
        ApplyGrabbedOffset();
    Slider("Y 偏移 (cm)", &godHand.offsetY, -10000.f, 10000.f, 10.f);
    if (ButtonFull("应用 Y 偏移"))
        ApplyGrabbedOffset();
    Slider("Z 偏移 (cm)", &godHand.offsetZ, -10000.f, 10000.f, 10.f);
    if (ButtonFull("应用 Z 偏移"))
        ApplyGrabbedOffset();

    TextDesc("联机: 通过 K2_TeleportTo/K2_SetActorLocation 每帧更新位置。单机直接生效, 联机位置可能被服务器覆盖。");
    EndPanel();
}

} // namespace pal::ui
