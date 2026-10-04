// features/PlayerObjects.cpp — 人物 / 周围对象功能层
//
// 逐段移植原 src/ui/tabs/TabPlayerObjects.cpp 的逻辑部分, 去掉全部 ImGui 调用:
//   对象分类判定、标签生成、世界 Actor 遍历、单对象操作与玩家经验读写。
// 偏移/条件/调用顺序与原版一致; 新增的只有边界检查 (指针校验与数组下标钳制)。

#include <pch.h>
#include "PlayerObjects.hpp"

#include "CheatState.hpp"
#include "PalEditor.hpp"
#include "core/Log.hpp"
#include "engine/GameHelper.hpp"
#include "engine/NameMapper.hpp"

#include <algorithm>

using namespace SDK;
using namespace Helper;

namespace pal::features::objects {

namespace {

// 工具: 取 MapObject 的 concrete model (失败返回 nullptr)
UPalMapObjectConcreteModelBase* GetObjectConcreteModel(AActor* actor) {
    if (!actor || !IsProbablyValidPtr(actor)) return nullptr;
    APalMapObject* mo = static_cast<APalMapObject*>(actor);
    if (!mo || !IsProbablyValidPtr(mo)) return nullptr;

    UPalMapObjectModel* model = nullptr;
    if (!Try([&] { model = mo->GetModel(); }) || !model || !IsProbablyValidPtr(model))
        return nullptr;

    UPalMapObjectConcreteModelBase* concrete = nullptr;
    if (!Try([&] { concrete = model->GetConcreteModel(true); }) || !concrete || !IsProbablyValidPtr(concrete))
        return nullptr;
    return concrete;
}

// 获取简单分类名 (物品/矿石/宝箱/帕鲁蛋)
std::string GetObjectCategoryName(const std::string& className) {
    if (className.find("DropItem") != std::string::npos ||
        className.find("Item") != std::string::npos)
        return "物品";
    if (className.find("Rock") != std::string::npos ||
        className.find("Ore") != std::string::npos)
        return "矿石";
    if (className.find("Chest") != std::string::npos ||
        className.find("Treasure") != std::string::npos ||
        className.find("TreasureBox") != std::string::npos)
        return "宝箱";
    if (className.find("PalEgg") != std::string::npos ||
        className.find("Egg") != std::string::npos)
        return "帕鲁蛋";

    std::string shortName = className;
    const auto pos = shortName.rfind("_C");
    if (pos != std::string::npos && pos == shortName.length() - 2)
        shortName = shortName.substr(0, pos);
    if (shortName.find("BP_") == 0)
        shortName = shortName.substr(3);
    return shortName;
}

// 判断 actor 是否属于指定类别 (优先 IsA 精确判定, 避免类名子串误伤)
bool IsObjectKind(AActor* actor, WorldObjectKind kind, bool* outPickupStyleChest = nullptr) {
    if (outPickupStyleChest) *outPickupStyleChest = false;
    if (!actor || !actor->Class || !IsProbablyValidPtr(actor->Class))
        return false;

    bool isDrop = false, isTreasure = false, isPalEgg = false;
    if (!Try([&] { isDrop = actor->IsA(APalMapObjectDropItem::StaticClass()); }))
        isDrop = false;
    if (!Try([&] { isTreasure = actor->IsA(APalMapObjectTreasureBox::StaticClass()); }))
        isTreasure = false;
    if (!Try([&] { isPalEgg = actor->IsA(APalMapObjectPalEgg::StaticClass()); }))
        isPalEgg = false;

    UPalMapObjectConcreteModelBase* concrete = GetObjectConcreteModel(actor);
    bool isPickableModel = false;
    bool isPalEggModel = false;
    bool isChestModel = false;
    if (concrete) {
        Try([&] { isPickableModel = concrete->IsA(UPalMapObjectPickableItemModelBase::StaticClass()); });
        Try([&] { isPalEggModel = concrete->IsA(UPalMapObjectPalEggModel::StaticClass()); });
        Try([&] { isChestModel = concrete->IsA(UPalMapObjectTreasureBoxModel::StaticClass()); });
    }

    const std::string cls = GetActorClassName(actor);

    switch (kind) {
    case WorldObjectKind::Egg:
        // 可孵化帕鲁蛋: 优先精确 actor 类型, 兜底 ConcreteModel 为蛋模型的对象
        if (isPalEgg) return true;
        return isPalEggModel;

    case WorldObjectKind::DropItem:
        // 地面可拾取物品 (不含帕鲁蛋)
        if (isPalEgg || isPalEggModel) return false;
        if (isDrop) return true;
        if (isPickableModel) return true;
        return cls.find("DropItem") != std::string::npos ||
               cls.find("ItemDrop") != std::string::npos;

    case WorldObjectKind::Ore:
        // 与 world_actions 中 MineNearbyRocks 的类名匹配一致
        return cls.find("DamagableRock") != std::string::npos ||
               cls.find("WeakPointOre") != std::string::npos ||
               cls.find("Rock") != std::string::npos;

    case WorldObjectKind::Chest:
        if (isTreasure) return true;
        // "拾取型宝箱": 以可拾取/掉落物形式出现, 但 concrete model 是宝箱模型
        if (isDrop && isChestModel) {
            if (outPickupStyleChest) *outPickupStyleChest = true;
            return true;
        }
        return false;
    }
    return false;
}

// 从 MapObject 的 concrete model 解析物品 ID (用于显示具体物品中文名), 失败返回空串
std::string GetActorItemId(AActor* actor) {
    if (!actor) return "";

    APalMapObject* mapObject = static_cast<APalMapObject*>(actor);
    if (!mapObject || !IsProbablyValidPtr(mapObject)) return "";

    UPalMapObjectModel* model = nullptr;
    if (!Try([&] { model = mapObject->GetModel(); }) || !model || !IsProbablyValidPtr(model))
        return "";

    UPalMapObjectConcreteModelBase* concrete = nullptr;
    if (!Try([&] { concrete = model->GetConcreteModel(true); }) || !concrete || !IsProbablyValidPtr(concrete))
        return "";

    // 破坏掉落模型: DropItemInfos[0].ItemId.StaticId
    bool isDropOnDamag = false;
    if (Try([&] { isDropOnDamag = concrete->IsA(UPalMapObjectItemDropOnDamagModel::StaticClass()); }) && isDropOnDamag) {
        auto* dropModel = static_cast<UPalMapObjectItemDropOnDamagModel*>(concrete);
        auto& infos = dropModel->DropItemInfos;
        if (infos.IsValidIndex(0)) {
            const FName id = infos[0].ItemId.StaticId;
            if (!id.IsNone()) return id.ToString();
        }
    }

    // 地面上可直接拾取的物品模型: CreatePickupItemInfo()[0].ItemId.StaticId
    bool isPickupItem = false;
    if (Try([&] { isPickupItem = concrete->IsA(UPalMapObjectPickableItemModelBase::StaticClass()); }) && isPickupItem) {
        auto* pickModel = static_cast<UPalMapObjectPickableItemModelBase*>(concrete);
        const TArray<FPalItemAndNum> infos = pickModel->CreatePickupItemInfo();
        if (infos.IsValidIndex(0)) {
            const FName id = infos[0].ItemId.StaticId;
            if (!id.IsNone()) return id.ToString();
        }
    }
    return "";
}

// 为对象生成显示标签: 优先物品中文名 (映射表), 否则分类名 + 距离
std::string BuildObjectLabel(AActor* actor, WorldObjectKind kind, float dist, bool pickupStyleChest = false) {
    std::string base = GetObjectCategoryName(GetActorClassName(actor));
    if (kind == WorldObjectKind::Chest && pickupStyleChest)
        base = "拾取宝箱";

    const std::string itemId = GetActorItemId(actor);
    if (!itemId.empty() && !cheatState.useEnglishNames) {
        std::string cn;
        if (NameMapper::Get().IsLoaded() && NameMapper::Get().GetItemChineseName(itemId, cn))
            base = pickupStyleChest ? base + "·" + cn : cn;
    }
    return base + "（" + std::to_string(static_cast<int>(dist)) + "m）";
}

} // namespace

// ---------------------------------------------------------------------------
// 玩家位置 / 距离 / 类名
// ---------------------------------------------------------------------------

bool GetPlayerLocation(FVector& outLoc) {
    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !IsProbablyValidPtr(player))
        return false;
    return Try([&] { outLoc = player->K2_GetActorLocation(); });
}

float CalcDistanceToPlayer(AActor* actor, const FVector& playerLoc) {
    if (!actor || !IsProbablyValidPtr(actor))
        return -1.f;
    FVector actorLoc;
    if (!Try([&] { actorLoc = actor->K2_GetActorLocation(); }))
        return -1.f;
    return playerLoc.GetDistanceTo(actorLoc) / 100.f;
}

std::string GetActorClassName(AActor* actor) {
    if (!actor || !actor->Class || !IsProbablyValidPtr(actor->Class))
        return {};
    std::string name;
    if (!Try([&] { name = actor->Class->GetName(); }))
        return {};
    return name;
}

// ---------------------------------------------------------------------------
// 周围生物
// ---------------------------------------------------------------------------

bool CollectNearbyCreatures(std::vector<APalCharacter*>& out, float maxRangeCm, bool fullMap) {
    out.clear();

    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !IsProbablyValidPtr(player))
        return false;

    TArray<APalCharacter*> allPals;
    if (!GetTAllPals(&allPals))
        GetAllPalsFromWorld(&allPals);
    if (allPals.Num() == 0)
        return false;

    FVector playerLoc;
    if (!Try([&] { playerLoc = player->K2_GetActorLocation(); }))
        return false;

    for (int32 i = 0; i < allPals.Num(); ++i) {
        if (!allPals.IsValidIndex(i)) continue;
        APalCharacter* pal = allPals[i];
        if (!pal || !IsProbablyValidPtr(pal)) continue;
        if (pal == player) continue;

        FVector palLoc;
        if (!Try([&] { palLoc = pal->K2_GetActorLocation(); })) continue;
        if (!fullMap && palLoc.GetDistanceTo(playerLoc) > maxRangeCm) continue;
        out.push_back(pal);
    }
    return !out.empty();
}

std::string CreatureDisplayName(APalCharacter* pal) {
    if (!pal) return "Unknown";
    std::string raw;
    if (!Try([&] { raw = pal->GetName(); })) return "Unknown";

    std::string name = GetCleanPalName2(raw);
    if (!cheatState.useEnglishNames) {
        std::string cn;
        if (NameMapper::Get().IsLoaded() && NameMapper::Get().GetPalChineseName(name, cn))
            name = cn;
    }
    return name;
}

int CreatureLevel(APalCharacter* pal) {
    if (!pal) return 0;
    int lv = 0;
    Try([&] {
        if (UPalCharacterParameterComponent* params = pal->CharacterParameterComponent)
            if (UPalIndividualCharacterParameter* iv = params->GetIndividualParameter())
                lv = iv->SaveParameter.Level;
    });
    return lv;
}

float CreatureHealthFrac(APalCharacter* pal) {
    if (!pal) return 0.f;
    float frac = 0.f;
    Try([&] {
        if (UPalCharacterParameterComponent* params = pal->CharacterParameterComponent) {
            const auto hp = params->GetHP();
            const auto maxHp = params->GetMaxHP();
            const float m = static_cast<float>(maxHp.Value);
            if (m > 0.f) frac = static_cast<float>(hp.Value) / m;
        }
    });
    return frac;
}

// ---------------------------------------------------------------------------
// 周围世界对象
// ---------------------------------------------------------------------------

void CollectNearbyWorldObjects(WorldObjectKind kind, std::vector<NearbyWorldObject>& out,
                               float maxRangeCm, bool fullMap) {
    out.clear();

    FVector playerLoc;
    if (!GetPlayerLocation(playerLoc))
        return;

    UWorld* world = UWorld::GetWorld();
    if (!world)
        return;

    const auto& levels = world->Levels;
    for (int32 i = 0; i < levels.Num(); ++i) {
        ULevel* level = levels[i];
        if (!level || !IsProbablyValidPtr(level))
            continue;

        const auto& actors = level->Actors;
        for (int32 j = 0; j < actors.Num(); ++j) {
            AActor* actor = actors[j];
            if (!actor || !IsProbablyValidPtr(actor))
                continue;
            if (!actor->Class || !IsProbablyValidPtr(actor->Class))
                continue;

            // 必须是 MapObject (物品/矿石/宝箱都是地图物件)
            bool isMapObject = false;
            if (!Try([&] { isMapObject = actor->IsA(APalMapObject::StaticClass()); }) || !isMapObject)
                continue;

            bool pickupStyle = false;
            if (!IsObjectKind(actor, kind, &pickupStyle))
                continue;

            const float dist = CalcDistanceToPlayer(actor, playerLoc);
            if (dist < 0.f) continue;
            if (!fullMap && dist > (maxRangeCm / 100.f))
                continue;

            // 矿石已被破坏 (HP 归零) 则不再显示
            if (kind == WorldObjectKind::Ore) {
                APalMapObject* mo = static_cast<APalMapObject*>(actor);
                UPalMapObjectModel* mdl = nullptr;
                int32 hp = 1;
                if (Try([&] { mdl = mo->GetModel(); }) && mdl && IsProbablyValidPtr(mdl)) {
                    if (Try([&] { hp = mdl->Hp.CurrentValue; }) && hp <= 0)
                        continue;
                }
            }

            NearbyWorldObject obj;
            obj.actor = actor;
            obj.distance = dist;
            obj.pickupStyleChest = pickupStyle;
            obj.label = BuildObjectLabel(actor, kind, dist, pickupStyle);
            out.push_back(std::move(obj));
        }
    }

    // 按距离升序
    std::sort(out.begin(), out.end(),
              [](const NearbyWorldObject& a, const NearbyWorldObject& b) { return a.distance < b.distance; });
}

// ---------------------------------------------------------------------------
// 对象操作
// ---------------------------------------------------------------------------

void TeleportToActor(AActor* actor) {
    if (!actor || !IsProbablyValidPtr(actor)) return;
    FVector loc;
    if (!Try([&] { loc = actor->K2_GetActorLocation(); }))
        return;
    TeleportPlayerTo(FVector(loc.X, loc.Y + 100.0f, loc.Z));
}

void TeleportActorToPlayer(AActor* actor) {
    if (!actor || !IsProbablyValidPtr(actor)) return;

    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !IsProbablyValidPtr(player)) return;

    FVector pl;
    if (!Try([&] { pl = player->K2_GetActorLocation(); }))
        return;
    Try([&] { actor->K2_SetActorLocation(FVector(pl.X, pl.Y + 150.0f, pl.Z), false, nullptr, true); });
}

void PickupSingleItem(AActor* actor) {
    if (!actor || !IsProbablyValidPtr(actor)) return;

    APalMapObject* mapObject = static_cast<APalMapObject*>(actor);
    if (!mapObject || !IsProbablyValidPtr(mapObject)) return;

    UPalMapObjectModel* model = mapObject->GetModel();
    if (!model || !IsProbablyValidPtr(model)) return;

    UPalMapObjectConcreteModelBase* concrete = nullptr;
    if (!Try([&] { concrete = model->GetConcreteModel(true); }))
        return;
    if (!concrete || !IsProbablyValidPtr(concrete)) return;

    // Case 1: drop-on-damage model (先标记检测到才能拾取)
    bool isDropOnDamag = false;
    if (Try([&] { isDropOnDamag = concrete->IsA(UPalMapObjectItemDropOnDamagModel::StaticClass()); }) && isDropOnDamag) {
        auto* dropOnDamag = static_cast<UPalMapObjectItemDropOnDamagModel*>(concrete);
        Try([&] { dropOnDamag->RequestMarkDetectedByPlayer(true); });
        return;
    }

    // Case 2: generic pickable-item model
    bool isPickable = false;
    if (!Try([&] { isPickable = concrete->IsA(UPalMapObjectPickableItemModelBase::StaticClass()); }) || !isPickable)
        return;

    auto* pickable = static_cast<UPalMapObjectPickableItemModelBase*>(concrete);
    Try([&] { pickable->RequestPickup(false); });
}

void DestroySingleOre(AActor* actor) {
    if (!actor || !IsProbablyValidPtr(actor)) return;

    // 1) 采集掉落: 读取该矿石 concrete model 的掉落清单, 直接加进背包
    Try([&] {
        APalMapObject* mapObject = static_cast<APalMapObject*>(actor);
        if (!mapObject || !IsProbablyValidPtr(mapObject)) return;

        UPalMapObjectModel* model = nullptr;
        if (!Try([&] { model = mapObject->GetModel(); }) || !model || !IsProbablyValidPtr(model))
            return;

        UPalMapObjectConcreteModelBase* concrete = nullptr;
        if (!Try([&] { concrete = model->GetConcreteModel(true); }) || !concrete || !IsProbablyValidPtr(concrete))
            return;

        bool isDropOnDamag = false;
        if (!Try([&] { isDropOnDamag = concrete->IsA(UPalMapObjectItemDropOnDamagModel::StaticClass()); }) ||
            !isDropOnDamag) {
            // 非掉落型模型也尝试可拾取型
            bool isPick = false;
            if (Try([&] { isPick = concrete->IsA(UPalMapObjectPickableItemModelBase::StaticClass()); }) && isPick) {
                auto* pick = static_cast<UPalMapObjectPickableItemModelBase*>(concrete);
                const TArray<FPalItemAndNum> infos = pick->CreatePickupItemInfo();
                for (int32 i = 0; i < infos.Num(); ++i) {
                    const FName sid = infos[i].ItemId.StaticId;
                    if (!sid.IsNone() && infos[i].Num > 0)
                        AddItemToInventoryByName(sid.ToString(), infos[i].Num);
                }
            }
            return;
        }

        auto* dropModel = static_cast<UPalMapObjectItemDropOnDamagModel*>(concrete);
        auto& dropInfos = dropModel->DropItemInfos;
        for (int32 i = 0; i < dropInfos.Num(); ++i) {
            const FName sid = dropInfos[i].ItemId.StaticId;
            if (!sid.IsNone() && dropInfos[i].Num > 0)
                AddItemToInventoryByName(sid.ToString(), dropInfos[i].Num);
        }
    });

    // 2) 破坏: RPC 伤害 (服务器掉落/同步) + 本地 HP 归零 (视觉消失)
    DamageSingleMapObject(actor);
}

void OpenSingleChest(AActor* actor) {
    if (!actor || !IsProbablyValidPtr(actor)) return;

    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !IsProbablyValidPtr(player)) return;

    // 获取 PlayerId (int32) 用于服务端 RPC
    APalPlayerController* pc = GetPalPlayerController();
    int32 playerId = 0;
    if (pc && IsProbablyValidPtr(pc)) {
        APlayerState* ps = pc->PlayerState;
        if (ps && IsProbablyValidPtr(ps))
            playerId = ps->PlayerId;
    }

    // 通过 ConcreteModel 走服务端开箱 (真正给 loot 的路径)
    APalMapObject* mapObject = static_cast<APalMapObject*>(actor);
    if (mapObject && IsProbablyValidPtr(mapObject)) {
        UPalMapObjectModel* model = nullptr;
        if (Try([&] { model = mapObject->GetModel(); }) && model && IsProbablyValidPtr(model)) {
            UPalMapObjectConcreteModelBase* concrete = nullptr;
            if (Try([&] { concrete = model->GetConcreteModel(true); }) && concrete && IsProbablyValidPtr(concrete)) {
                bool isTreasureBoxModel = false;
                if (Try([&] { isTreasureBoxModel = concrete->IsA(UPalMapObjectTreasureBoxModel::StaticClass()); }) &&
                    isTreasureBoxModel) {
                    auto* tbModel = static_cast<UPalMapObjectTreasureBoxModel*>(concrete);
                    Try([&] { tbModel->RequestOpen_ServerInternal(playerId, false); });

                    // 客户端触发开启动画
                    bool isTreasureActor = false;
                    if (Try([&] { isTreasureActor = actor->IsA(APalMapObjectTreasureBox::StaticClass()); }) && isTreasureActor) {
                        auto* treasure = static_cast<APalMapObjectTreasureBox*>(actor);
                        Try([&] { treasure->TriggerOpen(); });
                    }
                    return;
                }
            }
        }
    }

    // 兜底: Obtainable 类型走 RequestObtain
    bool isObtainable = false;
    Try([&] { isObtainable = actor->IsA(APalLevelObjectObtainable::StaticClass()); });
    if (isObtainable) {
        auto* obtainable = static_cast<APalLevelObjectObtainable*>(actor);
        bool picked = false;
        Try([&] { picked = obtainable->bPickedInClient; });
        if (picked) {
            TeleportToActor(actor);
            return;
        }
        if (pc && IsProbablyValidPtr(pc) && pc->Transmitter && pc->Transmitter->Player) {
            Try([&] {
                player->InteractComponent->SetEnableInteract(true, false);
                pc->Transmitter->Player->RequestObtainLevelObject_ToServer(obtainable);
            });
        }
        return;
    }

    // 最终兜底: 传送过去
    TeleportToActor(actor);
}

void KillPal(SDK::APalCharacter* pal, SDK::APalCharacter* player) {
    if (!pal || !IsProbablyValidPtr(pal) || !player) return;

    // 1) 服务器正式伤害流程 (产生死亡/掉落), 与一键秒杀路径一致
    Try([&] {
        APalPlayerController* pc = GetPalPlayerController();
        if (pc && IsProbablyValidPtr(pc)) {
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
            pc->DamageReactionComponent_ProcessDeath_ToServer_ToNPC(pal);
        }
    });

    // 2) 本地兜底: 直接把 HP 写零 (同帕鲁编辑器改血方式), 保证秒杀必定生效
    Try([&] {
        UPalCharacterParameterComponent* params = pal->CharacterParameterComponent;
        if (!params || !IsProbablyValidPtr(params))
            return;
        params->SetHP(FFixedPoint64(0));
        UPalIndividualCharacterParameter* iv = params->GetIndividualParameter();
        if (iv && IsProbablyValidPtr(iv)) {
            iv->SaveParameter.Hp = FFixedPoint64(0);
            params->OnRep_IndividualParameter();
            iv->OnRep_SaveParameter();
        } else {
            params->OnRep_IndividualParameter();
        }
    });
}

// ---------------------------------------------------------------------------
// 玩家经验
// ---------------------------------------------------------------------------

std::int64_t GetPlayerExp() {
    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !IsProbablyValidPtr(player)) return 0;

    std::int64_t exp = 0;
    Try([&] {
        if (UPalCharacterParameterComponent* params = player->CharacterParameterComponent)
            if (UPalIndividualCharacterParameter* iv = params->GetIndividualParameter())
                exp = iv->SaveParameter.Exp;
    });
    return exp;
}

void AddPlayerExp(std::int64_t amount) {
    if (amount <= 0) return;

    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player || !IsProbablyValidPtr(player)) return;

    Try([&] {
        UPalCharacterParameterComponent* params = player->CharacterParameterComponent;
        if (!params || !IsProbablyValidPtr(params)) return;

        UPalIndividualCharacterParameter* iv = params->GetIndividualParameter();
        if (!iv || !IsProbablyValidPtr(iv)) return;

        iv->SaveParameter.Exp += amount;
        params->OnRep_IndividualParameter();
        iv->OnRep_SaveParameter();
    });
}

} // namespace pal::features::objects
