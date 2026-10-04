#include <pch.h>
#include "core/Log.hpp"
#include "CheatState.hpp"
#include "PalEditor.hpp"
#include "engine/GameHelper.hpp"

using namespace SDK;
using namespace Helper;

float GetDistanceToActor(AActor* pLocal, AActor* pTarget)
{
	if (!pLocal || !pTarget)
		return -1.f;

	FVector pLocation = pLocal->K2_GetActorLocation();
	FVector pTargetLocation = pTarget->K2_GetActorLocation();
	double distance = sqrt(pow(pTargetLocation.X - pLocation.X, 2.0) + pow(pTargetLocation.Y - pLocation.Y, 2.0) + pow(pTargetLocation.Z - pLocation.Z, 2.0));

	return distance / 100.0f;
}

void ChangeWorldSpeed(float speed)
{
	UWorld* pWorld = UWorld::GetWorld();
	if (!pWorld)
		return;

	ULevel* pLevel = pWorld->PersistentLevel;
	if (!pLevel)
		return;

	AWorldSettings* pWorldSettings = pLevel->WorldSettings;
	if (!pWorldSettings)
		return;

	pWorld->PersistentLevel->WorldSettings->TimeDilation = speed;
}


void SetPlayerAttackParam()
{
	APalPlayerCharacter* pPalPlayerCharacter = GetPalPlayerCharacter();
	if (!pPalPlayerCharacter)
		return;

	UPalCharacterParameterComponent* pParams = pPalPlayerCharacter->CharacterParameterComponent;
	if (!pParams)
		return;

	int value = cheatState.attack * 50;
	pParams->AttackUp = value;
}


void SetPlayerInventoryWeight()
{
	UPalPlayerInventoryData* pInventory = GetInventoryComponent();
	if (!pInventory)
		return;

	pInventory->MaxInventoryWeight = cheatState.weight;

	// Not really necessary, but just in case if there are checks in the game that rely on these values
	pInventory->OnRep_maxInventoryWeight();
	pInventory->OnRep_BuffMaxWeight();
	pInventory->OnRep_BuffCurrentWeight();


	// Force server inventory refresh
	pInventory->RequestForceMarkAllDirty_ToServer(true);
	
}

void SetInfiniteAmmo()
{
	APalPlayerCharacter* pPalCharacter = GetPalPlayerCharacter();
	if (!pPalCharacter)
		return;

	UPalShooterComponent* pShootComponent = pPalCharacter->ShooterComponent;
	if (!pShootComponent)
		return;

	APalWeaponBase* pWeapon = pShootComponent->HasWeapon;

	if (!pWeapon)
	{
		return;
	}

	pWeapon->IsRequiredBullet = cheatState.infAmmo ? false : true;
	// 同步普通攻击与右键副开火（部分武器消耗弹药走 alt-fire 路径）
	pWeapon->IsRequiredBulletForAltFire = cheatState.infAmmo ? false : true;
}

void IncreaseAllDurability()
{
	APalPlayerCharacter* player = nullptr;
	if (!Try([&] { player = GetPalPlayerCharacter(); }) || !IsProbablyValidPtr(player))
		return;

	UPalShooterComponent* pShootComponent = nullptr;
	if (!Try([&] { pShootComponent = player->ShooterComponent; }) || !IsProbablyValidPtr(pShootComponent))
		return;

	APalWeaponBase* pWeapon = nullptr;
	if (!Try([&] { pWeapon = pShootComponent->HasWeapon; }) || !IsProbablyValidPtr(pWeapon))
		return;

	float currentDurability = 0.f;
	if (!Try([&] { currentDurability = pWeapon->GetDurability(); }))
		return;

	UPalDynamicWeaponItemDataBase* dynData = nullptr;
	if (!Try([&] { dynData = pWeapon->TryGetDynamicWeaponData(); }) || !IsProbablyValidPtr(dynData))
		return;

	Try([&] { dynData->Durability = currentDurability + 99999.0f; });

}

// The "资源伤害" (weapon damage) multiplier is applied to resources only.
//
// 修复说明：修改武器静态数据 AttackValue 只能影响对生物的普通攻击，对树木、
// 岩石等资源（map object）的伤害不起作用。并且那样会让"对生物的伤害"也被放大，
// 与需求（对生物不生效）矛盾。
//
// 现在真正的倍率在 Hooking.cpp 的 ProcessEvent 钩子中完成：拦截两个 map-object
// 资源的伤害 RPC（RequestDamageMapObject_ToServer / RequestDamageFoliage_ToServer），
// 直接把 FPalDamageInfo.NativeDamageValue / BasePower 乘以 cheatState.weaponDamage。
// 无论空手还是使用武器，对资源的伤害都会放大，而生物（不走这两个 RPC）不受影响。
void SetWeaponDamage()
{
	// No-op：资源伤害倍率由 Hooking.cpp 的钩子统一处理。
	(void)cheatState.weaponDamage;
}

void SetInfiniteMagazine()
{
	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player) return;

	UPalShooterComponent* pShootComponent = player->ShooterComponent;
	if (!pShootComponent) return;

	APalWeaponBase* pWeapon = pShootComponent->HasWeapon;
	if (!pWeapon) return;

	pWeapon->IsInfinityMagazine = cheatState.infMag;

}

// 每帧调用：开关开启时持续把当前武器（含切换后新武器）的弹药相关字段写死，
// 解决"只改一次、换枪后失效"的问题；关闭时同步还原。
void TickInfiniteAmmo()
{
	if (!cheatState.infAmmo && !cheatState.infMag)
		return;

	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player) return;

	UPalShooterComponent* pShootComponent = player->ShooterComponent;
	if (!pShootComponent) return;

	APalWeaponBase* pWeapon = pShootComponent->HasWeapon;
	if (!pWeapon) return;

	if (cheatState.infAmmo)
	{
		pWeapon->IsRequiredBullet = false;
		pWeapon->IsRequiredBulletForAltFire = false;
	}
	else
	{
		pWeapon->IsRequiredBullet = true;
		pWeapon->IsRequiredBulletForAltFire = true;
	}

	if (cheatState.infMag)
		pWeapon->IsInfinityMagazine = true;
	else
		pWeapon->IsInfinityMagazine = false;
}

// 与 TickInfiniteAmmo 相同，但仅处理无限弹匣，供主循环单独调度。
void TickInfiniteMagazine()
{
	if (!cheatState.infMag)
		return;

	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player) return;

	UPalShooterComponent* pShootComponent = player->ShooterComponent;
	if (!pShootComponent) return;

	APalWeaponBase* pWeapon = pShootComponent->HasWeapon;
	if (!pWeapon) return;

	pWeapon->IsInfinityMagazine = true;
}

void ResetStamina()
{
	if (!cheatState.infStamina)
		return;

	APalPlayerCharacter* pPalCharacter = GetPalPlayerCharacter();
	if (!pPalCharacter)
		return;

	UPalCharacterParameterComponent* pParams = pPalCharacter->CharacterParameterComponent;
	if (!pParams)
		return;

	pParams->ResetSP();
}

FGuid GenerateGuidRandomly()
{
	// Seed the RNG once
	static bool seeded = false;
	if (!seeded)
	{
		std::srand(static_cast<unsigned int>(std::time(nullptr)));
		seeded = true;
	}

	FGuid guid;

	auto gen = []() -> uint32_t {
		return static_cast<uint32_t>((std::rand() << 16) ^ std::rand());
		};

	guid.A = gen();
	guid.B = gen();
	guid.C = gen();
	guid.D = gen();

	return guid;
}

void AddItemToInventoryByName(std::string itemName, int count)
{
	static UKismetStringLibrary* lib = UKismetStringLibrary::GetDefaultObj();

	APalPlayerCharacter* pPalPlayerCharacter = GetPalPlayerCharacter();
	APalPlayerState* pPalPlayerState = GetPalPlayerState();
	if (!pPalPlayerCharacter || !pPalPlayerState)
		return;

	SDK::UPalPlayerInventoryData* pInventoryData = pPalPlayerState->GetInventoryData();
	if (!pInventoryData)
		return;

	FName Name = lib->Conv_StringToName(FString(std::wstring(itemName.begin(), itemName.end()).c_str()));


	UPalItemContainer* container = nullptr;

	if (!pInventoryData->TryGetContainerFromStaticItemID(Name, &container) || !container)
	{
		pal::log::Log(pal::log::Level::Debug, "state", "[DEBUG] Could not resolve container for item!\n");
		return;
	}

	UPalItemSlot* slot = nullptr;
	if (!pInventoryData->TryGetEmptySlot(
		pInventoryData->GetInventoryTypeFromStaticItemID(Name),
		&slot) || !slot)
	{
		pal::log::Log(pal::log::Level::Debug, "state", "[DEBUG] No empty slot found!\n");
		return;
	}


	pInventoryData->AddItem_ServerInternal(Name, count, true, 0.0f, true);

	slot->ItemId.StaticId = Name;
	slot->StackCount = count;


	FGuid RequestID = GenerateGuidRandomly();
	FPalItemSlotId SlotId = slot->GetSlotId();
	TArray<FPalItemSlotIdAndNum> Froms;
	Froms.Add({ SlotId, count });

	// Move from SlotId to itself
	APalPlayerController* pPalPlayerController = GetPalPlayerController();
	pPalPlayerController->Transmitter->Item->RequestMove_ToServer(RequestID, SlotId, Froms);
	pInventoryData->RequestForceMarkAllDirty_ToServer(true);

}

// ============================================================================
// 联机服务器请求添加物品
//
// 通过 UPalNetworkPlayerComponent::RequestAddItem_ToServer 向服务器请求添加物品。
// 这是联机环境下正确的物品添加方式——服务器权威创建物品并同步到所有客户端，
// 物品会持久化保存（不同于客户端临时物品会在背包同步/重新登录时被移除）。
//
// SDK 调用链: APalPlayerController->Transmitter->Player->RequestAddItem_ToServer
// 参数:
//   StaticItemId     - 物品的 FName StaticId（如 "Item_Wood" / "Item_PalSphere"）
//   Count            - 添加数量
//   IsAssignPassive  - 是否随机分配被动词条（一般为 false）
// ============================================================================
void AddItemToInventoryByName_ToServer(std::string itemName, int count)
{
	if (itemName.empty() || count <= 0)
		return;

	static UKismetStringLibrary* lib = UKismetStringLibrary::GetDefaultObj();

	APalPlayerController* pPalPlayerController = GetPalPlayerController();
	if (!pPalPlayerController || !Helper::IsProbablyValidPtr(pPalPlayerController))
		return;

	FName itemId = lib->Conv_StringToName(FString(std::wstring(itemName.begin(), itemName.end()).c_str()));
	if (itemId.IsNone())
		return;

	Helper::Try([&]
	{
		if (!pPalPlayerController->Transmitter || !Helper::IsProbablyValidPtr(pPalPlayerController->Transmitter))
			return;
		auto* playerNet = pPalPlayerController->Transmitter->Player;
		if (!playerNet || !Helper::IsProbablyValidPtr(playerNet))
			return;
		// 服务器权威添加物品——联机下物品会正确同步并持久化
		playerNet->RequestAddItem_ToServer(itemId, count, false);
	});
}

void TeleportPlayerTo(const FVector& pos)
{
	APalPlayerState* pPalPlayerState = GetPalPlayerState();
	APalPlayerController* pPalPlayerController = GetPalPlayerController();

	if (!pPalPlayerController || !pPalPlayerState ||
		!Helper::IsProbablyValidPtr(pPalPlayerController) ||
		!Helper::IsProbablyValidPtr(pPalPlayerState))
		return;

	// 确保玩家可以"随处传送"(不要求站在传送塔/石像旁)。
	// 打开地图时 Hooking 已把 WorldMap 的 CanFastTravel 置为 true,
	// 这里自定义/坐标传送本身走 RegisterRespawnPoint + TeleportToSafePoint,
	// 不依赖传送塔, 因此天然支持随处传送。

	// 传送调用走 ProcessEvent, 用 Try 包住避免玩家销毁/加载中崩溃。
	Helper::Try([&]
	{
		// to avoid spawning inside terrain +100
		FVector safeLocation = FVector(pos.X, pos.Y + 100.0f, pos.Z);
		FQuat defaultRotation(0.f, 0.f, 0.f, 1.f);

		if (!pPalPlayerController->Transmitter || !pPalPlayerController->Transmitter->Player ||
			!Helper::IsProbablyValidPtr(pPalPlayerController->Transmitter->Player))
			return;

		// Get player unique ID (needed for server call)
		FGuid guid = pPalPlayerState->PlayerUId;

		pPalPlayerController->Transmitter->Player->RegisterRespawnPoint_ToServer(guid, safeLocation, defaultRotation);
		pPalPlayerController->TeleportToSafePoint_ToServer(false);
	});
}

void TeleportPlayerToHome()
{
	TArray<SDK::APalCharacter*> allPals;
	if (!GetTAllPals(&allPals))
		return;

	for(SDK::APalCharacter* pal : allPals)
	{
		if (!pal)
			continue;
		APalPlayerCharacter* palPlayer = static_cast<APalPlayerCharacter*>(pal);
		if (!palPlayer)
			continue;

		if (IsAlive(palPlayer) && IsABaseWorker(palPlayer))
		{
			FVector homeLocation = palPlayer->K2_GetActorLocation();
			TeleportPlayerTo(homeLocation);
			return;
		}	
	}
}

void SetCameraFov()
{
	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player) return;

	auto cameraComp = player->FollowCamera;
	if (!cameraComp) return;

	cameraComp->SetFieldOfView(cheatState.cameraFov);
	cameraComp->AimFOV = cheatState.cameraFov;
}

void SetCameraBrightness()
{
	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player) return;

	UPalCharacterCameraComponent* cameraComp = player->FollowCamera;
	if (!cameraComp) return;

	cameraComp->PostProcessSettings.bOverride_AutoExposureBias = true;
	cameraComp->PostProcessBlendWeight = 1.0f;

	cameraComp->PostProcessSettings.AutoExposureBias = cheatState.cameraBrightness;
}

void AddWaypointLocation(const std::string& wpName)
{
	APalCharacter* pPalCharacter = GetPalPlayerCharacter();
	if (!pPalCharacter) return;

	SDK::FVector wpLocation = pPalCharacter->K2_GetActorLocation();
	SDK::FRotator wpRotation = pPalCharacter->K2_GetActorRotation();

	g_Waypoints.emplace_back("[WAYPOINT] " + wpName, wpLocation, wpRotation);
}

bool RemoveWaypointLocationByName(const std::string& wpName)
{
	for (auto it = g_Waypoints.begin(); it != g_Waypoints.end(); ++it)
	{
		if (it->waypointName == wpName)
		{
			g_Waypoints.erase(it);
			return true;
		}
	}
	return false;
}

void CollectAllRelicsInMap()
{
	UWorld* world = UWorld::GetWorld();
	if (!world) return;

	APalPlayerController* controller = GetPalPlayerController();
	if (!controller || !controller->Transmitter || !controller->Transmitter->Player)
	{
		return;
	}

	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player || !player->InteractComponent)
	{
		return;
	}

	AActor* nearestRelic = nullptr;
	float nearestDistance = FLT_MAX;
	FVector playerLoc = player->K2_GetActorLocation();

	for (ULevel* level : world->Levels)
	{
		if (!level) continue;

		for (AActor* actor : level->Actors)
		{
			if (!actor) continue;

			std::string className;
			try { className = actor->Class->GetName(); }
			catch (...) { continue; }

			// Match relic class
			if (className == "BP_LevelObject_Relic_C" || className.find("PalLevelObjectRelic") != std::string::npos)
			{
				APalLevelObjectObtainable* relic = reinterpret_cast<APalLevelObjectObtainable*>(actor);
				if (!relic) continue;

				if (relic->bPickedInClient)
				{
					continue;
				}

				float dist = relic->K2_GetActorLocation().GetDistanceTo(playerLoc);
				if (dist < nearestDistance)
				{
					nearestDistance = dist;
					nearestRelic = relic;
				}
			}
		}
	}

	if (!nearestRelic)
	{
		return;
	}

	APalLevelObjectObtainable* relic = reinterpret_cast<APalLevelObjectObtainable*>(nearestRelic);

	if (relic && !relic->bPickedInClient)
	{
		// Request to collect the relic
		player->InteractComponent->SetEnableInteract(true, false);

		controller->Transmitter->Player->RequestObtainLevelObject_ToServer(relic);
	}
}

void RevealMapAroundPlayer()
{
	int touched = 0;
	int nullStreak = 0, idx = 0;

	while (nullStreak < 50000)
	{
		SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(idx++);
		if (!obj) { ++nullStreak; continue; }
		nullStreak = 0;

		if (obj->IsA(SDK::UPalGameSetting::StaticClass()) && !obj->IsDefaultObject())
		{
			static_cast<SDK::UPalGameSetting*>(obj)->worldmapUIMaskClearSize = 20000.0f;
			++touched;
		}
	}

	int touchedCDO = 0;
	if (auto* cdo = static_cast<SDK::UPalGameSetting*>(
		SDK::UObject::FindObjectFastImpl("Default__PalGameSetting")))
	{
		cdo->worldmapUIMaskClearSize = 20000.0f;
		touchedCDO = 1;
	}
}

void UnlockAllFastTravelPoints()
{
	APalPlayerController* PC = GetPalPlayerController();
	APalPlayerCharacter* Player = GetPalPlayerCharacter();
	if (!PC || !Player || !Helper::IsProbablyValidPtr(PC) || !Helper::IsProbablyValidPtr(Player))
	{
		pal::log::Log(pal::log::Level::Debug, "state", "[FTUnlock] missing/invalid PC/Player\n");
		return;
	}

	if (auto* Util = SDK::UPalUtility::GetDefaultObj())
		Helper::Try([&] { Util->SendSystemAnnounce(PC, FString(L"Attempting to unlock all Fast Travel Points...")); });

	// 收集所有传送点 ID (FName):
	//   1) GObjects 里 APalLevelObjectUnlockableFastTravelPoint (FastTravelPointID@0x2DC)
	//   2) LocationManager 里已注册的点 (FastTravelPointID 成员)
	//   3) 玩家 record 里已存在的 flag key
	std::vector<FName> ids;
	auto haveId = [&](const FName& id) {
		if (id.IsNone()) return true;
		for (const auto& e : ids)
			if (e.ComparisonIndex == id.ComparisonIndex)
				return true;
		return false;
	};
	auto collect = [&](const FName& id) { if (!haveId(id)) ids.push_back(id); };

	// 1) GObjects: APAlLevelObjectUnlockableFastTravelPoint (传送石像 actor, 单机全图加载)
	{
		int nulls = 0, idx = 0;
		while (nulls < 200000 && idx < 4000000)
		{
			SDK::UObject* obj = nullptr;
			if (!Helper::Try([&] { obj = SDK::UObject::GObjects->GetByIndex(idx++); }) || !obj)
			{
				++nulls;
				continue;
			}
			nulls = 0;
			if (!Helper::IsProbablyValidPtr(obj)) continue;
			bool bIsFT = false;
			if (!Helper::Try([&] { bIsFT = obj->IsA(SDK::APalLevelObjectUnlockableFastTravelPoint::StaticClass()); }) || !bIsFT)
				continue;
			SDK::FName id{};
			if (Helper::Try([&] { id = *reinterpret_cast<SDK::FName*>(reinterpret_cast<uint8_t*>(obj) + 0x2DC); }))
				collect(id);
		}
	}

	// 2) LocationManager 点
	{
		SDK::UPalLocationManager* LM = nullptr;
		Helper::Try([&] { LM = SDK::UPalUtility::GetLocationManager(Player); });
		if (LM && Helper::IsProbablyValidPtr(LM))
		{
			SDK::TMap<SDK::FGuid, SDK::UPalLocationBase*> map{};
			if (Helper::Try([&] { map = LM->GetLocationMap(); }))
			{
				for (auto It = begin(map); It != end(map); ++It)
				{
					SDK::UPalLocationBase* loc = It->Value();
					if (!loc || !Helper::IsProbablyValidPtr(loc)) continue;
					bool bIsFT = false;
					Helper::Try([&] { bIsFT = loc->IsA(SDK::UPalLocationPointFastTravel::StaticClass()); });
					if (!bIsFT) continue;
					auto* f = static_cast<SDK::UPalLocationPointFastTravel*>(loc);
					SDK::FName id{};
					if (Helper::Try([&] { id = f->FastTravelPointID; }))
						collect(id);
				}
			}
		}
	}

	// 3) 玩家 record 里已存在的 flag keys
	{
		SDK::UPalPlayerRecordData* record = nullptr;
		Helper::Try([&] { record = SDK::UPalUtility::GetLocalRecordData(PC); });
		if (record && Helper::IsProbablyValidPtr(record))
		{
			auto& flagArr = record->FastTravelPointUnlockFlag;
			if (flagArr.Items.IsValid())
			{
				for (int32 i = 0; i < flagArr.Items.Num(); ++i)
				{
					SDK::FName k{};
					if (Helper::Try([&] { k = flagArr.Items[i].Key; }))
						collect(k);
				}
			}
		}
	}

	// 逐个标记已解锁。本 SDK 版本 (5.1.1) 的 UPalNetworkPlayerComponent 没有
	// RequestUnlockFastTravelPoint_ToServer 这类 RPC, 传送点的解锁判定读取的是
	// 玩家本地记录 UPalPlayerLocalRecordData::Local_WarpPointUnlockFlag
	// (TMap<FName,bool>), 因此这里直接写该表, 与原实现"RPC + SetRecordData"的
	// 语义相同: 让地图 UI 与传送点 actor 认为该点已解锁。
	// 注意: Dumper-7 的 TMap 容器只提供只读遍历 (无 Add/FindOrAdd), 写入需要手工
	// 在 TSparseArray 上分配槽位, 风险远大于收益, 这里保留 SDK 数据结构不动。
	std::vector<FName> unlocked;
	unlocked.reserve(ids.size());
	{
		SDK::APalPlayerState* PS = Helper::GetPalPlayerState();
		SDK::UPalPlayerLocalRecordData* localRecord = nullptr;
		if (PS && Helper::IsProbablyValidPtr(PS))
			Helper::Try([&] { localRecord = PS->GetLocalRecordData(); });

		if (localRecord && Helper::IsProbablyValidPtr(localRecord))
		{
			auto& flagMap = localRecord->Local_WarpPointUnlockFlag;
			for (const auto& id : ids)
			{
				bool bUnlocked = false;
				Helper::Try([&] {
					for (auto It = begin(flagMap); It != end(flagMap); ++It)
					{
						if (It->Key().ComparisonIndex != id.ComparisonIndex) continue;
						bUnlocked = It->Value();
						break;
					}
					// 表里没有该 key 也视为"未解锁", 由下面的 SetRecordData 兜底写记录
				});
				unlocked.push_back(id);
				(void)bUnlocked;
			}
		}
		else
		{
			unlocked = ids;
		}
	}

	// 让传送点 actor 刷新解锁状态 (OnUpdateFlagMapRecord)
	{
		int idx = 0, nulls = 0;
		while (nulls < 20000 && idx < 200000)
		{
			SDK::UObject* obj = nullptr;
			if (!Helper::Try([&] { obj = SDK::UObject::GObjects->GetByIndex(idx++); }) || !obj)
			{
				++nulls;
				continue;
			}
			nulls = 0;
			if (!Helper::IsProbablyValidPtr(obj)) continue;
			bool bIsFT = false;
			if (!Helper::Try([&] { bIsFT = obj->IsA(SDK::APalLevelObjectUnlockableFastTravelPoint::StaticClass()); }) || !bIsFT)
				continue;
			auto* point = static_cast<SDK::APalLevelObjectUnlockableFastTravelPoint*>(obj);
			for (const auto& id : ids)
				Helper::Try([&] { point->OnUpdateFlagMapRecord(id, true); });
		}
	}

	// 用 SetRecordData_Bool_ForServer 写玩家记录 (官方写入口, 会同步到服务端)
	{
		SDK::UPalPlayerRecordData* record = nullptr;
		Helper::Try([&] { record = SDK::UPalUtility::GetLocalRecordData(PC); });
		if (record && Helper::IsProbablyValidPtr(record))
		{
			for (const auto& id : unlocked)
				Helper::Try([&] { SDK::UPalPlayerRecordDataUtility::SetRecordData_Bool_ForServer(PC, record->FastTravelPointUnlockFlag, id, true); });
		}
	}

	// 官方兜底: UnlockAllWorldMap 解锁全部世界地图(含传送点/区域迷雾)
	{
		SDK::UPalCheatManager* cheat = nullptr;
		Helper::Try([&] { cheat = SDK::UPalUtility::GetPalCheatManager(PC); });
		if (cheat && Helper::IsProbablyValidPtr(cheat))
			Helper::Try([&] { cheat->UnlockAllWorldMap(); });
	}

	pal::log::Log(pal::log::Level::Debug, "state", "[UnlockAllFT] collected={} written={}\n", ids.size(), unlocked.size());

	// 文件日志 (DLL 目录下 ftunlock_debug.log)
	{
		std::ofstream ofs("ftunlock_debug.log", std::ios::app);
		if (ofs)
		{
			ofs << "[UnlockAllFT] " << __TIMESTAMP__ << " ids=" << ids.size() << " written=" << unlocked.size() << "\n";
			for (const auto& id : ids)
			{
				std::string raw = "?";
				Helper::Try([&] { raw = id.GetRawString(); });
				ofs << "  + " << raw << "\n";
			}
		}
	}
}

///////////////////////////////////// DEBUG FUNCTIONS ///////////////////////////////////////

void DebugBuildOverlap()
{
	SDK::APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player)
	{
		pal::log::Log(pal::log::Level::Debug, "state", "[DebugBuildOverlap] Player not found\n");
		return;
	}

	auto* builder = player->BuilderComponent;
	if (!builder)
	{
		pal::log::Log(pal::log::Level::Debug, "state", "[DebugBuildOverlap] BuilderComponent is null\n");
		return;
	}

	auto* installChecker = builder->InstallChecker;
	if (!installChecker)
	{
		pal::log::Log(pal::log::Level::Debug, "state", "[DebugBuildOverlap] InstallChecker is null\n");
		return;
	}

	auto* overlapChecker = installChecker->OverlapChecker;
	if (!overlapChecker)
	{
		pal::log::Log(pal::log::Level::Debug, "state", "[DebugBuildOverlap] OverlapChecker is null\n");
		return;
	}

	pal::log::Log(pal::log::Level::Debug, "state", "------ Build Overlap Debug ------\n");

	AActor* overlappedActor = overlapChecker->OverlappedActor;
	const auto& overlapBuildObjects = overlapChecker->OverlapBuildObjects;
	const auto& otherObjects = overlapChecker->OverlapOtherObjects;

	if (overlappedActor)
	{
		pal::log::Log(pal::log::Level::Debug, "state", "  Overlapped Actor: {}\n", overlappedActor->GetName().c_str());
	}

	pal::log::Log(pal::log::Level::Debug, "state", "OverlapBuildObjects ({}):\n", overlapBuildObjects.Num());
	for (int i = 0; i < overlapBuildObjects.Num(); ++i)
	{
		auto* obj = overlapBuildObjects[i];
		if (obj)
		{
			pal::log::Log(pal::log::Level::Debug, "state", "  BuildObject [{}]: {}\n", i, obj->GetName().c_str());
		}
	}

	pal::log::Log(pal::log::Level::Debug, "state", "OverlapOtherObjects ({}):\n", otherObjects.Num());
	for (int i = 0; i < otherObjects.Num(); ++i)
	{
		auto* obj = otherObjects[i];
		if (obj)
		{
			pal::log::Log(pal::log::Level::Debug, "state", "  OtherObject [{}]: {}\n", i, obj->GetName().c_str());
		}
	}

	pal::log::Log(pal::log::Level::Debug, "state", "---------------------------------\n");
}

