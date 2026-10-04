#include <pch.h>
#include "Hotkeys.hpp"
#include "CheatState.hpp"
#include <Windows.h>

Hotkeys key;

void TickHotkeys()
{
    // World Speed
    if (GetAsyncKeyState(key.hotkeyToggleWorldSpeed) & 1)
    {
        key.worldSpeedToggled = !key.worldSpeedToggled;
        ChangeWorldSpeed(key.worldSpeedToggled ? 10.0f : 1.0f);
    }

    if (GetAsyncKeyState(key.hotkeyStamina) & 1)
    {
        key.staminaToggled = !key.staminaToggled;
		cheatState.infStamina = key.staminaToggled;
    }

    // ESP
    if (GetAsyncKeyState(key.hotkeyToggleESP) & 1)
    {
        key.espToggled = !key.espToggled;
        cheatState.espEnabled = key.espToggled;
        cheatState.espBoxes = key.espToggled;
        cheatState.espShowDistance = key.espToggled;
        cheatState.espShowNames = key.espToggled;
        cheatState.espShowPalHealth = key.espToggled;
        cheatState.espShowPals = key.espToggled;
        cheatState.espShowRelics = key.espToggled;
    }

    if (GetAsyncKeyState(key.hotkeyToggleRelic) & 1)
    {
        key.relicToggled = !key.relicToggled;
        cheatState.espEnabled = key.relicToggled;
        cheatState.espShowRelics = key.relicToggled;
    }

    if (GetAsyncKeyState(key.hotkeyToggleAttack) & 1)
    {
        key.attackToggled = !key.attackToggled;
        if (key.attackToggled)
        {
            cheatState.attack = 90000;
            SetPlayerAttackParam();
        }
        else
        {
            cheatState.attack = 1;
            SetPlayerAttackParam();
        }
		
    } 

    if (GetAsyncKeyState(key.hotkeyRefreshWeight) & 1)
    {
        SetPlayerInventoryWeight();
    }
}

void TickHotkeysOneShot()
{
    // Repair weapon: only run once per press
    static bool repairKeyDown = false;

    if (GetAsyncKeyState(key.hotkeyRepairWeapon) & 0x8000) // key is held
    {
        if (!repairKeyDown)
        {
            // First frame key is pressed
            repairKeyDown = true;
            IncreaseAllDurability();
        }
    }
    else
    {
        // Key released 鈫?reset flag
        repairKeyDown = false;
    }

    if (GetAsyncKeyState(key.hotkeyTeleportHome) & 1)
    {
		TeleportPlayerToHome();
    }
}

