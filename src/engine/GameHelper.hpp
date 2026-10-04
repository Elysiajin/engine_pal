#pragma once
#include <pch.h>



namespace Helper
{
    SDK::APalPlayerCharacter* GetPalPlayerCharacter();
    SDK::ULocalPlayer* GetLocalPlayer();
    SDK::APalPlayerController* GetPalPlayerController();
    SDK::UPalPlayerInventoryData* GetInventoryComponent();
    SDK::APalPlayerState* GetPalPlayerState();
    SDK::APalWeaponBase* GetPlayerEquippedWeapon();
    bool GetTAllPals(SDK::TArray<class SDK::APalCharacter*>* outResult);
    // 遍历当前世界中所有已加载的 APalCharacter（帕鲁/NPC/玩家）。
    // CharacterImportanceManager::GetAllPalCharacter 可能漏掉部分未登记重要度的帕鲁，
    // 用世界 Actor 遍历来补齐，避免近处帕鲁在 ESP 中被漏画。
    void GetAllPalsFromWorld(SDK::TArray<class SDK::APalCharacter*>* outResult);
    bool IsAlive(SDK::AActor* pCharacter);
    bool IsABaseWorker(SDK::APalCharacter* pChar, bool bLocalControlled = true);
    bool IsTamed(SDK::APalCharacter* pChar);

    bool GetTAllNPC(SDK::TArray<class SDK::APalCharacter*>* outResult);
    SDK::UPalCharacterImportanceManager* GetCharacterImpManager();

    // 字符串 -> FName (经 UKismetStringLibrary::Conv_StringToName)。
    // 内部保持 FString 的底层缓冲存活到调用结束; 失败返回 None。
    SDK::FName StringToFName(const std::string& str);
    float GetDistance(const SDK::FVector2D& a, const SDK::FVector2D& b);
    bool HasCameraLOS_Kismet(SDK::APlayerController* PC, SDK::APalPlayerCharacter* Player, SDK::APalCharacter* Pal);

    inline bool IsProbablyValidPtr(const void* p) {
        if (!p) return false;
        const uintptr_t a = reinterpret_cast<uintptr_t>(p);
#ifdef _WIN64
        if (a < 0x10000 || a > 0x00007FFFFFFF0000ull) return false;
#else
        if (a < 0x10000 || a > 0x7FFF0000u) return false;
#endif
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        __try { volatile const void* vt = *reinterpret_cast<void* const*>(p); (void)vt; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return true;
    }

    template <typename Fn>
    inline bool Try(Fn&& fn) {
#if defined(_MSC_VER)
        __try { fn(); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
        fn(); return true;
#endif
    }

    // 游戏就绪检测 (World/GameInstance/LocalPlayer 就绪)
    bool IsGameReady();
    bool IsGameReadyImpl();  // 无 SEH 版本, 调用方自包 Try

    template <class Ret, class Fn>
    inline bool SafeCallRet(Ret& out, Fn&& fn) {
        Ret tmp{};
        if (!Try([&] { tmp = fn(); })) return false;
        out = tmp;
        return true;
    }

}
