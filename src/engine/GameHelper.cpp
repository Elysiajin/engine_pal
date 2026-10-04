#include <pch.h>
#include "engine/GameHelper.hpp"
#include <random>
#include <ctime>
#include <mutex>
#include <queue>
#include <future>
#include <memory>
#include <type_traits>

using namespace SDK;

namespace Helper
{
    APalPlayerCharacter* GetPalPlayerCharacter()
    {

        UWorld* world = UWorld::GetWorld();
        if (!world) {
            return nullptr;
        }

        if (!world->OwningGameInstance) {
            return nullptr;
        }

        auto localPlayers = world->OwningGameInstance->LocalPlayers;
        if (localPlayers.Num() == 0) {
            return nullptr;
        }

        auto controller = localPlayers[0]->PlayerController;
        if (!controller) {
            return nullptr;
        }

        if (!controller->AcknowledgedPawn) {
            return nullptr;
        }

        return static_cast<APalPlayerCharacter*>(controller->AcknowledgedPawn);
    }

    ULocalPlayer* GetLocalPlayer()
    {

        UWorld* world = UWorld::GetWorld();
        if (!world) {
            return nullptr;
        }

        if (!world->OwningGameInstance) {
            return nullptr;
        }

        const auto& localPlayers = world->OwningGameInstance->LocalPlayers;
        if (localPlayers.Num() == 0) {
            return nullptr;
        }

        return localPlayers[0];
    }

    SDK::APalPlayerController* GetPalPlayerController()
    {
        SDK::APalPlayerCharacter* pPlayer = GetPalPlayerCharacter();
        if (!pPlayer)
            return nullptr;

        // 先尝试游戏专用的 GetPalPlayerController()，若失败则回退到标准 Controller
        SDK::APalPlayerController* pc = nullptr;
        if (Helper::Try([&] { pc = static_cast<SDK::APalPlayerController*>(pPlayer->GetPalPlayerController()); }))
            if (pc && Helper::IsProbablyValidPtr(pc))
                return pc;

        // 回退：使用标准的 AController
        AController* ctrl = nullptr;
        if (Helper::Try([&] { ctrl = pPlayer->Controller; }) && ctrl && Helper::IsProbablyValidPtr(ctrl))
            return static_cast<SDK::APalPlayerController*>(ctrl);

        return nullptr;
    }

    APalPlayerState* GetPalPlayerState()
    {
        SDK::APalPlayerCharacter* pPlayer = GetPalPlayerCharacter();
        if (!pPlayer)
            return nullptr;

        return static_cast<APalPlayerState*>(pPlayer->PlayerState);
    }

    UPalPlayerInventoryData* GetInventoryComponent()
    {
        APalPlayerState* pPlayerState = GetPalPlayerState();
        if (!pPlayerState)
            return nullptr;

        return pPlayerState->InventoryData;
    }

    SDK::UPalCharacterImportanceManager* GetCharacterImpManager()
    {
        SDK::UWorld* pWorld = UWorld::GetWorld();
        if (!pWorld)
            return nullptr;

        SDK::UGameInstance* pGameInstance = pWorld->OwningGameInstance;
        if (!pGameInstance)
            return nullptr;

        return static_cast<SDK::UPalGameInstance*>(pGameInstance)->CharacterImportanceManager;
    }

    bool GetTAllNPC(SDK::TArray<class SDK::APalCharacter*>* outResult)
    {
        UPalCharacterImportanceManager* mPal = GetCharacterImpManager();
        if (!mPal)
            return false;

        mPal->GetAllNPC(outResult);
        return true;
    }

    bool GetTAllPals(SDK::TArray<class SDK::APalCharacter*>* outResult)
    {
        SDK::UPalCharacterImportanceManager* mPal = GetCharacterImpManager();
        if (!mPal)
            return false;

        mPal->GetAllPalCharacter(outResult);
        return true;
    }

    void GetAllPalsFromWorld(SDK::TArray<class SDK::APalCharacter*>* outResult)
    {
        if (!outResult)
            return;
        outResult->Clear();

        // 收集去重容器
        std::vector<SDK::APalCharacter*> collected;

        auto AddUnique = [&](SDK::APalCharacter* pal)
        {
            if (!pal || !Helper::IsProbablyValidPtr(pal))
                return;
            // 去重：避免同指针重复
            for (size_t k = 0; k < collected.size(); ++k)
                if (collected[k] == pal)
                    return;
            collected.push_back(pal);
        };

        // 第一步：用游戏官方维护的 CharacterImportanceManager 作为可靠基线。
        // 这是 Palworld 内部登记当前加载角色的列表，必然包含大部分附近的帕鲁。
        {
            SDK::UPalCharacterImportanceManager* mgr = GetCharacterImpManager();
            if (mgr && Helper::IsProbablyValidPtr(mgr))
            {
                SDK::TArray<SDK::APalCharacter*> managed;
                Helper::Try([&] { mgr->GetAllPalCharacter(&managed); });
                for (int32 i = 0; i < managed.Num(); ++i)
                {
                    if (!managed.IsValidIndex(i))
                        continue;
                    AddUnique(managed[i]);
                }
            }
        }

        // 第二步：遍历世界所有已加载 Actor，补充 ImportanceManager 可能漏掉的帕鲁。
        // 关键：把每个 actor 的类型判断包进 Try，因为世界里存在正在被 GC/销毁的
        // stale actor，IsA 会读取已释放的 Class 指针导致崩溃。一旦崩溃被全局 handler
        // 吞掉，整个 ESP 就会"一个都画不出来"。包上 Try 后单个坏 actor 最多被跳过。
        SDK::UWorld* world = SDK::UWorld::GetWorld();
        if (world)
        {
            const auto& levels = world->Levels;
            for (int32 i = 0; i < levels.Num(); ++i)
            {
                SDK::ULevel* level = levels[i];
                if (!level || !Helper::IsProbablyValidPtr(level))
                    continue;

                const auto& actors = level->Actors;
                for (int32 j = 0; j < actors.Num(); ++j)
                {
                    SDK::AActor* actor = actors[j];
                    if (!actor || !Helper::IsProbablyValidPtr(actor))
                        continue;

                    // 类型判断放 Try 里：stale actor 的 Class 可能已失效
                    bool isPal = false;
                    if (!Helper::Try([&] { isPal = actor->IsA(SDK::APalCharacter::StaticClass()); }))
                        continue;
                    if (!isPal)
                        continue;

                    AddUnique(static_cast<SDK::APalCharacter*>(actor));
                }
            }
        }

        // 填回输出数组
        for (size_t k = 0; k < collected.size(); ++k)
            outResult->Add(collected[k]);
    }

    SDK::APalWeaponBase* GetPlayerEquippedWeapon()
    {
        SDK::APalPlayerCharacter* pPalCharacter = GetPalPlayerCharacter();
        if (!pPalCharacter)
            return nullptr;

        SDK::UPalShooterComponent* pWeaponInventory = pPalCharacter->ShooterComponent;
        if (!pWeaponInventory)
            return nullptr;

        return pWeaponInventory->HasWeapon;
    }

    bool IsAlive(SDK::AActor* pChar)
    {
        if (!Helper::IsProbablyValidPtr(pChar)) return false;

        // Cast then verify again as APalCharacter
        SDK::APalCharacter* pal = static_cast<SDK::APalCharacter*>(pChar);
        if (!Helper::IsProbablyValidPtr(pal)) return false;

        // Access member safely (read can fault if owner is stale)
        auto* params = (SDK::UPalCharacterParameterComponent*)nullptr;
        if (!Helper::Try([&] { params = pal->CharacterParameterComponent; }) ||
            !Helper::IsProbablyValidPtr(params))
            return false;

        // Use the parameter component's own member "IsLive()" instead of
        // UPalUtility::IsDead(). The static utility call goes through ProcessEvent
        // and can throw/crash on certain states, which (once swallowed) makes every
        // pal look "dead" -> ESP draws nothing and world actions skip all pals.
        bool isLive = false;
        if (!Helper::SafeCallRet(isLive, [&] { return params->IsLive(); }))
            return false; // treat failed call as "not safe"

        return isLive;
    }

    bool IsABaseWorker(SDK::APalCharacter* pChar, bool bLocalControlled)
    {
        if (!Helper::IsProbablyValidPtr(pChar)) return false;
        if (!IsAlive(pChar)) return false;

        // Prefer the parameter component's own member flags. "IsAssignedToAnyWork()"
        // + "IsCooping()" reliably identify base-camp workers without the fragile
        // UPalUtility::IsLocalPlayerCampPal / IsBaseCampPal static calls.
        auto* params = (SDK::UPalCharacterParameterComponent*)nullptr;
        if (!Helper::Try([&] { params = pChar->CharacterParameterComponent; }) ||
            !Helper::IsProbablyValidPtr(params))
            return false;

        bool out = false;
        if (!Helper::SafeCallRet(out, [&] { return params->IsAssignedToAnyWork(); }))
            return false;

        // A pal assigned to any work (or cooping/working) is a base worker.
        if (out)
            return true;

        bool cooping = false;
        if (Helper::SafeCallRet(cooping, [&] { return params->IsCooping(); }))
            return cooping;
        return false;
    }

    bool IsTamed(SDK::APalCharacter* pChar)
    {
        if (!Helper::IsProbablyValidPtr(pChar)) return false;
        if (!IsAlive(pChar)) return false;

        // A player's otomo (riding / party / deployed pal) is tamed. Using the
        // parameter component member avoids the fragile UPalUtility static call.
        auto* params = (SDK::UPalCharacterParameterComponent*)nullptr;
        if (!Helper::Try([&] { params = pChar->CharacterParameterComponent; }) ||
            !Helper::IsProbablyValidPtr(params))
            return false;

        bool isOtomo = false;
        if (!Helper::SafeCallRet(isOtomo, [&] { return params->IsOtomo(); }))
            return false;

        return isOtomo;
    }

    float GetDistance(const SDK::FVector2D& a, const SDK::FVector2D& b)
    {
        return sqrtf(
            (a.X - b.X) * (a.X - b.X) +
            (a.Y - b.Y) * (a.Y - b.Y)
        );
    }

    bool HasCameraLOS_Kismet(SDK::APlayerController* PC,
        SDK::APalPlayerCharacter* Player,
        SDK::APalCharacter* Pal)
    {
        if (!PC || !Player || !Pal) return false;

        // Camera origin
        FVector camLoc; FRotator camRot;
        PC->GetPlayerViewPoint(&camLoc, &camRot);

        // Target points (remove head if your dump lacks it)
        const FVector head = Pal->GetHPGaugeLocation();
        const FVector base = Pal->K2_GetActorLocation();
        const FVector chest = base + FVector(0.f, 0.f, 40.f);

        // Use TraceTypeQuery1 (commonly Visibility)
        const SDK::ETraceTypeQuery visTrace = SDK::ETraceTypeQuery::TraceTypeQuery1;

        auto TryPoint = [&](const FVector& tgt)->bool
            {
                // Pass 1: ignore only the local player
                SDK::TArray<SDK::AActor*> ignore1;
                ignore1.Add(Player);

                SDK::FHitResult hit1{};
                const bool pass1 =
                    SDK::UKismetSystemLibrary::LineTraceSingle(
                        /*WorldContextObject*/ Player,
                        /*Start*/ camLoc,
                        /*End*/   tgt,
                        /*TraceChannel*/ visTrace,
                        /*bTraceComplex*/ true,
                        /*ActorsToIgnore*/ ignore1,
                        /*DrawDebugType*/ SDK::EDrawDebugTrace::ForDuration,
                        /*OutHit*/ &hit1,
                        /*bIgnoreSelf*/ true,
                        /*TraceColor*/   SDK::FLinearColor(0, 0, 0, 0),
                        /*TraceHitColor*/SDK::FLinearColor(0, 0, 0, 0),
                        /*DrawTime*/     3.0f
                    );

                if (!hit1.bBlockingHit)
                    return true; // clear path

                // Pass 2: also ignore the Pal; if it becomes clear, first hit was the Pal
                SDK::TArray<SDK::AActor*> ignore2 = ignore1;
                ignore2.Add(Pal);

                SDK::FHitResult hit2{};
                const bool pass2 =
                    SDK::UKismetSystemLibrary::LineTraceSingle(
                        Player, camLoc, tgt, visTrace, true, ignore2,
                        SDK::EDrawDebugTrace::None, &hit2, true,
                        SDK::FLinearColor(0, 0, 0, 0), SDK::FLinearColor(0, 0, 0, 0), 0.0f
                    );

                if (!hit2.bBlockingHit)
                    return true; // after ignoring Pal, nothing blocks => Pal was first hit

                return false;    // still blocked by world/other
            };

        if (TryPoint(head))  return true;
        if (TryPoint(chest)) return true;
        if (TryPoint(base))  return true;

        return false;
    }
}

// ---------------------------------------------------------------------------
// 游戏就绪检测 (供 Bootstrap/FrameDriver 使用)
// ---------------------------------------------------------------------------
namespace Helper {

bool IsGameReadyImpl() {
    SDK::UWorld* world = SDK::UWorld::GetWorld();
    if (!world || !world->OwningGameInstance) return false;
    const auto& locals = world->OwningGameInstance->LocalPlayers;
    return locals.Num() > 0 && locals[0] && locals[0]->PlayerController;
}

bool IsGameReady() {
    bool ready = false;
    Try([&] { ready = IsGameReadyImpl(); });
    return ready;
}

SDK::FName StringToFName(const std::string& str) {
    if (str.empty()) return SDK::FName();
    SDK::FName result;
    // FString(const wchar_t*) 只保存裸指针而不拷贝, 因此 wstring 必须活到调用结束
    const std::wstring wide(str.begin(), str.end());
    Try([&] {
        SDK::UKismetStringLibrary* lib = SDK::UKismetStringLibrary::GetDefaultObj();
        if (!lib) return;
        result = lib->Conv_StringToName(SDK::FString(wide.c_str()));
    });
    return result;
}

} // namespace Helper
