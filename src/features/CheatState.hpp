#pragma once
#include <pch.h>
#include <string>
#include <Windows.h>

extern SDK::APalPlayerCharacter* selectedPlayer;
extern SDK::APalWeaponBase* playerWeapon;

void SetInfiniteAmmo();
void TickInfiniteAmmo();           // 每帧: 持续维持无限弹药/弹匣（换武器后也自动生效）
void TickInfiniteMagazine();
void ChangeWorldSpeed(float speed);
void SetPlayerAttackParam();
void SetPlayerInventoryWeight();
void SetInfiniteAmmo();
void ResetStamina();
void AddItemToInventoryByName(std::string itemName, int count);
void AddItemToInventoryByName_ToServer(std::string itemName, int count);  // 联机服务器请求添加物品
void TeleportPlayerTo(const SDK::FVector& pos);
void SetCameraFov();
void SetCameraBrightness();
void AddWaypointLocation(const std::string& wpName);
bool RemoveWaypointLocationByName(const std::string& wpName);
void IncreaseAllDurability();
void CollectAllRelicsInMap();
void SetWeaponDamage();
void SetInfiniteMagazine();
void TeleportPlayerToHome();
void RevealMapAroundPlayer();
void UnlockAllFastTravelPoints();

//Debug
void DebugBuildOverlap();

//Build Unlock (建造/制作解锁)
void TickBuildUnlock();      // 每帧: 处理重叠清除 / 快速建造 / 制作
void ForceBuildUnlock();     // 一次性: 解锁全建造和制作样式

//Player Movement (玩家移动)
void TickInfiniteJump();     // 每帧: 无限跳跃

//Status Immunity (异常状态免疫)
void TickStatusImmune();     // 每帧: 免疫寒冷/炎热/负重等所有负面异常状态
void TickInvisible();        // 每帧: 隐身模式（不触发犯罪/不被察觉/受击不反击）

//SinglePlayer
void ExploitFly();
void ToggleFly(bool bEnable);        // 进入/退出飞行
void TickFly(float DeltaTime);       // 每帧: 处理飞行方向/升空/降落（无惯性）
void SetPlayerDefenseParam();
void SetCraftingSpeed();
void AddTechPoints();
void AddAncientTechPoints();
void RemoveTechPoints();
void RemoveAncientTechPoint();
void SetPlayerSpeed();
void SetPlayerLevel();
void SetPlayerInvicity();
void ChopNearbyTrees();
void MineNearbyRocks();
void PickupNearbyItems();
void KillNearbyWildPals();
void KillAllCreaturesNearby(float Range);   // 秒杀半径内所有非本队生物（含玩家）
void DamageSingleMapObject(SDK::AActor* Actor);  // 破坏单个地图物件（矿石/树，采集掉落+本地HP归零）
void TickFoodNoSpoil();                      // 每帧: 食物永不腐烂
void SetWorldTimePreset(__int32 Hour);       // 切换游戏时段到指定小时
void TickTimeFreeze();                       // 每帧: 时间静止
// 时间编辑器: 读取当前游戏时间 / 自由设置存档的天数与时:分
bool GetWorldTimeInfo(__int32& OutTotalDay, __int32& OutHour, __int32& OutMinute);
void SetWorldTimeCustom(__int32 Day, __int32 Hour, __int32 Minute);
// 秒杀准星所指对象（生物或地图物件，本队帕鲁不受影响）
void KillTargetUnderCrosshair();

struct BulkItem { std::string id; int count; };

struct CheatState
{ 

    //Features
    float worldSpeed = 1.0f;
    int weaponDamage = 1;
    int attack = 1;
    float weight = 600.0f;
    bool infAmmo = false;
    bool infMag = false;
    bool infStamina = false;
    float cameraFov = 90.0f;
    float cameraBrightness = 1.0f;

    //Aimbot
    bool isSilent = true;
	bool aimbotEnabled = false;
	bool aimbotShowFov = false;
	bool aimbotDrawFOV = false;
	float aimbotFov = 120.0f;
	float aimbotSmooth = 0.1f;
    int aimbotHotkey = VK_MENU;
    bool aimbotVisibilityCheck = true;
    int aimbotAimPart = 0;             // 瞄准部位: 0=头部 1=身体 2=腿部(可配置)

    // ESP
    bool espEnabled = true;
    bool espBoxes = false;
    bool espBoxes3D = false;           // 3D 方框
    bool espSkeleton = false;          // 骨骼可视化
    bool espThermal = false;           // 热能透视（热成像上色，最高深度/穿墙）
    bool espShowNames = true;
    bool espShowDistance = false;
    float espDistance = 10000.0f;
    // ESP Filters
    bool espShowPalHealth = false;
    bool espShowPals = true;
    bool espShowRelics = true;
    bool espShowWaypoints = false;
    // ESP 颜色调节（RGBA 0-1，供绘制系统自定义线条颜色）
    float espColorBox[4]     = { 0.0f, 1.0f, 0.0f, 1.0f };   // 方框
    float espColorBox3D[4]   = { 0.0f, 1.0f, 0.0f, 1.0f };   // 3D 方框
    float espColorSkeleton[4]= { 0.0f, 1.0f, 1.0f, 1.0f };   // 骨骼
    float espColorName[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };   // 名称

    // MiniMap Radar
    bool minimapEnabled = false;
    float minimapRadius = 200.0f;       // 雷达半径（像素）
    float minimapRange = 20000.0f;      // 探测范围（厘米，200米）
    bool minimapRotationUp = true;      // 方位模式: true=Rotation-Up, false=North-Up
    bool minimapShowLabels = false;     // 是否显示实体标签
    bool minimapShowWildPals = true;    // 分类显示开关
    bool minimapShowTamedPals = true;
    bool minimapShowPlayers = true;
    bool minimapShowNPC = true;
    bool minimapShowOre = true;
    bool minimapShowEggs = true;
    bool minimapShowTreasure = true;
    bool minimapShowRelics = true;
    bool minimapShowFastTravel = true;
    float minimapPosX = 0.02f;          // 雷达位置 X（屏幕比例）
    float minimapPosY = 0.25f;          // 雷达位置 Y（屏幕比例）

    //SinglePlayer
    bool isFly = false;
    bool flyInertia = false;         // 飞行无惯性（松键立即停）默认 true 语义，此处保留配置项
    float flySpeed = 1200.0f;        // 飞行速度（可配置，默认 1200 cm/s）
    bool isInvicity = false;
    bool invisible = false;         // 隐身模式: 不触发犯罪、敌人/帕鲁/NPC 无法察觉、受击不反击
    bool statusImmune = false;      // 免疫所有异常状态(寒冷/炎热/中毒/眩晕/负重过高等)
    bool foodNeverSpoil = false;    // 食物永不腐烂
    bool timeFreeze = false;        // 时间静止（世界暂停，仅玩家可动）
    float killRange = 10000.0f;     // 一键秒杀作用半径（厘米，默认100米）
    float craftingSpeed = 1.0f;
    __int32 addTechPoints = 0;
    __int32 addAncientTechPoints = 0;
    __int32 removeTechPoints = 0;
    __int32 removeAncientTechPoints = 0;
    int defence = 0;
    float speedMultiplier = 600.0f;
    __int32 playerLevel = 0;
    __int32 palCaptureCount = 0;
    int expToAdd = 0;

    // === 5.1 人物属性强化次数（StatusPoint 编辑目标值）===
    int spMaxHp = 0;            // 生命值强化次数
    int spMaxStamina = 0;       // 体力强化次数
    int spAttack = 0;           // 攻击力强化次数
    int spDefense = 0;          // 防御力强化次数
    int spWorkSpeed = 0;        // 工作速度强化次数
    int spCapture = 0;          // 捕获力强化次数 (AddCaptureLevel)
    int spWeight = 0;           // 负重上限强化次数
    int spSwimSpeed = 0;        // 游泳能力强化次数
    int spJumpPower = 0;        // 跳跃能力强化次数
    int spClimbSpeed = 0;       // 攀爬能力强化次数
    int spGlideSpeed = 0;       // 滑翔能力强化次数
    int spFoodPreserve = 0;     // 食物保存强化次数
    int spHungerResist = 0;     // 耐饿能力强化次数
    int spStatusResist = 0;     // 异常状态抵抗强化次数
    int spSphereTrack = 0;      // 帕鲁球追踪强化次数
    int spFortune = 0;          // 虹彩之运强化次数
    int spMoveSpeed = 0;        // 移动速度强化次数

    //Player Movement / Capture (玩家移动 & 捕获)
    bool infJump = false;               // 无限跳跃(无需跳跃鞋, 落地前可无限连跳)
    bool palCapture100 = false;         // 捕获概率100% (必中)
    bool canCatchTowerBoss = false;     // 可以抓塔主(联机)
    bool mapFreeTeleport = true;        // 解除传送限制: 打开地图即可随处实时传送
    bool allFastTravelUnlocked = false; // 所有传送点已解锁 (由"解锁所有传送塔"按钮触发)
                                        // (无需站在传送石像/终端旁)

    //Build Unlock (建造/制作解锁, 临时生效, 重启还原)
    bool buildUnlockEnabled = false;            // 总开关: 启用全部建造解锁
    bool buildIgnoreRequirements = false;       // 制作和建造无视需求(临时解锁全建造和制作样式)
    bool buildNoConsumeMaterial = false;        // 建造和制作不消耗材料(即使材料不够也能造, 够的话不消耗)
    bool buildIgnoreGroundPlacement = false;    // 无视"未放置在地面上"
    bool buildAllowOverlapTerminal = false;     // 允许重叠终端
    bool buildIgnoreNearBoss = false;           // 无视"距离特殊头目或设施过近"
    bool buildIgnoreOil = false;                // 无视"需放置于可采集原油的位置"
    bool buildIgnoreBaseLimit = false;          // 无视"基地已达建筑上限"
    bool buildIgnoreCampLimit = false;          // 无视"无法建造更多据点"
    bool buildIgnoreObstacle = false;           // 无视"路径存在障碍物"
    bool buildIgnoreOtherGuild = false;         // 无视"无法在其他公会基地建造"
    bool buildIgnoreSupport = false;            // 无视"支撑不足"
    bool buildIgnoreCeiling = false;            // 无视"放置天花板"
    bool buildFastBuild = false;                // 快速建造/制作
    bool buildIgnoreOverlap = false;            // 无视"与其他物体重叠"
    bool buildIgnoreGroundContact = false;      // 无视"所有地板必须与地面接触"
    bool buildIgnoreNearCamp = false;           // 无视"与其他据点过近"
    bool buildIgnoreUnderSea = false;           // 无视"无法在海平面以下建造"
    bool buildIgnoreHighPlace = false;          // 无视"无法在如此高处建造"
    bool buildIgnoreSlope = false;              // 无视"接触地面的斜面过度倾斜"
    bool buildIgnoreBaseRange = false;          // 无视"必须建造在基地范围内"
    bool buildIgnoreIndoor = false;             // 无视"建造在室内"
    bool buildIgnoreConnect = false;            // 无视"未连接至建筑"
    bool buildIgnoreWall = false;               // 无视"放置墙体"
    bool buildNoDismantle = false;              // 放置后禁止破坏

    // ===================================================
    // Pathfinding (寻路系统)
    // ===================================================
    bool    pathfindingEnabled = false;      // 总开关：启用寻路功能
    bool    pathVisualizationEnabled = true; // 路径可视化开关
    bool    pathAutoRecalc = true;           // 自动重算（玩家/目标移动时）
    bool    pathNavMeshOnly = false;         // 仅 NavMesh（禁用 A* 回退）
    float   pathColor[4] = { 0.0f, 0.8f, 1.0f, 1.0f };     // 路径线颜色 RGBA（默认亮青蓝）
    float   pathStartColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f }; // 起点标记颜色（绿色）
    float   pathEndColor[4] = { 1.0f, 0.2f, 0.2f, 1.0f };   // 终点标记颜色（红色）
    float   pathPartialColor[4] = { 1.0f, 0.8f, 0.0f, 1.0f }; // 部分路径（不可达段）颜色
    float   pathLineThickness = 3.0f;        // 路径线宽（像素）
    float   pathWaypointSize = 4.0f;         // 路径点标记大小（像素）
    float   pathRecalcThreshold = 2.0f;      // 移动超过此距离（米）自动重算
    int     pathMaxActive = 10;              // 最大同时活跃路径数
    int     pathThrottleMs = 100;            // 寻路计算最小间隔（毫秒）

    // Misc
    int  menuToggleKey = VK_INSERT; // 菜单显隐切换键
    bool useEnglishNames = false;   // 是否使用英文名称（默认 false: 显示中文）

    std::vector<BulkItem> bulkItems;
};

struct SWaypoint
{
    std::string waypointName;
    SDK::FVector waypointLocation;
    SDK::FRotator waypointRotation;

    SWaypoint(const std::string& name, const SDK::FVector& loc, const SDK::FRotator& rot)
        : waypointName(name), waypointLocation(loc), waypointRotation(rot) {
    }
};

struct Hotkeys
{
    int hotkeyToggleWorldSpeed = VK_F1;
    int hotkeyStamina = VK_F2;
    int hotkeyToggleESP = VK_F3;
    int hotkeyToggleRelic = VK_F4;
    int hotkeyToggleAttack = VK_F5;
    int hotkeyRepairWeapon = VK_F6;
	int hotkeyTeleportHome = VK_F7;
    int hotkeyRefreshWeight = VK_F8;
    int hotkeyFastTravelMap = 0x4D;

    bool worldSpeedToggled = false;
	bool staminaToggled = false;
    bool espToggled = false;
	bool relicToggled = false;
	bool attackToggled = false;
    bool repairWeapon = false;

};

namespace gSilent
{
    inline SDK::APalCharacter* targetPal = nullptr;
}

inline CheatState cheatState;
extern Hotkeys key;
inline std::vector<SWaypoint> g_Waypoints;
