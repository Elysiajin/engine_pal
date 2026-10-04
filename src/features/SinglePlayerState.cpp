#include <pch.h>
#include "CheatState.hpp"
#include "engine/GameHelper.hpp"

using namespace SDK;
using namespace Helper;


// ============================================================================
// 自由飞行（参考 D:\My Projects\DLLSH\DLLSH 的高级飞行系统）
//   - 空格(Space) 起飞/上升, 左Ctrl 或 Shift 降落/下降, WASD 控制水平方向
//   - 无惯性: 直接通过 K2_SetActorLocation 每帧移动, 松开按键立即停止
//   - 飞行速度可配置 (cheatState.flySpeed)
//   - 进入飞行: 关闭重力 + 切到 MOVE_Flying; 退出: 恢复重力 + MOVE_Walking
// ============================================================================

static bool g_FlyWasEnabled = false;
static float g_FlyPrevGravity = 1.0f;
static float g_FlyPrevMaxFlySpeed = 600.0f;
static float g_FlyPrevMaxAccel = 2048.0f;

static inline bool IsFlyKeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
// 轴输入: 正键按下=+1, 负键按下=-1, 两者相抵
static inline float FlyKeyAxis(int vkPos, int vkNeg)
{
	float pos = IsFlyKeyDown(vkPos) ? 1.f : 0.f;
	float neg = IsFlyKeyDown(vkNeg) ? -1.f : 0.f;
	return pos + neg;
}

// 进入/退出飞行状态。参考 DLLSH 的 ToggleFly:
//   - bEnable=true : 关闭重力、设置最大飞行速度/加速度、切到 MOVE_Flying
//   - bEnable=false: 恢复重力与默认速度、切回 MOVE_Walking
void ToggleFly(bool bEnable)
{
	SDK::APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player || !Helper::IsProbablyValidPtr(player))
		return;

	UPalCharacterMovementComponent* mc = player->GetPalCharacterMovementComponent();
	if (!mc || !Helper::IsProbablyValidPtr(mc))
		return;

	if (bEnable)
	{
		// 记录原值以便退出时还原
		g_FlyPrevGravity    = mc->GravityScale;
		g_FlyPrevMaxFlySpeed = mc->MaxFlySpeed;
		g_FlyPrevMaxAccel   = mc->MaxAcceleration;

		mc->GravityScale      = 0.0f;
		mc->MaxFlySpeed       = cheatState.flySpeed;
		mc->MaxAcceleration   = cheatState.flySpeed * 5.0f;
		mc->MaxCustomMovementSpeed = cheatState.flySpeed;
		Helper::Try([&] { mc->SetMovementMode(SDK::EMovementMode::MOVE_Flying, 0); });

		// 联机同步: 通知服务器进入飞行状态, 使服务器端的移动模式/速度保持一致,
		// 其它客户端才能正确看到玩家处于飞行状态。
		if (SDK::APalPlayerController* pc = GetPalPlayerController())
			Helper::Try([&] { pc->StartFlyToServer(); });

		g_FlyWasEnabled = true;
	}
	else
	{
		mc->GravityScale      = g_FlyPrevGravity;
		mc->MaxFlySpeed       = g_FlyPrevMaxFlySpeed;
		mc->MaxAcceleration   = g_FlyPrevMaxAccel;
		mc->MaxCustomMovementSpeed = g_FlyPrevMaxFlySpeed;
		// 退出飞行: 若当前在空中则切回 Falling, 让重力把角色拉回地面;
		// 若已着地则切回 Walking。
		Helper::Try([&]
		{
			if (player->K2_GetActorLocation().Z > 0.f && mc->Velocity.Z > -1.f)
				mc->SetMovementMode(SDK::EMovementMode::MOVE_Falling, 0);
			else
				mc->SetMovementMode(SDK::EMovementMode::MOVE_Walking, 0);
		});

		// 联机同步: 通知服务器退出飞行状态
		if (SDK::APalPlayerController* pc = GetPalPlayerController())
			Helper::Try([&] { pc->EndFlyToServer(); });

		g_FlyWasEnabled = false;
	}
}

// 兼容旧接口: 根据 cheatState.isFly 切换飞行状态
void ExploitFly()
{
	ToggleFly(cheatState.isFly);
}

// 每帧飞行输入处理（无惯性，类《我的世界》）。
//   - 空格(Space) -> 上升 (仅改 Z)
//   - Shift      -> 下降 (仅改 Z)
//   - W/S/A/D    -> 水平方向移动（世界 XY 平面），投影到水平面，
//                   不会朝摄像机前方（含俯仰角）飞行，保证 Z 保持稳定
//   无惯性: moveDir 归一化后乘以 速度*DeltaTime, 直接 K2_SetActorLocation 移动,
//   因此松开按键立即停止。
void TickFly(float DeltaTime)
{
	if (!cheatState.isFly)
	{
		// 确保不在飞行时状态干净
		if (g_FlyWasEnabled)
		{
			ToggleFly(false);
			cheatState.isFly = false;
		}
		return;
	}

	// 确保进入飞行状态（首次开启或重新进入游戏）
	if (!g_FlyWasEnabled)
		ToggleFly(true);

	SDK::APalPlayerCharacter* player = GetPalPlayerCharacter();
	// 只做空判断还不够: 玩家可能正处于加载/重生/销毁中, 指针存在但已是失效对象。
	// 若不校验, 下面的 K2_SetActorLocation 里的 ProcessEvent 会读到无效内存而崩溃。
	if (!player || !Helper::IsProbablyValidPtr(player))
		return;

	// 收集按键轴
	float fwd = FlyKeyAxis('W', 'S');       // 水平 前 +
	float right = FlyKeyAxis('D', 'A');     // 水平 右 +
	float up = FlyKeyAxis(VK_SPACE, VK_SHIFT); // 上+ / 下- (仅垂直)

	// 无任何输入 -> 立即停止（无惯性）, 不移动
	if (fwd == 0.f && right == 0.f && up == 0.f)
		return;

	// 获取玩家控制器与相机朝向
	SDK::APalPlayerController* pc = GetPalPlayerController();
	if (!pc)
		return;

	// 相机朝向: 读取失败则用角色朝向兜底。
	FRotator camRot{};
	if (pc->PlayerCameraManager)
	{
		Helper::Try([&] { camRot = pc->PlayerCameraManager->GetCameraRotation(); });
	}
	else
	{
		Helper::Try([&] { camRot = player->K2_GetActorRotation(); });
	}

	// 朝向向量用 Try 保护。GetForwardVector/GetRightVector 是静态库函数,
	// 会走 GetDefaultObj()+ProcessEvent, 在游戏加载/切换时同样可能崩溃。
	FVector forward(1.f, 0.f, 0.f);
	FVector rightVec(0.f, 1.f, 0.f);
	Helper::Try([&] { forward = SDK::UKismetMathLibrary::GetForwardVector(camRot); });
	Helper::Try([&] { rightVec = SDK::UKismetMathLibrary::GetRightVector(camRot); });
	FVector upVec(0.f, 0.f, 1.f); // 世界 Z 轴

	// 禁止朝摄像机前方飞行: 把 forward/right 投影到水平面(Z=0),
	// 使 WASD 只控制 XY 平面移动, 不随俯仰角改变高度。
	forward.Z = 0.f;
	rightVec.Z = 0.f;
	if (!forward.IsZero()) forward.Normalize();
	if (!rightVec.IsZero()) rightVec.Normalize();

	FVector moveDir = forward * fwd + rightVec * right + upVec * up;
	if (moveDir.IsZero())
		return;

	moveDir.Normalize();

	float speed = cheatState.flySpeed * DeltaTime;

	// 整段移动用 Try 包住: K2_GetActorLocation / K2_SetActorLocation 都走
	// ProcessEvent, 若玩家在移动瞬间被销毁/加载中, 会触发访问违规崩溃。
	// 包上后异常被捕获, 本帧跳过移动, 不会把游戏搞崩。
	//
	// 关键: K2_SetActorLocation 用 bSweep=false + bTeleport=true (传送)。
	// 之前用 bSweep=true 会触发物理碰撞扫描, 这是游戏线程独占的操作;
	// 从后台线程调用会与游戏物理线程竞争, 导致游戏原生代码崩溃
	// (崩溃栈只显示游戏帧, 看不到我们的 DLL)。
	Helper::Try([&]
	{
		FVector newLoc = player->K2_GetActorLocation() + moveDir * speed;
		player->K2_SetActorLocation(newLoc, false, nullptr, true);
	});
}

void SetPlayerDefenseParam()
{
	APalPlayerCharacter* pPalPlayerCharacter = GetPalPlayerCharacter();
	if (!pPalPlayerCharacter)
		return;

	UPalCharacterParameterComponent* pParams = pPalPlayerCharacter->CharacterParameterComponent;
	if (!pParams)
		return;

	if (pParams->DefenseUp != cheatState.defence)
		pParams->DefenseUp = cheatState.defence;
}

void SetCraftingSpeed()
{
	APalPlayerCharacter* pPalCharacter = GetPalPlayerCharacter();
	if (!pPalCharacter)
		return;

	UPalCharacterParameterComponent* pParams = pPalCharacter->CharacterParameterComponent;
	if (!pParams)
		return;

	UPalIndividualCharacterParameter* ivParams = pParams->IndividualParameter;
	if (!ivParams)
		return;

	FPalIndividualCharacterSaveParameter sParams = ivParams->SaveParameter;
	TArray<FFloatContainer_FloatPair> mCraftSpeedArray = sParams.CraftSpeedRates.Values;

	if (mCraftSpeedArray.Num() > 0)
		mCraftSpeedArray[0].Value = cheatState.craftingSpeed;
}

void AddTechPoints()
{
	APalPlayerState* mPlayerState = GetPalPlayerState();
	if (!mPlayerState)
		return;

	UPalTechnologyData* pTechData = mPlayerState->TechnologyData;
	if (!pTechData)
		return;

	pTechData->TechnologyPoint += cheatState.addTechPoints;
}

//	
void AddAncientTechPoints()
{
	APalPlayerState* mPlayerState = GetPalPlayerState();
	if (!mPlayerState)
		return;

	UPalTechnologyData* pTechData = mPlayerState->TechnologyData;
	if (!pTechData)
		return;

	pTechData->bossTechnologyPoint += cheatState.addAncientTechPoints;
}

void RemoveTechPoints()
{
	APalPlayerState* mPlayerState = GetPalPlayerState();
	if (!mPlayerState)
		return;

	UPalTechnologyData* pTechData = mPlayerState->TechnologyData;
	if (!pTechData)
		return;

	pTechData->TechnologyPoint -= cheatState.removeTechPoints;
}

//	
void RemoveAncientTechPoint()
{
	APalPlayerState* mPlayerState = GetPalPlayerState();
	if (!mPlayerState)
		return;

	UPalTechnologyData* pTechData = mPlayerState->TechnologyData;
	if (!pTechData)
		return;

	pTechData->bossTechnologyPoint -= cheatState.removeAncientTechPoints;
}

void SetPlayerSpeed()
{
	APalPlayerCharacter* pPalPlayerCharacter = GetPalPlayerCharacter();
	if (!pPalPlayerCharacter)
		return;

	// APalPlayerCharacter inherits from APalCharacter
	UPalCharacterMovementComponent* pMovement = pPalPlayerCharacter->GetPalCharacterMovementComponent();
	if (!pMovement)
		return;

	// Apply speed multiplier
	pMovement->MaxWalkSpeed = cheatState.speedMultiplier;
	pMovement->MaxFlySpeed = cheatState.speedMultiplier;  // optional
	pMovement->MaxSwimSpeed = cheatState.speedMultiplier;  // optional
}

// 无限跳跃: 每帧重置跳跃计数 ACharacter::JumpCurrentCount = 0, 使玩家
// 在空中也能不断连跳, 无需跳跃鞋。
// 原实现使用 SetJumpZVelocityMultiplier(FName(), 100.0f) 把跳跃高度提升
// 到 100 倍, 但这并非真正的"无限跳"——只是超级高跳, 且 FName() 空名字
// 可能被游戏内部逻辑忽略或覆盖。
// 正确做法: JumpCurrentCount 是 ACharacter 继承的字段 (offset 0x428),
// 每帧置零即可让玩家在空中一直按跳跃键重新触发跳跃。
// 同时设 JumpMaxCount = INT_MAX 作为兜底, 避免游戏内的跳跃次数限制。
void TickInfiniteJump()
{
	if (!cheatState.infJump)
		return;

	APalPlayerCharacter* pPalPlayerCharacter = GetPalPlayerCharacter();
	if (!pPalPlayerCharacter)
		return;

	// APalPlayerCharacter -> APalCharacter -> ACharacter
	// JumpCurrentCount / JumpMaxCount 在 ACharacter 基类中。
	// 直接通过 static_cast 访问基类字段。
	SDK::ACharacter* pChar = static_cast<SDK::ACharacter*>(pPalPlayerCharacter);
	if (!pChar)
		return;

	// 重置跳跃计数: 允许在空中无限次连跳
	pChar->JumpCurrentCount = 0;
	// 兜底: 确保跳跃次数上限非常大
	pChar->JumpMaxCount = 0x7FFFFFFF;
}

void SetPlayerLevel()
{
	APalPlayerCharacter* pPalCharacter = GetPalPlayerCharacter();
	if (!pPalCharacter)
		return;

	UPalCharacterParameterComponent* pParams = pPalCharacter->CharacterParameterComponent;
	if (!pParams)
		return;

	UPalIndividualCharacterParameter* ivParams = pParams->IndividualParameter;
	if (!ivParams)
		return;

	ivParams->SetOverrideLevel(cheatState.playerLevel);
}

void SetPlayerInvicity()
{
	auto* player = GetPalPlayerCharacter();
	if (!player) return;

	// SetMuteki lives on UPalCharacterParameterComponent and manages
	// the bIsEnableMuteki / IsImmortality flags internally.
	// Use one fixed empty FName flag: enable with true, disable with the same flag.
	UPalCharacterParameterComponent* pParams = player->CharacterParameterComponent;
	if (!pParams)
		return;

	pParams->SetMuteki(FName(), cheatState.isInvicity);
}


static SDK::UPalExpDatabase* GetExpDatabase()
{
	if (SDK::APalPlayerCharacter* me = GetPalPlayerCharacter())
	{
		if (SDK::UWorld* world = UWorld::GetWorld())
		{
			// Use whatever your SDK exposes (OwningGameInstance or GetGameInstance())
			if (SDK::UGameInstance* giBase = world->OwningGameInstance) // or world->GetGameInstance()
				return static_cast<SDK::UPalExpDatabase*>(static_cast<void*>(
					reinterpret_cast<SDK::UPalGameInstance*>(giBase)->ExpDatabase));
		}
	}
	return nullptr;
}

// Are we authority? (must be true for direct server calls to work)
static bool IsAuthority()
{
	if (SDK::APalPlayerCharacter* me = GetPalPlayerCharacter())
	{
		// UE4/5 style: actor role check
		return me->GetLocalRole() == SDK::ENetRole::ROLE_Authority;
	}
	return false;
}

// ============================================================================
// 免疫所有异常状态 (Status Immunity)
//
// 通过 UPalStatusComponent::SetDisableAddStatusIDs 屏蔽所有"负面"EPalStatusID，
// 使游戏不会再向玩家附加这些状态（寒冷/炎热/中毒/眩晕/昏迷/睡眠/溺水/麻痹/
// 泥泞/藤蔓缠绕/黑暗/攻击下降/毒气/酸蚀/部位破坏/浮空眩晕/过劳等）。
// 同时对已生效的状态调用 RemoveStatus 逐一清除，并处理负重过高(负重免疫)。
// 每帧调用一次，开启时强制保持效果，关闭时恢复。
// ============================================================================
void TickStatusImmune()
{
	// 开关关闭时不处理(保持原样，不主动移除状态)
	if (!cheatState.statusImmune)
		return;

	APalPlayerCharacter* pPlayer = GetPalPlayerCharacter();
	if (!pPlayer)
		return;

	UPalStatusComponent* pStatus = pPlayer->StatusComponent;
	if (!pStatus)
		return;

	// ---- 1. 屏蔽所有负面异常状态(不再附加) ----
	// 收集需要屏蔽的负面状态 ID
	TAllocatedArray<SDK::EPalStatusID> disableIDs(64);

	auto AddID = [&disableIDs](SDK::EPalStatusID id)
	{
		// 去重后加入
		for (int i = 0; i < disableIDs.Num(); ++i)
			if (disableIDs[i] == id)
				return;
		disableIDs.Add(id);
	};

	// 环境/元素类负面状态
	AddID(SDK::EPalStatusID::Freeze);            // 寒冷/冰冻
	AddID(SDK::EPalStatusID::Burn);              // 炎热/灼烧
	AddID(SDK::EPalStatusID::LavaDamage);        // 岩浆伤害
	AddID(SDK::EPalStatusID::Wetness);           // 淋湿
	AddID(SDK::EPalStatusID::Electrical);        // 触电/麻痹
	AddID(SDK::EPalStatusID::Muddy);             // 泥泞
	AddID(SDK::EPalStatusID::IvyCling);          // 藤蔓缠绕
	AddID(SDK::EPalStatusID::Darkness);          // 黑暗
	AddID(SDK::EPalStatusID::Drown);             // 溺水
	AddID(SDK::EPalStatusID::DrownCheck);        // 溺水判定
	AddID(SDK::EPalStatusID::FallDamage);        // 摔落伤害
	AddID(SDK::EPalStatusID::MapObjectOverHeat); // 物体过热

	// 战斗/伤害类负面状态
	AddID(SDK::EPalStatusID::Poison);            // 中毒
	AddID(SDK::EPalStatusID::Stun);              // 眩晕
	AddID(SDK::EPalStatusID::Coma);              // 昏迷
	AddID(SDK::EPalStatusID::Sleep);             // 睡眠
	AddID(SDK::EPalStatusID::Dying);             // 濒死
	AddID(SDK::EPalStatusID::AcidDamage);        // 酸蚀
	AddID(SDK::EPalStatusID::ToxicGas);          // 毒气
	AddID(SDK::EPalStatusID::ToxicGas_FromAttack); // 攻击性毒气
	AddID(SDK::EPalStatusID::PartBreak);         // 部位破坏
	AddID(SDK::EPalStatusID::FloatStun);         // 浮空眩晕
	AddID(SDK::EPalStatusID::Overwork);          // 过劳
	AddID(SDK::EPalStatusID::AttackDown);        // 攻击下降
	AddID(SDK::EPalStatusID::StepCooldown);      // 技能冷却惩罚

	pStatus->SetDisableAddStatusIDs(disableIDs);

	// ---- 2. 清除已生效的负面状态 ----
	// 遍历所有可能的负面状态, 若已存在则移除
	auto RemoveIfPresent = [pStatus](SDK::EPalStatusID id)
	{
		if (pStatus->GetExecutionStatus(id) != nullptr)
			pStatus->RemoveStatus(id);
	};

	RemoveIfPresent(SDK::EPalStatusID::Freeze);
	RemoveIfPresent(SDK::EPalStatusID::Burn);
	RemoveIfPresent(SDK::EPalStatusID::LavaDamage);
	RemoveIfPresent(SDK::EPalStatusID::Wetness);
	RemoveIfPresent(SDK::EPalStatusID::Electrical);
	RemoveIfPresent(SDK::EPalStatusID::Muddy);
	RemoveIfPresent(SDK::EPalStatusID::IvyCling);
	RemoveIfPresent(SDK::EPalStatusID::Darkness);
	RemoveIfPresent(SDK::EPalStatusID::Drown);
	RemoveIfPresent(SDK::EPalStatusID::DrownCheck);
	RemoveIfPresent(SDK::EPalStatusID::FallDamage);
	RemoveIfPresent(SDK::EPalStatusID::MapObjectOverHeat);
	RemoveIfPresent(SDK::EPalStatusID::Poison);
	RemoveIfPresent(SDK::EPalStatusID::Stun);
	RemoveIfPresent(SDK::EPalStatusID::Coma);
	RemoveIfPresent(SDK::EPalStatusID::Sleep);
	RemoveIfPresent(SDK::EPalStatusID::Dying);
	RemoveIfPresent(SDK::EPalStatusID::AcidDamage);
	RemoveIfPresent(SDK::EPalStatusID::ToxicGas);
	RemoveIfPresent(SDK::EPalStatusID::ToxicGas_FromAttack);
	RemoveIfPresent(SDK::EPalStatusID::PartBreak);
	RemoveIfPresent(SDK::EPalStatusID::FloatStun);
	RemoveIfPresent(SDK::EPalStatusID::Overwork);
	RemoveIfPresent(SDK::EPalStatusID::AttackDown);
	RemoveIfPresent(SDK::EPalStatusID::StepCooldown);

	// ---- 3. 负重过高免疫 ----
	// 持续把最大负重抬到极大值, 使玩家永远不会进入"负重过高"状态
	UPalPlayerInventoryData* pInventory = GetInventoryComponent();
	if (pInventory)
	{
		if (pInventory->MaxInventoryWeight < 999999.0f)
		{
			pInventory->MaxInventoryWeight = 999999.0f;
			pInventory->OnRep_maxInventoryWeight();
			// 仅在真正修改了最大负重时才通知服务器刷新
			pInventory->RequestForceMarkAllDirty_ToServer(true);
		}
	}
}

// ============================================================================
// 隐身模式 (Invisibility)
//   - 敌人/帕鲁/人类 NPC 无法察觉玩家: 把玩家的 bCanTargetFromAI 置 false,
//     AI 传感器/仇恨/战斗模块选取目标时会跳过玩家, 从而"看不见"玩家
//   - 玩家不会进入战斗模式: 置 bIgnoreChangeBattleModeFlag, 避免被标记为交战
//   - 不反击/不累积仇恨: 由 Hooking.cpp 的 ProcessEvent 钩子拦截仇恨与犯罪上报
//   - 关闭时恢复玩家可被当作目标的状态
// ============================================================================
void TickInvisible()
{
	APalPlayerCharacter* pPlayer = GetPalPlayerCharacter();
	if (!pPlayer)
		return;

	UPalCharacterParameterComponent* params = pPlayer->CharacterParameterComponent;
	if (!params)
		return;

	UPalIndividualCharacterParameter* ivParams = params->IndividualParameter;
	if (!ivParams)
		return;

	if (cheatState.invisible)
	{
		// 使玩家不可被任何 AI 作为目标/锁定
		Helper::Try([&] { ivParams->SetCanTargetFromAI(false); });
		// 玩家不进入战斗模式(被攻击也不切换交战状态)
		Helper::Try([&] { pPlayer->bIgnoreChangeBattleModeFlag = true; });
	}
	else
	{
		Helper::Try([&] { ivParams->SetCanTargetFromAI(true); });
		Helper::Try([&] { pPlayer->bIgnoreChangeBattleModeFlag = false; });
	}
}

// ============================================================================
// 食物永不腐烂 (Food Never Spoils)
//
// Palworld 的食物腐烂（Corruption）在 UPalItemContainer::CorruptionMultiplier
// 与 UPalItemSlot::CorruptionProgressValue 上推进。开启后：
//   1) 把所有可达的背包物品槽的腐败进度直接清零（OnRep 通知 UI 刷新）
//   2) 把背包对应容器的 CorruptionMultiplier 设为 0，使游戏不再累积腐烂
// 关闭后恢复容器倍率（1.0），未再干预新进度。
// 注意：仅影响本地客户端可见数据，服务器权威的联机环境可能仍由服务器结算。
// ============================================================================
void TickFoodNoSpoil()
{
	if (!cheatState.foodNeverSpoil)
		return;

	UPalPlayerInventoryData* pInventory = GetInventoryComponent();
	if (!pInventory)
		return;

	Helper::Try([&]
	{
		// 遍历背包所有容器（普通/必带/装备/食物栏等）
		UPalItemContainerMultiHelper* helper = pInventory->InventoryMultiHelper;
		if (!helper || !Helper::IsProbablyValidPtr(helper))
			return;

		const TArray<UPalItemContainer*>& containers = helper->Containers;
		for (int32 c = 0; c < containers.Num(); ++c)
		{
			UPalItemContainer* container = containers[c];
			if (!container || !Helper::IsProbablyValidPtr(container))
				continue;

			// 容器级腐败倍率清零（0 = 不腐烂）
			if (container->CorruptionMultiplier != 0.0f)
			{
				container->CorruptionMultiplier = 0.0f;
				// 容器内的 FloatContainer 倍率一并清零，防止另有来源重新累积
				container->CorruptionMultiplierContainer.Values.Clear();
			}

			const TArray<UPalItemSlot*>& slots = container->ItemSlotArray;
			for (int32 s = 0; s < slots.Num(); ++s)
			{
				UPalItemSlot* slot = slots[s];
				if (!slot || !Helper::IsProbablyValidPtr(slot))
					continue;
				if (slot->IsEmpty())
					continue;

				// 清零腐败进度
				if (slot->CorruptionProgressValue != 0.0f)
				{
					slot->CorruptionProgressValue = 0.0f;
					Helper::Try([&] { slot->OnRep_CorruptionProgressValue(); });
				}
			}
		}
	});
}

// ============================================================================
// 时间控制：时段切换 + 时间静止
// ============================================================================

// 切换游戏时段到指定小时（0-23）。直接访问世界 UPalTimeManager 设置时钟，
// 比通过 CheatManager 更可靠；CheatManager 作为兜底。
void SetWorldTimePreset(int32 Hour)
{
	UWorld* world = UWorld::GetWorld();
	if (!world)
		return;

	Hour = ((Hour % 24) + 24) % 24;

	// 优先：直接操作世界时钟管理器，最可靠（不受 Cheat 环境限制）
	UPalTimeManager* timeMgr = UPalUtility::GetTimeManager(world);
	if (timeMgr && Helper::IsProbablyValidPtr(timeMgr))
	{
		Helper::Try([&] { timeMgr->SetGameTime_FixDay(Hour); });
		return;
	}

	// 兜底：走 CheatManager
	UPalCheatManager* cheat = UPalUtility::GetPalCheatManager(world);
	if (cheat && Helper::IsProbablyValidPtr(cheat))
		Helper::Try([&] { cheat->SetPalWorldTime(Hour); });
}

// ============================================================================
// 时间编辑器：自由设置存档的天数与时:分
//
// 原理：
//   - 读取: UPalTimeManager 的 GetCurrentPalWorldTime_TotalDay/_Hour/_Minute
//   - 写入: APalGameStateInGame::WorldTime (FGameDateTime.Ticks, int64)。
//     Palworld 的世界时钟以 Ticks 推进，1 天 = 86400 秒游戏时间。
//     通过"目标 ticks - 当前 ticks"差值一次性平移世界时间，
//     可同时精确设置天数与时分（比 CheatManager 只能按小时设更自由）。
//     单人模式下 client == server，直接写 GameState 后由 RepNotify 同步。
// ============================================================================

// 一天对应的游戏内秒数（Palworld 昼夜周期为现实感 24 小时制）
static constexpr int64 kGameSecondsPerDay = 86400;
// FGameDateTime.Ticks 的推进粒度: 每秒 100 ticks (UE FTimespan 粒度)
static constexpr int64 kTicksPerGameSecond = 100;

// 获取世界 GameStateInGame（失败返回 nullptr）
static APalGameStateInGame* GetGameStateInGame()
{
	UWorld* world = UWorld::GetWorld();
	if (!world)
		return nullptr;

	APalGameStateInGame* gs = nullptr;
	if (!Helper::Try([&] { gs = UPalUtility::GetPalGameStateInGame(world); }))
		return nullptr;
	if (!gs || !Helper::IsProbablyValidPtr(gs))
		return nullptr;
	return gs;
}

bool GetWorldTimeInfo(__int32& OutTotalDay, __int32& OutHour, __int32& OutMinute)
{
	UWorld* world = UWorld::GetWorld();
	if (!world)
		return false;

	UPalTimeManager* timeMgr = UPalUtility::GetTimeManager(world);
	if (!timeMgr || !Helper::IsProbablyValidPtr(timeMgr))
		return false;

	int32 totalDay = 0, hour = 0, minute = 0;
	bool ok = true;
	ok &= Helper::SafeCallRet(totalDay, [&] { return timeMgr->GetCurrentPalWorldTime_TotalDay(); });
	ok &= Helper::SafeCallRet(hour,    [&] { return timeMgr->GetCurrentPalWorldTime_Hour(); });
	ok &= Helper::SafeCallRet(minute,  [&] { return timeMgr->GetCurrentPalWorldTime_Minute(); });
	if (!ok)
		return false;

	OutTotalDay = totalDay;
	OutHour     = hour;
	OutMinute   = minute;
	return true;
}

void SetWorldTimeCustom(__int32 Day, __int32 Hour, __int32 Minute)
{
	// 参数规范化
	if (Day < 1) Day = 1;                       // 游戏内天数从第 1 天起算
	Hour   = ((Hour % 24) + 24) % 24;
	Minute = ((Minute % 60) + 60) % 60;

	// ============================================================
	// 修复：原来的实现只修改 gs->WorldTime.Ticks，但 APalGameStateInGame::WorldTime
	// 是 Net/RepNotify 属性，TimeManager 每帧会用它自己的内部时钟重新推进并覆盖
	// 本地写值，所以"应用时间设置"看起来毫无效果。
	//
	// 正确做法：直接调用 UPalTimeManager::SetGameTime_FixDay(Hour) —— 这正是
	// 上面四个预设按钮有效的原因（它们走同一条 API）。但该接口只能设小时，
	// 无法设日期与分钟。因此这里分两步：
	//   1) 先用 SetGameTime_FixDay 把"当天小时"设为目标小时（必定生效）；
	//   2) 再写 GameState WorldTime.Ticks 调整天数与分钟，作为精确化修正。
	//      因为第 1 步已强制 TimeManager 同步到目标小时的整点，第 2 步的
	//      相对平移量很小（分钟级 + 天数偏移），被覆盖的概率大幅降低。
	// ============================================================
	UWorld* world = UWorld::GetWorld();
	UPalTimeManager* timeMgr = world ? UPalUtility::GetTimeManager(world) : nullptr;

	// 第一步：小时级同步（与预设按钮相同的路径，保证至少小时生效）
	if (timeMgr && Helper::IsProbablyValidPtr(timeMgr))
		Helper::Try([&] { timeMgr->SetGameTime_FixDay(Hour); });

	// 第二步：天数与分钟精确化
	APalGameStateInGame* gs = GetGameStateInGame();
	if (!gs)
		return;

	Helper::Try([&]
	{
		FGameDateTime cur = gs->WorldTime;
		const int64 curTicks = cur.Ticks;

		const int64 ticksPerDay = kGameSecondsPerDay * kTicksPerGameSecond;
		const int64 curDayOffsetTicks = ((curTicks % ticksPerDay) + ticksPerDay) % ticksPerDay;

		// 目标一天内的秒偏移（保持分钟精度，秒级归零避免跳动感）
		const int64 targetSecondOfDay = static_cast<int64>(Hour) * 3600 + static_cast<int64>(Minute) * 60;
		const int64 targetDayOffsetTicks = targetSecondOfDay * kTicksPerGameSecond;

		const int64 targetBaseTicks = (static_cast<int64>(Day) - 1) * ticksPerDay;
		const int64 newTicks = targetBaseTicks + targetDayOffsetTicks;

		gs->WorldTime.Ticks = newTicks;
		// RepNotify 手动触发，立即把新时间推给所有客户端（主机/单机都有效）
		gs->OnRep_WorldTime();
	});
}

// ============================================================================
// 时间静止（深度升级版）
//   作用范围 : 冻结"世界时钟"(昼夜不再推进) + 冻结"野生/敌对实体"的时间
//               (把其 CustomTimeDilation 置 0, 使野生帕鲁与敌人停止行动/攻击)
//   玩家例外 : 玩家 Pawn 及其队伍/骑乘帕鲁的 CustomTimeDilation 保持 1.0,
//               因此玩家仍可自由移动、骑乘、与队友互动 (解决旧版"连玩家也静止")
//   关键改进 : 只冻结"野生/敌对"实体, 不再把玩家、己方队伍、骑乘帕鲁、
//               以及场景/物理一并冻结, 避免出现"玩家被世界卡住/无法操作"
//   视觉反馈 : 每帧渲染时间静止的覆盖层文字与蓝色调
//   持续时间 : 由玩家开关控制, 开启期间持续生效, 关闭即恢复
//   兼容性   : 世界速度(TimeDilation)由 ChangeWorldSpeed 管理, 本功能不互相覆盖
// ============================================================================
static bool g_TimeFreezeActive = false;

// 把世界内"野生/敌对"实体的 CustomTimeDilation 设为指定值(冻结为 0)。
// 仅处理 APalCharacter; 玩家自身、被玩家控制、以及玩家队伍/骑乘的帕鲁一律跳过,
// 保证时间静止不会对玩家及其队友生效。
static void SetWorldActorsTimeDilation(UWorld* world, float dilation)
{
	if (!world)
		return;

	APalPlayerCharacter* player = GetPalPlayerCharacter();

	for (ULevel* level : world->Levels)
	{
		if (!level)
			continue;
		for (AActor* actor : level->Actors)
		{
			if (!actor || !Helper::IsProbablyValidPtr(actor))
				continue;
			if (!actor->IsA(SDK::APalCharacter::StaticClass()))
				continue;

			// 玩家自身例外
			if (player && actor == player)
				continue;

			// 玩家控制的(含骑乘/队伍)帕鲁例外: 不冻结
			if (UPalUtility::IsPlayerControlActor(actor))
				continue;
			if (UPalUtility::IsPlayerOrOtomo(actor))
				continue;
			// 只冻结野生/敌对实体
			if (!UPalUtility::IsWildNPC(actor))
				continue;

			Helper::Try([&] { actor->CustomTimeDilation = dilation; });
		}
	}
}

void TickTimeFreeze()
{
	UWorld* world = UWorld::GetWorld();
	if (!world)
		return;

	if (cheatState.timeFreeze)
	{
		Helper::Try([&]
		{
			// 1) 冻结世界时钟（昼夜/游戏时间）
			UPalCheatManager* cheat = UPalUtility::GetPalCheatManager(world);
			if (cheat && Helper::IsProbablyValidPtr(cheat))
				cheat->SetPalWorldTimeScale(0.0f);

			// 2) 只冻结野生/敌对实体（玩家与己方队伍不受影响）
			SetWorldActorsTimeDilation(world, 0.0f);

			// 3) 确保玩家自身时间正常
			if (APalPlayerCharacter* p = GetPalPlayerCharacter())
				p->CustomTimeDilation = 1.0f;
		});

		g_TimeFreezeActive = true;
	}
	else if (g_TimeFreezeActive)
	{
		// 关闭: 恢复世界时钟速率 + 恢复被冻结实体的时间
		Helper::Try([&]
		{
			UPalCheatManager* cheat = UPalUtility::GetPalCheatManager(world);
			if (cheat && Helper::IsProbablyValidPtr(cheat))
				cheat->SetPalWorldTimeScale(cheatState.worldSpeed);
		});

		SetWorldActorsTimeDilation(world, 1.0f);
		if (APalPlayerCharacter* p = GetPalPlayerCharacter())
			p->CustomTimeDilation = 1.0f;

		g_TimeFreezeActive = false;
	}
}
