#include <pch.h>
#include <cstring>
#include <string>
#include "Aimbot.hpp"
#include "CheatState.hpp"
#include "core/Gfx.hpp"
#include "engine/GameHelper.hpp"
#include <Windows.h>

using namespace SDK;
using namespace Helper;

namespace gfx = pal::gfx;   // 覆盖层绘制门面 (本文件在全局命名空间, 需要别名)

struct AimbotTarget
{
    APalCharacter* Pal;
    FVector2D ScreenPos;
    float Fov;

    AimbotTarget(APalCharacter* p, const FVector2D& s, float f)
        : Pal(p), ScreenPos(s), Fov(f) {
    }
};

FRotator CalcLookAtRotation(const SDK::FVector& from, const SDK::FVector& to)
{
    SDK::FVector dir = to - from;
    float yaw = atan2f(dir.Y, dir.X) * (180.0f / 3.14159265f);
    float dist = sqrtf(dir.X * dir.X + dir.Y * dir.Y);
    float pitch = atan2f(dir.Z, dist) * (180.0f / 3.14159265f);
    return FRotator(pitch, yaw, 0.0f); // Roll is usually zero
}

bool IsValidAimbotTarget(APalCharacter* pal)
{
    if (!pal) {
        return false;
    }

    auto params = pal->CharacterParameterComponent;
    if (!params) {
        return false;
    }

    if (!params->IndividualParameter) {
        return false;
    }

    if (!IsAlive(pal))
    {
        return false;
    }

    // Passes all checks
    return true;
}

AimbotTarget FindBestPalTarget(APlayerController* controller, FVector2D screenCenter, float maxFov)
{
    SDK::TArray<SDK::APalCharacter*> pals;
    if (!GetTAllPals(&pals))
        return { nullptr, {}, FLT_MAX };

    float maxDistance = 35000.0f;
    float bestFov = maxFov;
    APalCharacter* bestPal = nullptr;
    FVector2D bestScreen{};

    for (int i = 0; i < pals.Num(); ++i)
    {
        APalCharacter* pal = pals[i];
        if (!pal || !IsAlive(pal) || IsABaseWorker(pal))
            continue;

        if (!IsValidAimbotTarget(pal))
            continue;

        // Validate controller pawn
        if (!controller || !controller->K2_GetPawn())
            continue;

        // Validate distance
        float distance = controller->K2_GetPawn()->GetDistanceTo(pal);
        if (distance > maxDistance)
            continue;

        FVector2D screenPos;
        if (!controller->ProjectWorldLocationToScreen(pal->K2_GetActorLocation(), &screenPos, false))
            continue;

        float fov = GetDistance(screenPos, screenCenter);
        if (fov < bestFov)
        {
            bestFov = fov;
            bestPal = pal;
            bestScreen = screenPos;
        }
    }

    return { bestPal, bestScreen, bestFov };
}

void MoveMouseRelative(int dx, int dy)
{
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

void RunPalAimbot()
{
    APalPlayerCharacter* player = GetPalPlayerCharacter();
    if (!player) return;

    APalPlayerController* controller = GetPalPlayerController();
    if (!controller) return;

    const gfx::Vec2 screen = gfx::ScreenSize();
    FVector2D screenCenter(screen.x * 0.5f, screen.y * 0.5f);

    AimbotTarget target = FindBestPalTarget(controller, screenCenter, cheatState.aimbotFov);

    if (!target.Pal || !IsAlive(target.Pal))
        return;

    //Visibility check :)
    if (cheatState.aimbotVisibilityCheck)
    {
        const bool isVisible = Helper::HasCameraLOS_Kismet(controller, player, target.Pal);
        if (!isVisible)
            return;
    }
    FVector targetLoc = FVector(0, 0, 0);
    targetLoc = target.Pal->K2_GetActorLocation();
    USkeletalMeshComponent* mesh = target.Pal->Mesh;

    // 根据用户选择的瞄准部位选取对应骨骼点:
    //   0=头部(head)  1=身体(spine/pelvis)  2=腿部(thigh/calf)
    const char* aimBone = "head";
    switch (cheatState.aimbotAimPart)
    {
        case 1: aimBone = "spine_01"; break;
        case 2: aimBone = "thigh_l";  break;
        default: aimBone = "head";    break;
    }

    if (mesh)
    {
        // 用游戏本地化字符串转换接口构造 FName(本 SDK 的 FName 无字符串构造函数)
        auto MakeName = [](const char* s) -> FName {
            static SDK::UKismetStringLibrary* lib = SDK::UKismetStringLibrary::GetDefaultObj();
            if (!lib) return FName();
            std::wstring w(s, s + strlen(s));
            return lib->Conv_StringToName(FString(w.c_str()));
        };

        FName aimBoneName = MakeName(aimBone);
        // 若指定骨骼不存在(不同体型), 逐级回退到合法骨骼
        if (aimBoneName.IsNone() || !mesh->DoesSocketExist(aimBoneName))
        {
            const char* fallbacks[] = { "pelvis", "neck_01", "spine_01", "head" };
            for (const char* fb : fallbacks)
            {
                FName fn = MakeName(fb);
                if (!fn.IsNone() && mesh->DoesSocketExist(fn)) { aimBoneName = fn; break; }
            }
        }
        if (!aimBoneName.IsNone() && mesh->DoesSocketExist(aimBoneName))
        {
            targetLoc = mesh->GetSocketLocation(aimBoneName);
        }
    }

    if (!controller) return;

    FVector cameraLoc;
    FRotator cameraRot;
    controller->GetPlayerViewPoint(&cameraLoc, &cameraRot);

    FRotator desiredRot = CalcLookAtRotation(cameraLoc, targetLoc);
    desiredRot.Roll = 0.0f;

    float smooth = cheatState.aimbotSmooth;
    if (smooth < 0.0f) smooth = 0.0f;
    if (smooth > 1.0f) smooth = 1.0f;

    float deltaYaw = desiredRot.Yaw - cameraRot.Yaw;
    float deltaPitch = desiredRot.Pitch - cameraRot.Pitch;

    if (deltaYaw > 180.f) deltaYaw -= 360.f;
    if (deltaYaw < -180.f) deltaYaw += 360.f;

    if (deltaPitch > 180.f) deltaPitch -= 360.f;
    if (deltaPitch < -180.f) deltaPitch += 360.f;

    // Apply smooth factor (snapFactor closer to 1 = faster)
    float snapFactor = powf(1.0f - smooth, 0.5f);
    if (snapFactor < 0.01f) snapFactor = 0.01f;
    else if (snapFactor > 1.0f) snapFactor = 1.0f;

    deltaYaw *= snapFactor;
    deltaPitch *= snapFactor;

    // Mouse sensitivity conversion (lower = faster)
    float sensitivity = 0.15f;

    float rawX = deltaYaw / sensitivity;
    float rawY = -deltaPitch / sensitivity;

    int mouseX = static_cast<int>(rawX);
    int mouseY = static_cast<int>(rawY);

    if (smooth <= 0.001f)
    {
        if (mouseX == 0 && fabsf(rawX) > 0.01f)
            mouseX = (rawX > 0.0f ? 1 : -1);

        if (mouseY == 0 && fabsf(rawY) > 0.01f)
            mouseY = (rawY > 0.0f ? 1 : -1);
    }

    MoveMouseRelative(mouseX, mouseY);
}







