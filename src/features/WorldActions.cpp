#include <pch.h>
#include "CheatState.hpp"
#include "core/Gfx.hpp"
#include "engine/GameHelper.hpp"

using namespace SDK;
using namespace Helper;

namespace gfx = pal::gfx;   // 覆盖层绘制门面 (本文件在全局命名空间, 需要别名)

// Range (in cm) around the player to apply the actions.
static const float WORLDActionRange = 10000.0f; // ~100 meters

// Returns the player world location, or false if the player is unavailable.
static bool GetPlayerLocation(FVector& OutLocation)
{
	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player)
		return false;
	OutLocation = player->K2_GetActorLocation();
	return true;
}

// Iterates all actors currently loaded in the world.
static void ForEachActor(const std::function<bool(AActor*)>& Callback)
{
	UWorld* world = UWorld::GetWorld();
	if (!world)
		return;

	const auto& levels = world->Levels;
	for (int32 i = 0; i < levels.Num(); ++i)
	{
		ULevel* level = levels[i];
		if (!level)
			continue;

		const auto& actors = level->Actors;
		for (int32 j = 0; j < actors.Num(); ++j)
		{
			AActor* actor = actors[j];
			if (!actor || !Helper::IsProbablyValidPtr(actor))
				continue;

			// 包上 Try：世界里存在正在 GC/销毁的 stale actor，回调里访问其
			// Class/IsA/位置可能崩溃。若不包裹，全局 handler 吞掉异常后整个
			// 遍历被中断，导致"杀帕鲁/砍树/拾取不起作用"。包上后坏 actor 被
			// 跳过，其余动作正常执行。
			bool keepGoing = true;
			if (!Helper::Try([&] { keepGoing = Callback(actor); }))
				continue; // 该 actor 处理崩溃，跳到下一个

			if (!keepGoing)
				return; // stop iteration
		}
	}
}

// Reads the clean class name of an actor, or empty string on failure.
static std::string GetActorClassName(AActor* Actor)
{
	if (!Actor || !Actor->Class || !Helper::IsProbablyValidPtr(Actor->Class))
		return std::string();

	std::string name;
	if (!Helper::Try([&] { name = Actor->Class->GetName(); }))
		return std::string();
	return name;
}

// Builds a large FPalDamageInfo so the target resource is destroyed immediately
// and follows the normal damage/collection path (so drops are spawned).
// 设置 Attacker=本地玩家，让服务器能把掉落归属给玩家（否则只破坏不出掉落）。
// 修复：伤害倍率(CollectionObjectDamageRate)必须 > 0 且攻击类型/武器类型正确，
// 服务器才会走"采集掉落"路径生成木材/矿石等掉落物。
static FPalDamageInfo MakeResourceDamageInfo(AActor* Attacker)
{
	FPalDamageInfo info{};
	info.NativeDamageValue = 1000000;
	info.BasePower = 1000000;
	info.RedirectDamageValue = 0;
	info.Attacker = Attacker;
	info.OverrideNetworkOwner = Attacker;
	// 让服务器把它当成"采集/收集伤害"处理（关键：CollectionObjectDamageRate 非 0
	// 时，破坏树木/岩石会走正常采集掉落路径，产生木材/矿石等掉落物）。
	// 修复：原值 100.0 可能被服务器视为异常值而忽略，改为 1.0（正常采集倍率）。
	info.CollectionObjectDamageRate = 1.0f;
	// 让服务器把它当成"常规攻击"处理（不是爆炸/特殊伤害），以便走正常掉落路径。
	info.bIsExplosionDamage = false;
	info.NoDamage = false;
	info.IgnoreCanProcessDamage = false;
	info.bApplyNativeDamageValue = true;
	// 修复：必须设置正确的攻击类型和武器类型，服务器才会按正规采集流程处理掉落。
	// AttackType = EPalAttackType::weapon(值为1)，WeaponType = MeleeWeapon，
	// AttackElementType = Normal，与玩家徒手/工具采集一致。
	info.AttackType = static_cast<EPalAttackType>(1);
	info.WeaponType = EPalWeaponType::MeleeWeapon;
	info.AttackElementType = EPalElementType::Normal;
	info.AttackerLevel = 1;
	info.bCannotKill = false;
	return info;
}

// Applies lethal damage to a map object through the network transmitter so that
// the object is destroyed the same way as normal gathering -> drops are produced.
// 同时本地兜底置零模型 HP，确保视觉上立即消失（即使 RPC 因网络延迟未落地）。
void DamageSingleMapObject(AActor* Actor)
{
	if (!Actor || !Helper::IsProbablyValidPtr(Actor))
		return;
	APalPlayerController* PC = GetPalPlayerController();
	if (!PC || !PC->Transmitter || !PC->Transmitter->MapObject)
		return;

	APalMapObject* mapObject = static_cast<APalMapObject*>(Actor);
	APalPlayerCharacter* player = GetPalPlayerCharacter();
	FPalDamageInfo info = MakeResourceDamageInfo(player);
	Helper::Try([&]
	{
		FGuid modelInstanceId = mapObject ? mapObject->ModelInstanceId : FGuid();
		PC->Transmitter->MapObject->RequestDamageMapObject_ToServer(modelInstanceId, info);
	});

	// 本地兜底：直接置零模型 HP，确保画面立即反馈（即使 RPC 延迟）
	if (mapObject)
	{
		UPalMapObjectModel* model = nullptr;
		if (Helper::Try([&] { model = mapObject->GetModel(); }) && model && Helper::IsProbablyValidPtr(model))
			Helper::Try([&] { model->Hp.CurrentValue = 0; });
	}
}

// (保留原有 static 函数供内部调用)
static void DamageMapObject(AActor* Actor, APalPlayerController* PC)
{
	if (!PC || !PC->Transmitter || !PC->Transmitter->MapObject)
		return;

	APalMapObject* mapObject = static_cast<APalMapObject*>(Actor);
	APalPlayerCharacter* player = GetPalPlayerCharacter();
	FPalDamageInfo info = MakeResourceDamageInfo(player);
	Helper::Try([&]
	{
		FGuid modelInstanceId = mapObject ? mapObject->ModelInstanceId : FGuid();
		PC->Transmitter->MapObject->RequestDamageMapObject_ToServer(modelInstanceId, info);
	});
}

// Finds all UPalMapObjectFoliage instances in the world.
// 优先通过 UPalUtility::GetMapObjectManager(player) 拿到世界子系统持有的
// Foliage 对象（这是服务器/客户端双方登记树的可靠入口），GObjects 枚举作兜底。
static void GetAllMapObjectFoliage(std::vector<UPalMapObjectFoliage*>& out)
{
	out.clear();

	// 方式1（首选）：从 MapObjectManager->Foliage 拿。这是 PalWorldSubsystem 持有
	// 的 foliage 管理器，包含当前加载的所有树。
	if (APalPlayerCharacter* p = GetPalPlayerCharacter())
	{
		UPalMapObjectManager* mgr = nullptr;
		if (Helper::Try([&] { mgr = SDK::UPalUtility::GetMapObjectManager(p); }) &&
			mgr && Helper::IsProbablyValidPtr(mgr))
		{
			UPalMapObjectFoliage* f = nullptr;
			if (Helper::Try([&] { f = mgr->Foliage; }) && f && Helper::IsProbablyValidPtr(f))
			{
				out.push_back(f);
				return;
			}
		}
	}

	if (!UObject::GObjects)
		return;

	// 方式2（兜底）：遍历 GObjects 找所有 UPalMapObjectFoliage。
	const int32 n = UObject::GObjects->Num();
	for (int32 i = 0; i < n; ++i)
	{
		UObject* obj = UObject::GObjects->GetByIndex(i);
		if (!obj || !obj->Class || !Helper::IsProbablyValidPtr(obj))
			continue;
		bool isFoliage = false;
		if (Helper::Try([&] { isFoliage = obj->IsA(UPalMapObjectFoliage::StaticClass()); }) && isFoliage)
		{
			UPalMapObjectFoliage* f = static_cast<UPalMapObjectFoliage*>(obj);
			if (f)
				out.push_back(f);
		}
	}
}

// 直接遍历 GObjects 找到所有 UPalFoliageInstance，设置 HP=0 强制破坏。
// 这是比网络 RPC 更直接的本地破坏方式，在单人中（client==server）立即生效。
// 单个 foliage 实例：归零 HP 并置 Dead，距离内才处理。
// 注意：此本地兜底方式可能绕过掉落生成路径，因此仅在服务器 RPC 不可用时使用。
static void KillFoliageInstance(UPalFoliageInstance* inst, const FVector& playerLoc)
{
	if (!inst || !Helper::IsProbablyValidPtr(inst))
		return;
	Helper::Try([&]
	{
		int32 hp = 0;
		if (!Helper::Try([&] { hp = inst->Hp; }))
			return;
		if (hp <= 0)
			return;
		FVector instLoc;
		if (!Helper::Try([&] { instLoc = inst->WorldTransform.Location; }))
			return;
		if (instLoc.GetDistanceTo(playerLoc) > WORLDActionRange)
			return;

		// 直接归零 HP，并把实例状态置为 Dead。
		// 在单人模式下 client==server，游戏会据此把该 foliage 判定为已破坏，
		// 触发销毁/重生流程（树消失）。InstanceState 是关键，光改 HP 不够。
		// 但此方式可能导致不掉落木材（因为没有走正规伤害流程），所以只在 RPC 失败时兜底。
		inst->Hp = 0;
		inst->InstanceState = EPalFoliageInstanceState::Dead;
	});
}

static void ForceDestroyNearbyFoliageInstances(const FVector& playerLoc)
{
	// 先通过 MapObjectManager->Foliage 的 GridModelMap 拿到所有实例（可靠来源）。
	{
		std::vector<UPalMapObjectFoliage*> foliageList;
		GetAllMapObjectFoliage(foliageList);
		for (UPalMapObjectFoliage* foliage : foliageList)
		{
			if (!foliage || !Helper::IsProbablyValidPtr(foliage))
				continue;
			Helper::Try([&]
			{
				auto& gridModels = foliage->GridModelMap;
				for (auto gridIt = begin(gridModels); gridIt != end(gridModels); ++gridIt)
				{
					UPalFoliageGridModel* grid = gridIt->Value();
					if (!grid || !Helper::IsProbablyValidPtr(grid))
						continue;

					// Source A: grid->FoliageModelMapInServer -> UPalMapObjectFoliageModel->InstanceMap
					auto& foliageModels = grid->FoliageModelMapInServer;
					for (auto modelIt = begin(foliageModels); modelIt != end(foliageModels); ++modelIt)
					{
						UPalMapObjectFoliageModel* foliageModel = modelIt->Value();
						if (!foliageModel || !Helper::IsProbablyValidPtr(foliageModel))
							continue;
						auto& instances = foliageModel->InstanceMap;
						for (auto instIt = begin(instances); instIt != end(instances); ++instIt)
							KillFoliageInstance(instIt->Value(), playerLoc);
					}

					// Source B: grid->InstanceMapByComponentId -> FPalFoliageGridInstanceMap->InstanceMap
					auto& byComp = grid->InstanceMapByComponentId;
					for (auto compIt = begin(byComp); compIt != end(byComp); ++compIt)
					{
						const FPalFoliageGridInstanceMap& gm = compIt->Value();
						auto& instances = gm.InstanceMap;
						for (auto instIt = begin(instances); instIt != end(instances); ++instIt)
							KillFoliageInstance(instIt->Value(), playerLoc);
					}
				}
			});
		}
	}

	if (!UObject::GObjects)
		return;

	// 兜底：遍历 GObjects 找所有 UPalFoliageInstance 强制破坏。
	const int32 n = UObject::GObjects->Num();
	for (int32 i = 0; i < n; ++i)
	{
		UObject* obj = UObject::GObjects->GetByIndex(i);
		if (!obj || !obj->Class || !Helper::IsProbablyValidPtr(obj))
			continue;

		bool isInst = false;
		if (!Helper::Try([&] { isInst = obj->IsA(UPalFoliageInstance::StaticClass()); }) || !isInst)
			continue;

		KillFoliageInstance(static_cast<UPalFoliageInstance*>(obj), playerLoc);
	}
}

// Chops destructible foliage (real trees) near the player.
// 策略：
//  1) 遍历 UPalMapObjectFoliage->GridModelMap，发送网络 RPC（RequestDamageFoliage_ToServer）
//     让服务器按巨大伤害破坏实例并登记掉落（这是产生木材掉落的正确路径）。
//  2) 无条件调用 ForceDestroyNearbyFoliageInstances 本地兜底：
//     RPC 负责服务器掉落，本地兜底保证树一定消失（即使 RPC 因网络问题
//     未被服务器处理，用户也能看到树被砍掉的效果）。
static void ChopNearbyFoliage(const FVector& playerLoc, APalPlayerController* PC)
{
	// 网络 RPC 部分：让服务器破坏实例并登记掉落（产生木材/矿石掉落物）。
	if (PC && PC->Transmitter && PC->Transmitter->MapObject)
	{
		// 遍历所有 UPalMapObjectFoliage 实例（可能有多个，全部处理）
		std::vector<UPalMapObjectFoliage*> foliageList;
		GetAllMapObjectFoliage(foliageList);

		for (UPalMapObjectFoliage* foliage : foliageList)
		{
			if (!foliage || !Helper::IsProbablyValidPtr(foliage))
				continue;

			// GridModelMap : TMap<FPalCellCoord, UPalFoliageGridModel*>
			auto& gridModels = foliage->GridModelMap;
			for (auto gridIt = begin(gridModels); gridIt != end(gridModels); ++gridIt)
			{
				UPalFoliageGridModel* grid = gridIt->Value();
				if (!grid || !Helper::IsProbablyValidPtr(grid))
					continue;

				struct Col { FName modelId; TArray<FPalFoliageInstanceId> ids; };
				std::vector<Col> groups;

				// Source A: grid->FoliageModelMapInServer : TMap<FName, UPalMapObjectFoliageModel*>
				auto& foliageModels = grid->FoliageModelMapInServer;
				for (auto modelIt = begin(foliageModels); modelIt != end(foliageModels); ++modelIt)
				{
					FName modelId = modelIt->Key();
					UPalMapObjectFoliageModel* foliageModel = modelIt->Value();
					if (!foliageModel || !Helper::IsProbablyValidPtr(foliageModel))
						continue;

					auto& instances = foliageModel->InstanceMap;
					Col g; g.modelId = modelId;
					for (auto instIt = begin(instances); instIt != end(instances); ++instIt)
					{
						UPalFoliageInstance* inst = instIt->Value();
						if (!inst || !Helper::IsProbablyValidPtr(inst))
							continue;
						int32 hp = 0;
						if (!Helper::Try([&] { hp = inst->Hp; }))
							continue;
						if (hp <= 0)
							continue;
						FVector instLoc;
						if (!Helper::Try([&] { instLoc = inst->WorldTransform.Location; }))
							continue;
						if (instLoc.GetDistanceTo(playerLoc) > WORLDActionRange)
							continue;
						g.ids.Add(instIt->Key());
					}
					if (g.ids.Num() > 0)
						groups.push_back(g);
				}

				// Source B: grid->InstanceMapByComponentId : TMap<FName, FPalFoliageGridInstanceMap>
				auto& byComp = grid->InstanceMapByComponentId;
				for (auto compIt = begin(byComp); compIt != end(byComp); ++compIt)
				{
					const FPalFoliageGridInstanceMap& gm = compIt->Value();
					auto& instances = gm.InstanceMap;
					Col g;
					for (auto instIt = begin(instances); instIt != end(instances); ++instIt)
					{
						UPalFoliageInstance* inst = instIt->Value();
						if (!inst || !Helper::IsProbablyValidPtr(inst))
							continue;
						int32 hp = 0;
						if (!Helper::Try([&] { hp = inst->Hp; }))
							continue;
						if (hp <= 0)
							continue;
						FVector instLoc;
						if (!Helper::Try([&] { instLoc = inst->WorldTransform.Location; }))
							continue;
						if (instLoc.GetDistanceTo(playerLoc) > WORLDActionRange)
							continue;
						FName fType;
						if (Helper::Try([&] { fType = inst->FoliageTypeId; }))
							g.modelId = fType;
						g.ids.Add(instIt->Key());
					}
					if (g.ids.Num() > 0)
						groups.push_back(g);
				}

				// 发送网络 RPC 让服务器破坏实例并登记掉落
				for (auto& g : groups)
				{
					if (g.ids.Num() <= 0)
						continue;
					APalPlayerCharacter* player = GetPalPlayerCharacter();
					FPalDamageInfo info = MakeResourceDamageInfo(player);
					info.FoliageModelId = g.modelId;
					info.FoliageInstanceIds = g.ids;
					Helper::Try([&]
					{
						PC->Transmitter->MapObject->RequestDamageFoliage_ToServer(
							gridIt->Key(), g.modelId, g.ids, info);
					});
				}
			}
		}
	}

	// 本地兜底：无条件遍历 GObjects 强制破坏所有附近的 foliage 实例。
	// RPC 负责服务器掉落生成，本地兜底保证树一定消失（即使 RPC 因网络问题
	// 未被服务器处理，用户也能看到树被砍掉的效果）。
	ForceDestroyNearbyFoliageInstances(playerLoc);
}

// Destroys all destructible tree/foliage objects near the player.
void ChopNearbyTrees()
{
	FVector playerLoc;
	if (!GetPlayerLocation(playerLoc))
		return;

	APalPlayerController* pc = GetPalPlayerController();

	// Foliage-based trees (the common case in Palworld).
	ChopNearbyFoliage(playerLoc, pc);

	// Map-object based trees (rare, class name based).
	ForEachActor([&](AActor* actor) -> bool
	{
		bool isMapObject = false;
		if (!Helper::Try([&] { isMapObject = actor->IsA(APalMapObject::StaticClass()); }) || !isMapObject)
			return true;

		// Do not destroy dropped item actors.
		bool isDropItem = false;
		if (Helper::Try([&] { isDropItem = actor->IsA(APalMapObjectDropItem::StaticClass()); }) && isDropItem)
			return true;

		std::string cls = GetActorClassName(actor);
		// 放宽匹配条件，匹配所有可能包含 "Tree"、"Foliage"、"Wood" 等关键词的 MapObject
		bool matches = 
			cls.find("FoliageModelChunk") != std::string::npos ||
			cls.find("Foliage") != std::string::npos ||
			cls.find("Tree") != std::string::npos ||
			cls.find("Wood") != std::string::npos ||
			cls.find("Damagable") != std::string::npos ||
			cls.find("Resource") != std::string::npos;
		if (!matches)
			return true;

		FVector actorLoc;
		if (!Helper::Try([&] { actorLoc = actor->K2_GetActorLocation(); }))
			return true;
		if (actorLoc.GetDistanceTo(playerLoc) > WORLDActionRange)
			return true;

		// 直接在本地也尝试设置模型HP为0，加上网络请求双管齐下
		APalMapObject* mapObject = static_cast<APalMapObject*>(actor);
		if (mapObject) {
			// 尝试通过模型的 HP 直接归零
			UPalMapObjectModel* model = nullptr;
			if (Helper::Try([&] { model = mapObject->GetModel(); }) && model && Helper::IsProbablyValidPtr(model))
			{
				Helper::Try([&] { model->Hp.CurrentValue = 0; });
			}
		}

		// Use normal damage path so tree drops are produced.
		DamageMapObject(actor, pc);
		return true;
	});
}

// Destroys all destructible rock / ore objects near the player.
void MineNearbyRocks()
{
	FVector playerLoc;
	if (!GetPlayerLocation(playerLoc))
		return;

	APalPlayerController* pc = GetPalPlayerController();

	ForEachActor([&](AActor* actor) -> bool
	{
		bool isMapObject = false;
		if (!Helper::Try([&] { isMapObject = actor->IsA(APalMapObject::StaticClass()); }) || !isMapObject)
			return true;

		bool isDropItem = false;
		if (Helper::Try([&] { isDropItem = actor->IsA(APalMapObjectDropItem::StaticClass()); }) && isDropItem)
			return true;

		std::string cls = GetActorClassName(actor);
		if (cls.find("DamagableRock") == std::string::npos &&
			cls.find("WeakPointOre") == std::string::npos &&
			cls.find("Rock") == std::string::npos)
			return true;

		FVector actorLoc;
		if (!Helper::Try([&] { actorLoc = actor->K2_GetActorLocation(); }))
			return true;
		if (actorLoc.GetDistanceTo(playerLoc) > WORLDActionRange)
			return true;

		// Use normal damage path so ore drops are produced (fixes "no drops" bug).
		DamageMapObject(actor, pc);
		return true;
	});
}

// Picks up all drop item actors near the player.
void PickupNearbyItems()
{
	FVector playerLoc;
	if (!GetPlayerLocation(playerLoc))
		return;

	APalPlayerController* pc = GetPalPlayerController();

	ForEachActor([&](AActor* actor) -> bool
	{
		bool isMapObject = false;
		if (!Helper::Try([&] { isMapObject = actor->IsA(APalMapObject::StaticClass()); }) || !isMapObject)
			return true;

		FVector actorLoc;
		if (!Helper::Try([&] { actorLoc = actor->K2_GetActorLocation(); }))
			return true;
		if (actorLoc.GetDistanceTo(playerLoc) > WORLDActionRange)
			return true;

		APalMapObject* mapObject = static_cast<APalMapObject*>(actor);
		UPalMapObjectModel* model = mapObject ? mapObject->GetModel() : nullptr;
		if (!model || !Helper::IsProbablyValidPtr(model))
			return true;

		UPalMapObjectConcreteModelBase* concrete = nullptr;
		if (!Helper::Try([&] { concrete = model->GetConcreteModel(true); }))
			return true;
		if (!concrete || !Helper::IsProbablyValidPtr(concrete))
			return true;

		// Case 1: drop-on-damage model (ore / wood / Pal drops spawned when a
		// resource is damaged). These must first be marked as detected by the
		// player before they become pickable.
		bool isDropOnDamag = false;
		if (Helper::Try([&] { isDropOnDamag = concrete->IsA(UPalMapObjectItemDropOnDamagModel::StaticClass()); }) &&
			isDropOnDamag)
		{
			UPalMapObjectItemDropOnDamagModel* dropOnDamag =
				static_cast<UPalMapObjectItemDropOnDamagModel*>(concrete);
			Helper::Try([&] { dropOnDamag->RequestMarkDetectedByPlayer(true); });
			return true;
		}

		// Case 2: generic pickable-item model (dropped / pickup items).
		bool isPickable = false;
		if (!Helper::Try([&] { isPickable = concrete->IsA(UPalMapObjectPickableItemModelBase::StaticClass()); }) ||
			!isPickable)
			return true;

		UPalMapObjectPickableItemModelBase* pickable =
			static_cast<UPalMapObjectPickableItemModelBase*>(concrete);

		Helper::Try([&] { pickable->RequestPickup(false); });
		return true;
	});
}

// Kills all wild (untamed) Pals near the player.
void KillNearbyWildPals()
{
	FVector playerLoc;
	if (!GetPlayerLocation(playerLoc))
		return;

	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player)
		return;

	// 先用 CharacterImportanceManager 获取所有帕鲁
	SDK::TArray<SDK::APalCharacter*> allPals;
	if (!Helper::GetTAllPals(&allPals))
	{
		// Fallback: world iteration
		Helper::GetAllPalsFromWorld(&allPals);
	}

	if (allPals.Num() == 0)
		return;

	for (int32 i = 0; i < allPals.Num(); ++i)
	{
		if (!allPals.IsValidIndex(i))
			continue;
		APalCharacter* pal = allPals[i];
		if (!pal || !Helper::IsProbablyValidPtr(pal))
			continue;

		if (pal == player)
			continue;

		Helper::Try([&]
		{
			UPalCharacterParameterComponent* params = nullptr;
			if (!Helper::Try([&] { params = pal->CharacterParameterComponent; }) ||
				!Helper::IsProbablyValidPtr(params))
				return;

			// Skip pals that are confidently dead
			bool isLive = false;
			bool liveOk = Helper::SafeCallRet(isLive, [&] { return params->IsLive(); });
			if (liveOk && !isLive)
				return;

			// Skip the player's own tamed / otomo pals.
			bool isOtomo = false;
			if (Helper::SafeCallRet(isOtomo, [&] { return params->IsOtomo(); }) && isOtomo)
				return;

			// Skip pals that are actively working at a base camp.
			bool assigned = false;
			if (Helper::SafeCallRet(assigned, [&] { return params->IsAssignedToAnyWork(); }) && assigned)
				return;

			FVector palLoc;
			if (!Helper::Try([&] { palLoc = pal->K2_GetActorLocation(); }))
				return;
			if (palLoc.GetDistanceTo(playerLoc) > WORLDActionRange)
				return;

			// 击杀掉落的正确做法：由服务器走一次"伤害→HP归零→死亡→掉落战利品"
			// 的完整流程。关键点：
			//   1) 不能在调用伤害 RPC 前手动把 HP 置 0（SetHP/AddHP_ToServer 扣血
			//      会让目标在 RPC 到达前就已死亡，服务器检测到目标已死会跳过
			//      死亡掉落生成，导致"秒杀可用但不出掉落物"）。
			//   2) 必须让 FPalDamageInfo 携带有效的击杀者(Attacker=玩家)与正确的
			//      攻击类型/武器类型/元素/等级，服务器才能把掉落归属给玩家并正常结算。
			//   3) 最后补一发 ProcessDeath 兜底，确保死亡状态完全落地。
			APalPlayerController* pc = GetPalPlayerController();
			if (pc && Helper::IsProbablyValidPtr(pc))
			{
				Helper::Try([&]
				{
					// 走正规"伤害"流程：服务器处理伤害时发现 HP 归零会自然触发
					// 死亡，并走 OnDeadDelegate -> APalCharacter::OnDeadCharacter
					// 生成掉落战利品。Defender 是受击方 = 目标帕鲁。
					FPalDamageInfo killInfo{};
					killInfo.NativeDamageValue = 999999999;
					killInfo.BasePower = 999999999;
					killInfo.RedirectDamageValue = 999999999;
					killInfo.Attacker = player;
					killInfo.OverrideNetworkOwner = player;
					// 归属给玩家的普通攻击（MeleeWeapon / 玩家攻击），确保掉落进玩家背包。
					// AttackType = EPalAttackType::weapon(值为1)，用数值避免非ASCII标识符问题。
					killInfo.AttackType = static_cast<EPalAttackType>(1);
					killInfo.WeaponType = EPalWeaponType::MeleeWeapon;
					killInfo.AttackElementType = EPalElementType::Normal;
					killInfo.AttackerLevel = 1;
					killInfo.NoDamage = false;
					killInfo.IgnoreCanProcessDamage = false;
					killInfo.bApplyNativeDamageValue = true;
					killInfo.bCannotKill = false;
					killInfo.bIsExplosionDamage = false;
					pc->DamageReactionComponent_ProcessDamage_ToServer_ToNPC(killInfo, pal);
				});

				// 兜底：直接通知服务器处理该 NPC 的死亡（确保死亡完全生效）。
				Helper::Try([&] { pc->DamageReactionComponent_ProcessDeath_ToServer_ToNPC(pal); });
			}
		});
	}
}

// ------------------------------------------------------------------
// 一键秒杀（全生物版）：
//  - 枚举半径内所有"生物"（帕鲁、NPC、敌对玩家等，只要角色带参数组件）
//  - 精准筛选：仅保留本队已收服的帕鲁（isOtomo），其余全部击杀
//  - 击杀方式沿用正规伤害 RPC，确保掉落正常产生
// ------------------------------------------------------------------
void KillAllCreaturesNearby(float Range)
{
	FVector playerLoc;
	if (!GetPlayerLocation(playerLoc))
		return;

	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player)
		return;

	SDK::TArray<SDK::APalCharacter*> allChars;
	if (!Helper::GetTAllPals(&allChars))
		Helper::GetAllPalsFromWorld(&allChars);

	if (allChars.Num() == 0)
		return;

	for (int32 i = 0; i < allChars.Num(); ++i)
	{
		if (!allChars.IsValidIndex(i))
			continue;
		APalCharacter* pal = allChars[i];
		if (!pal || !Helper::IsProbablyValidPtr(pal))
			continue;

		if (pal == player)
			continue;

		Helper::Try([&]
		{
			UPalCharacterParameterComponent* params = nullptr;
			if (!Helper::Try([&] { params = pal->CharacterParameterComponent; }) ||
				!Helper::IsProbablyValidPtr(params))
				return;

			// 跳过已死亡的实体
			bool isLive = false;
			bool liveOk = Helper::SafeCallRet(isLive, [&] { return params->IsLive(); });
			if (liveOk && !isLive)
				return;

			// 精准筛选：本队已收服的帕鲁（Otomo/被驯服）保留，不击杀
			bool isOtomo = false;
			if (Helper::SafeCallRet(isOtomo, [&] { return params->IsOtomo(); }) && isOtomo)
				return;

			// 距离过滤（使用可配置半径，单位厘米）
			FVector palLoc;
			if (!Helper::Try([&] { palLoc = pal->K2_GetActorLocation(); }))
				return;
			if (palLoc.GetDistanceTo(playerLoc) > Range)
				return;

			// 正规伤害击杀（与 KillNearbyWildPals 相同，保留掉落归属）
			APalPlayerController* pc = GetPalPlayerController();
			if (pc && Helper::IsProbablyValidPtr(pc))
			{
				Helper::Try([&]
				{
					FPalDamageInfo killInfo{};
					killInfo.NativeDamageValue = 999999999;
					killInfo.BasePower = 999999999;
					killInfo.RedirectDamageValue = 999999999;
					killInfo.Attacker = player;
					killInfo.OverrideNetworkOwner = player;
					killInfo.AttackType = static_cast<EPalAttackType>(1);
					killInfo.WeaponType = EPalWeaponType::MeleeWeapon;
					killInfo.AttackElementType = EPalElementType::Normal;
					killInfo.AttackerLevel = 1;
					killInfo.NoDamage = false;
					killInfo.IgnoreCanProcessDamage = false;
					killInfo.bApplyNativeDamageValue = true;
					killInfo.bCannotKill = false;
					killInfo.bIsExplosionDamage = false;
					pc->DamageReactionComponent_ProcessDamage_ToServer_ToNPC(killInfo, pal);
				});

				// 兜底死亡
				Helper::Try([&] { pc->DamageReactionComponent_ProcessDeath_ToServer_ToNPC(pal); });
			}
		});
	}
}

// ------------------------------------------------------------------
// 秒杀准星所指对象：
//  - 生物目标（APalCharacter）：复用一键秒杀的正规伤害 RPC（保留掉落归属），
//    本队已收服帕鲁(IsOtomo)与玩家自身不受影响
//  - 地图物件目标（APalMapObject，如矿石/树木）：走 RequestDamageMapObject
//    正规采集路径，产生掉落
//  - 查找方式：屏幕中心（准星）投影匹配最近的目标（同上帝之手的做法）
// 原有的一键秒杀功能保持不变。
// ------------------------------------------------------------------
void KillTargetUnderCrosshair()
{
	APalPlayerCharacter* player = GetPalPlayerCharacter();
	if (!player)
		return;

	APlayerController* controller = nullptr;
	if (!Helper::Try([&] { controller = reinterpret_cast<APlayerController*>(player->Controller); }) ||
		!controller || !Helper::IsProbablyValidPtr(controller))
		return;

	const gfx::Vec2 center(gfx::ScreenWidth() * 0.5f, gfx::ScreenHeight() * 0.5f);
	const float thresholdPx = 120.0f;   // 准星判定阈值（像素），同上帝之手

	// ---- 目标 1: 生物（帕鲁/NPC/敌对玩家）----
	auto killCharacter = [&](APalCharacter* pal) -> bool
	{
		if (!pal || !Helper::IsProbablyValidPtr(pal))
			return false;
		if (pal == player)
			return false;

		UPalCharacterParameterComponent* params = nullptr;
		if (!Helper::Try([&] { params = pal->CharacterParameterComponent; }) ||
			!Helper::IsProbablyValidPtr(params))
			return false;

		// 本队已收服帕鲁不杀
		bool isOtomo = false;
		if (Helper::SafeCallRet(isOtomo, [&] { return params->IsOtomo(); }) && isOtomo)
			return false;
		// 已死亡的不处理
		bool isLive = false;
		if (Helper::SafeCallRet(isLive, [&] { return params->IsLive(); }) && !isLive)
			return false;

		APalPlayerController* pc = GetPalPlayerController();
		if (!pc || !Helper::IsProbablyValidPtr(pc))
			return false;

		// 正规伤害击杀（保留掉落归属）
		Helper::Try([&]
		{
			FPalDamageInfo killInfo{};
			killInfo.NativeDamageValue = 999999999;
			killInfo.BasePower = 999999999;
			killInfo.RedirectDamageValue = 999999999;
			killInfo.Attacker = player;
			killInfo.OverrideNetworkOwner = player;
			killInfo.AttackType = static_cast<EPalAttackType>(1);
			killInfo.WeaponType = EPalWeaponType::MeleeWeapon;
			killInfo.AttackElementType = EPalElementType::Normal;
			killInfo.AttackerLevel = 1;
			killInfo.NoDamage = false;
			killInfo.IgnoreCanProcessDamage = false;
			killInfo.bApplyNativeDamageValue = true;
			killInfo.bCannotKill = false;
			killInfo.bIsExplosionDamage = false;
			pc->DamageReactionComponent_ProcessDamage_ToServer_ToNPC(killInfo, pal);
		});
		// 兜底死亡
		Helper::Try([&] { pc->DamageReactionComponent_ProcessDeath_ToServer_ToNPC(pal); });
		return true;
	};

	// ---- 目标 2: 地图物件（矿石/树木等 APalMapObject）----
	auto destroyMapObject = [&](AActor* actor) -> bool
	{
		if (!actor || !Helper::IsProbablyValidPtr(actor))
			return false;
		if (!actor->Class)
			return false;
		bool isMapObject = false;
		if (!Helper::Try([&] { isMapObject = actor->IsA(APalMapObject::StaticClass()); }) || !isMapObject)
			return false;

		APalMapObject* mapObject = static_cast<APalMapObject*>(actor);

		APalPlayerController* pc = GetPalPlayerController();
		if (!pc || !Helper::IsProbablyValidPtr(pc) || !pc->Transmitter || !pc->Transmitter->MapObject)
			return false;

		APalPlayerCharacter* attacker = GetPalPlayerCharacter();
		FPalDamageInfo info{};
		info.NativeDamageValue = 1000000;
		info.BasePower = 1000000;
		info.RedirectDamageValue = 0;
		info.Attacker = attacker;
		info.OverrideNetworkOwner = attacker;
		info.CollectionObjectDamageRate = 1.0f;
		info.bIsExplosionDamage = false;
		info.NoDamage = false;
		info.IgnoreCanProcessDamage = false;
		info.bApplyNativeDamageValue = true;
		info.AttackType = static_cast<EPalAttackType>(1);
		info.WeaponType = EPalWeaponType::MeleeWeapon;
		info.AttackElementType = EPalElementType::Normal;
		info.AttackerLevel = 1;
		info.bCannotKill = false;

		Helper::Try([&]
		{
			FGuid modelInstanceId = mapObject->ModelInstanceId;
			pc->Transmitter->MapObject->RequestDamageMapObject_ToServer(modelInstanceId, info);
		});
		return true;
	};

	// ---- 屏幕中心查找：先生物后地图物件，取准星最近的 ----
	float bestCharDist = thresholdPx;
	APalCharacter* bestChar = nullptr;
	{
		SDK::TArray<SDK::APalCharacter*> allChars;
		if (Helper::GetTAllPals(&allChars))
		{
			for (int32 i = 0; i < allChars.Num(); ++i)
			{
				if (!allChars.IsValidIndex(i)) continue;
				APalCharacter* pal = allChars[i];
				if (!pal || !Helper::IsProbablyValidPtr(pal) || pal == player) continue;

				FVector loc;
				if (!Helper::Try([&] { loc = pal->K2_GetActorLocation(); })) continue;
				FVector2D screenPos;
				bool onScreen = false;
				if (!Helper::Try([&] { onScreen = controller->ProjectWorldLocationToScreen(loc, &screenPos, false); }))
					continue;
				if (!onScreen || screenPos.X < 0 || screenPos.Y < 0) continue;

				const float dx = screenPos.X - center.x;
				const float dy = screenPos.Y - center.y;
				const float dist = sqrtf(dx * dx + dy * dy);
				if (dist < bestCharDist)
				{
					bestCharDist = dist;
					bestChar = pal;
				}
			}
		}
	}

	float bestObjDist = thresholdPx;
	AActor* bestObj = nullptr;
	{
		UWorld* world = UWorld::GetWorld();
		if (world)
		{
			const auto& levels = world->Levels;
			for (int32 li = 0; li < levels.Num() && bestObjDist > 0.0f; ++li)
			{
				ULevel* level = levels[li];
				if (!level || !Helper::IsProbablyValidPtr(level)) continue;
				const auto& actors = level->Actors;
				for (int32 ai = 0; ai < actors.Num(); ++ai)
				{
					AActor* actor = actors[ai];
					if (!actor || !Helper::IsProbablyValidPtr(actor) || !actor->Class) continue;

					bool isMapObject = false;
					if (!Helper::Try([&] { isMapObject = actor->IsA(APalMapObject::StaticClass()); }) || !isMapObject)
						continue;

					FVector loc;
					if (!Helper::Try([&] { loc = actor->K2_GetActorLocation(); })) continue;
					FVector2D screenPos;
					bool onScreen = false;
					if (!Helper::Try([&] { onScreen = controller->ProjectWorldLocationToScreen(loc, &screenPos, false); }))
						continue;
					if (!onScreen || screenPos.X < 0 || screenPos.Y < 0) continue;

					const float dx = screenPos.X - center.x;
					const float dy = screenPos.Y - center.y;
					const float dist = sqrtf(dx * dx + dy * dy);
					if (dist < bestObjDist)
					{
						bestObjDist = dist;
						bestObj = actor;
					}
				}
			}
		}
	}

	// 谁离准星更近就打谁；生物优先（距离相同时）
	if (bestChar && bestCharDist <= bestObjDist)
		killCharacter(bestChar);
	else if (bestObj)
		destroyMapObject(bestObj);
	else if (bestChar)
		killCharacter(bestChar);
}
