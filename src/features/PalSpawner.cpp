// features/PalSpawner.cpp — 帕鲁生成实现 (原 src/ui/cheat/pal_spawner_util.cpp)
//
// 保留原版全部语义: 数值 clamp、ParamSetup 失败时的兜底初始化、属性覆盖顺序、
// 世界 Actor 生成 + 队伍槽位挂载、以及被动/主动技能的深拷贝写入。
// 仅把 std::cout 换成 pal::log, 并统一使用 Helper 的字符串->FName 转换。

#include <pch.h>
#include "PalSpawner.hpp"

#include "core/Log.hpp"
#include "engine/GameHelper.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_set>

using namespace SDK;
using namespace Helper;

namespace pal::features::palspawn {

namespace {

// 把干净的帕鲁名 (如 "Alpaca") 转成游戏内的 CharacterID (如 "Pal_Alpaca")
std::string NormalizeCharacterID(const std::string& palName) {
    if (palName.rfind("Pal_", 0) == 0) return palName;
    if (palName.rfind("NPC_", 0) == 0) return palName;
    return "Pal_" + palName;
}

// 被动 key 列表 -> TArray<FName>。
// 注意: TAllocatedArray 析构时会 free 其内存, 而 TArray 的默认拷贝是浅拷贝,
// 所以这里必须显式深拷贝到堆内存 (与原版一致)。
TArray<FName> PassiveKeysToFNames(const std::vector<std::string>& keys) {
    TAllocatedArray<FName> tmp(static_cast<int32>(keys.size()));
    for (const auto& key : keys) {
        if (key.empty()) continue;
        tmp.Add(StringToFName(key));
    }

    const int32 count = tmp.Num();
    if (count > 0) {
        FName* data = static_cast<FName*>(malloc(static_cast<size_t>(count) * sizeof(FName)));
        memcpy(data, tmp.GetDataPtr(), static_cast<size_t>(count) * sizeof(FName));
        return TArray<FName>(data, count, count);
    }
    return TArray<FName>();
}

// EPalWazaID 数值列表 -> TArray<EPalWazaID> (同样需要深拷贝)
TArray<EPalWazaID> WazaIdsToArray(const std::vector<int>& wazas) {
    TAllocatedArray<EPalWazaID> tmp(static_cast<int32>(wazas.size()));
    for (int w : wazas)
        tmp.Add(static_cast<EPalWazaID>(w));

    const int32 count = tmp.Num();
    if (count > 0) {
        EPalWazaID* data = static_cast<EPalWazaID*>(malloc(static_cast<size_t>(count) * sizeof(EPalWazaID)));
        memcpy(data, tmp.GetDataPtr(), static_cast<size_t>(count) * sizeof(EPalWazaID));
        return TArray<EPalWazaID>(data, count, count);
    }
    return TArray<EPalWazaID>();
}

} // namespace

UPalIndividualCharacterHandle* SpawnPalWithOptions(const std::string& palID,
                                                   const PalSpawnOptions& opts) {
    UWorld* world = UWorld::GetWorld();
    if (!world || !world->OwningGameInstance) {
        log::Error("[PalSpawner] 无法获取 GameInstance");
        return nullptr;
    }

    APalPlayerCharacter* player = GetPalPlayerCharacter();
    APalPlayerState* playerState = GetPalPlayerState();
    if (!player || !playerState) {
        log::Error("[PalSpawner] 无法获取本地玩家");
        return nullptr;
    }

    const std::string characterID = NormalizeCharacterID(palID);
    const FName charFName = StringToFName(characterID);
    const FName uniqueNPCID;                     // 留空: 让游戏自动分配唯一 ID
    const FGuid ownerUID = playerState->PlayerUId;

    // Clamp 输入, 保证 ParamSetup 在正确的值上计算属性
    const int clampedLevel = std::clamp(opts.level, 1, 50);
    const int clampedRank  = std::clamp(opts.rank, 0, 255);

    // ---- 1. 初始化基础参数 ----
    FPalIndividualCharacterSaveParameter initParam{};
    TArray<EPalWazaID> wazaList = WazaIdsToArray(opts.wazas);
    TArray<FName> passiveList = PassiveKeysToFNames(opts.passives);

    bool ok = UPalUtility::GetInitializedCharacterSaveParemter_ParamSetup(
        world,
        charFName,
        uniqueNPCID,
        clampedLevel,
        opts.setTalents ? opts.talentHP : 0,     // TalentLevel
        ownerUID,
        &initParam,
        passiveList.Num() > 0,                   // DisableRandomPassiveSkill
        wazaList,
        passiveList,
        clampedRank,
        {},                                      // StatusRank
        false,                                   // RarePalAble
        0);                                      // FriendshipRank

    if (!ok) {
        // 部分版本的 ParamSetup 对特殊帕鲁 ID 返回失败, 用标准初始化接口兜底
        ok = UPalUtility::GetInitializedCharacterSaveParemter(
            world, charFName, uniqueNPCID, clampedLevel, ownerUID, &initParam,
            passiveList.Num() > 0, false);
    }

    if (!ok) {
        log::Error("[PalSpawner] 无法初始化帕鲁数据 {}", characterID);
        return nullptr;
    }

    // ---- 2. 覆盖用户指定属性 ----
    initParam.CharacterID     = charFName;
    initParam.Level           = static_cast<uint8>(clampedLevel);
    initParam.Rank            = static_cast<uint8>(clampedRank);
    initParam.Rank_HP         = static_cast<uint8>(std::clamp(opts.rankHP, 0, 255));
    initParam.Rank_Attack     = static_cast<uint8>(std::clamp(opts.rankAtk, 0, 255));
    initParam.Rank_Defence    = static_cast<uint8>(std::clamp(opts.rankDef, 0, 255));
    initParam.Rank_CraftSpeed = static_cast<uint8>(std::clamp(opts.rankCraft, 0, 255));

    if (opts.setTalents) {
        initParam.Talent_HP      = static_cast<uint8>(std::clamp(opts.talentHP, 0, 100));
        initParam.Talent_Melee   = static_cast<uint8>(std::clamp(opts.talentAtk, 0, 100));
        initParam.Talent_Shot    = static_cast<uint8>(std::clamp(opts.talentAtk, 0, 100));
        initParam.Talent_Defense = static_cast<uint8>(std::clamp(opts.talentDef, 0, 100));
    }

    if (opts.setExactHp)
        initParam.Hp = FFixedPoint64{ static_cast<int64>(opts.hpValue) };
    else
        initParam.Hp = FFixedPoint64{ initParam.MaxHP.Value };   // 默认满血

    // 性别: 0=随机 (不覆盖) 1=雄性 2=雌性
    if (opts.gender == 1)
        initParam.Gender = EPalGenderType::Male;
    else if (opts.gender == 2)
        initParam.Gender = EPalGenderType::Female;

    if (!opts.nickname.empty()) {
        const std::wstring wNick(opts.nickname.begin(), opts.nickname.end());
        initParam.NickName = FString(wNick.c_str());
        initParam.FilteredNickName = FString(wNick.c_str());
    }

    // 手动写入被动/主动技能 (ParamSetup 可能未应用传入的列表)
    if (passiveList.Num() > 0)
        initParam.PassiveSkillList = passiveList;
    if (wazaList.Num() > 0)
        initParam.EquipWaza = wazaList;

    // ---- 3. 生成帕鲁 ----
    // 统一采用"生成世界 Actor + 加入队伍"的方式, 保证帕鲁立即可见
    UPalCharacterManager* charMgr = UPalUtility::GetCharacterManager(world);
    if (!charMgr) {
        log::Error("[PalSpawner] 无法获取 PalCharacterManager");
        return nullptr;
    }

    FVector spawnLoc = player->K2_GetActorLocation();
    spawnLoc.Z += 150.0f;   // 略高于玩家, 避免卡进地面

    FNetworkActorSpawnParameters spawnParam{};
    spawnParam.SpawnLocation = spawnLoc;
    spawnParam.SpawnRotation = FRotator(0, 0, 0);
    spawnParam.SpawnScale    = FVector(1, 1, 1);
    spawnParam.bNeedAdjustToFloor = true;
    spawnParam.AdjustUpOffset     = 0.f;
    spawnParam.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    spawnParam.bStartAsInactivePalCharacter = false;   // 生成后直接可见可行动

    TDelegate<void(const FPalInstanceID&)> spawnCb;    // 空回调
    UPalIndividualCharacterHandle* handle = charMgr->SpawnNewCharacter(initParam, spawnParam, spawnCb);

    if (!handle) {
        log::Error("[PalSpawner] SpawnNewCharacter 返回空");
        return nullptr;
    }

    // ---- 4. (可选) 加入玩家队伍 ----
    if (opts.addToParty) {
        UPalOtomoHolderComponentBase* holder = UPalUtility::GetOtomoHolderComponent(world);
        if (holder && IsProbablyValidPtr(holder)) {
            const bool added = holder->AddOtomoHandleToFreeSlot(handle);
            if (added)
                log::Info("[PalSpawner] 已加入队伍: {}", characterID);
            else
                log::Warn("[PalSpawner] 队伍加入失败 (槽位已满?), 帕鲁保留在世界中");
        } else {
            log::Warn("[PalSpawner] 未找到 OtomoHolder, 帕鲁仅生成到世界");
        }
    }

    log::Info("[PalSpawner] 生成成功 {} Lv.{} Rank.{} (被动 {}, 技能 {})",
              characterID, static_cast<int>(initParam.Level), static_cast<int>(initParam.Rank),
              initParam.PassiveSkillList.Num(), initParam.EquipWaza.Num());
    return handle;
}

// ---------------------------------------------------------------------------
// 兼容旧接口
// ---------------------------------------------------------------------------

bool SpawnPalToPlayer(const std::string& palID, int level) {
    PalSpawnOptions opts;
    opts.level = std::clamp(level, 1, 50);
    opts.addToParty = true;
    return SpawnPalWithOptions(palID, opts) != nullptr;
}

bool SpawnPalWithParams(const std::string& palID, int level, int rank,
                        const std::vector<std::string>& passiveSkills) {
    PalSpawnOptions opts;
    opts.level = std::clamp(level, 1, 50);
    opts.rank  = std::clamp(rank, 1, 5);
    opts.passives = passiveSkills;
    opts.addToParty = true;
    return SpawnPalWithOptions(palID, opts) != nullptr;
}

// ---------------------------------------------------------------------------
// 对一只已存在的帕鲁批量补被动技能
// ---------------------------------------------------------------------------

bool ApplyPassiveSkillsToPal(APalCharacter* pal, const std::vector<std::string>& passiveKeys) {
    if (!pal || !pal->CharacterParameterComponent) return false;

    UPalIndividualCharacterParameter* individual = nullptr;
    if (!Try([&] { individual = pal->CharacterParameterComponent->GetIndividualParameter(); }) || !individual)
        return false;

    auto& passiveSkills = individual->SaveParameter.PassiveSkillList;

    std::unordered_set<std::string> existing;
    for (int i = 0; i < passiveSkills.Num(); ++i)
        existing.insert(passiveSkills[i].ToString());

    for (const auto& key : passiveKeys) {
        if (key.empty()) continue;
        if (existing.contains(key)) continue;

        const FName name = StringToFName(key);
        if (name.IsNone()) continue;
        passiveSkills.Add(name);
        existing.insert(key);
    }

    Try([&] { individual->OnRep_SaveParameter(); });
    return true;
}

} // namespace pal::features::palspawn
