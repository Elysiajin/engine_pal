#include <pch.h>
#include "features/CheatState.hpp"
#include "engine/ConfigManager.hpp"
#include "features/Hotkeys.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    constexpr int ConfigVersion = 2;

    fs::path GetDllDirectory()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(reinterpret_cast<HINSTANCE>(&__ImageBase), path, MAX_PATH);
        return fs::path(path).parent_path();
    }

    fs::path GetConfigDirectory()
    {
        return GetDllDirectory() / L"c_settings";
    }

    fs::path GetJsonConfigPath(const std::string& filename)
    {
        fs::path path(filename);
        path.replace_extension(L".json");
        return GetConfigDirectory() / path.filename();
    }

    template <typename T>
    void ReadValue(const json& object, const char* key, T& target)
    {
        const auto it = object.find(key);
        if (it == object.end() || it->is_null())
            return;

        try
        {
            target = it->get<T>();
        }
        catch (const json::exception&)
        {
            pal::log::Log(pal::log::Level::Warn, "config", "Ignoring invalid value for {}", key);
        }
    }

    json SerializeCheatState()
    {
        json bulkItems = json::array();
        for (const auto& item : cheatState.bulkItems)
            bulkItems.push_back({ { "id", item.id }, { "count", item.count } });

        return {
            { "worldSpeed", cheatState.worldSpeed },
            { "weaponDamage", cheatState.weaponDamage },
            { "attack", cheatState.attack },
            { "weight", cheatState.weight },
            { "infAmmo", cheatState.infAmmo },
            { "infMag", cheatState.infMag },
            { "infStamina", cheatState.infStamina },
            { "cameraFov", cheatState.cameraFov },
            { "cameraBrightness", cheatState.cameraBrightness },
            { "isSilent", cheatState.isSilent },
            { "aimbotEnabled", cheatState.aimbotEnabled },
            { "aimbotShowFov", cheatState.aimbotShowFov },
            { "aimbotDrawFOV", cheatState.aimbotDrawFOV },
            { "aimbotFov", cheatState.aimbotFov },
            { "aimbotSmooth", cheatState.aimbotSmooth },
            { "aimbotHotkey", cheatState.aimbotHotkey },
            { "aimbotVisibilityCheck", cheatState.aimbotVisibilityCheck },
            { "espEnabled", cheatState.espEnabled },
            { "espBoxes", cheatState.espBoxes },
            { "espShowNames", cheatState.espShowNames },
            { "espShowDistance", cheatState.espShowDistance },
            { "espDistance", cheatState.espDistance },
            { "espShowPalHealth", cheatState.espShowPalHealth },
            { "espShowPals", cheatState.espShowPals },
            { "espShowRelics", cheatState.espShowRelics },
            { "espShowWaypoints", cheatState.espShowWaypoints },
            { "espBoxes", cheatState.espBoxes },
            { "espBoxes3D", cheatState.espBoxes3D },
            { "espSkeleton", cheatState.espSkeleton },
            { "espThermal", cheatState.espThermal },
            { "isFly", cheatState.isFly },
            { "flySpeed", cheatState.flySpeed },
            { "isInvicity", cheatState.isInvicity },
            { "invisible", cheatState.invisible },
            { "aimbotAimPart", cheatState.aimbotAimPart },
            { "espColorBox", { cheatState.espColorBox[0], cheatState.espColorBox[1], cheatState.espColorBox[2], cheatState.espColorBox[3] } },
            { "espColorBox3D", { cheatState.espColorBox3D[0], cheatState.espColorBox3D[1], cheatState.espColorBox3D[2], cheatState.espColorBox3D[3] } },
            { "espColorSkeleton", { cheatState.espColorSkeleton[0], cheatState.espColorSkeleton[1], cheatState.espColorSkeleton[2], cheatState.espColorSkeleton[3] } },
            { "espColorName", { cheatState.espColorName[0], cheatState.espColorName[1], cheatState.espColorName[2], cheatState.espColorName[3] } },
            { "statusImmune", cheatState.statusImmune },
            { "foodNeverSpoil", cheatState.foodNeverSpoil },
            { "timeFreeze", cheatState.timeFreeze },
            { "killRange", cheatState.killRange },
            { "craftingSpeed", cheatState.craftingSpeed },
            { "defence", cheatState.defence },
            { "speedMultiplier", cheatState.speedMultiplier },
            { "playerLevel", cheatState.playerLevel },
            { "showMenu", pal::core::input::IsMenuOpen() },
            { "useEnglishNames", cheatState.useEnglishNames },
            // Build Unlock (建造/制作解锁) 开关：随配置保存，读取后恢复 UI 选择
            { "buildUnlockEnabled", cheatState.buildUnlockEnabled },
            { "buildIgnoreRequirements", cheatState.buildIgnoreRequirements },
            { "buildNoConsumeMaterial", cheatState.buildNoConsumeMaterial },
            { "buildIgnoreGroundPlacement", cheatState.buildIgnoreGroundPlacement },
            { "buildAllowOverlapTerminal", cheatState.buildAllowOverlapTerminal },
            { "buildIgnoreNearBoss", cheatState.buildIgnoreNearBoss },
            { "buildIgnoreOil", cheatState.buildIgnoreOil },
            { "buildIgnoreBaseLimit", cheatState.buildIgnoreBaseLimit },
            { "buildIgnoreCampLimit", cheatState.buildIgnoreCampLimit },
            { "buildIgnoreObstacle", cheatState.buildIgnoreObstacle },
            { "buildIgnoreOtherGuild", cheatState.buildIgnoreOtherGuild },
            { "buildIgnoreSupport", cheatState.buildIgnoreSupport },
            { "buildIgnoreCeiling", cheatState.buildIgnoreCeiling },
            { "buildFastBuild", cheatState.buildFastBuild },
            { "buildIgnoreOverlap", cheatState.buildIgnoreOverlap },
            { "buildIgnoreGroundContact", cheatState.buildIgnoreGroundContact },
            { "buildIgnoreNearCamp", cheatState.buildIgnoreNearCamp },
            { "buildIgnoreUnderSea", cheatState.buildIgnoreUnderSea },
            { "buildIgnoreHighPlace", cheatState.buildIgnoreHighPlace },
            { "buildIgnoreSlope", cheatState.buildIgnoreSlope },
            { "buildIgnoreBaseRange", cheatState.buildIgnoreBaseRange },
            { "buildIgnoreIndoor", cheatState.buildIgnoreIndoor },
            { "buildIgnoreConnect", cheatState.buildIgnoreConnect },
            { "buildIgnoreWall", cheatState.buildIgnoreWall },
            { "buildNoDismantle", cheatState.buildNoDismantle },
            // Player Movement / Capture 开关
            { "infJump", cheatState.infJump },
            { "palCapture100", cheatState.palCapture100 },
            { "canCatchTowerBoss", cheatState.canCatchTowerBoss },
            { "mapFreeTeleport", cheatState.mapFreeTeleport },
            // MiniMap Radar
            { "minimapEnabled", cheatState.minimapEnabled },
            { "minimapRadius", cheatState.minimapRadius },
            { "minimapRange", cheatState.minimapRange },
            { "minimapRotationUp", cheatState.minimapRotationUp },
            { "minimapShowLabels", cheatState.minimapShowLabels },
            { "minimapShowWildPals", cheatState.minimapShowWildPals },
            { "minimapShowTamedPals", cheatState.minimapShowTamedPals },
            { "minimapShowPlayers", cheatState.minimapShowPlayers },
            { "minimapShowNPC", cheatState.minimapShowNPC },
            { "minimapShowOre", cheatState.minimapShowOre },
            { "minimapShowEggs", cheatState.minimapShowEggs },
            { "minimapShowTreasure", cheatState.minimapShowTreasure },
            { "minimapShowRelics", cheatState.minimapShowRelics },
            { "minimapShowFastTravel", cheatState.minimapShowFastTravel },
            { "minimapPosX", cheatState.minimapPosX },
            { "minimapPosY", cheatState.minimapPosY },
            // Pathfinding (寻路系统)
            { "pathfindingEnabled", cheatState.pathfindingEnabled },
            { "pathVisualizationEnabled", cheatState.pathVisualizationEnabled },
            { "pathAutoRecalc", cheatState.pathAutoRecalc },
            { "pathNavMeshOnly", cheatState.pathNavMeshOnly },
            { "pathColor", { cheatState.pathColor[0], cheatState.pathColor[1], cheatState.pathColor[2], cheatState.pathColor[3] } },
            { "pathStartColor", { cheatState.pathStartColor[0], cheatState.pathStartColor[1], cheatState.pathStartColor[2], cheatState.pathStartColor[3] } },
            { "pathEndColor", { cheatState.pathEndColor[0], cheatState.pathEndColor[1], cheatState.pathEndColor[2], cheatState.pathEndColor[3] } },
            { "pathPartialColor", { cheatState.pathPartialColor[0], cheatState.pathPartialColor[1], cheatState.pathPartialColor[2], cheatState.pathPartialColor[3] } },
            { "pathLineThickness", cheatState.pathLineThickness },
            { "pathWaypointSize", cheatState.pathWaypointSize },
            { "pathRecalcThreshold", cheatState.pathRecalcThreshold },
            { "pathMaxActive", cheatState.pathMaxActive },
            { "pathThrottleMs", cheatState.pathThrottleMs },
            { "bulkItems", std::move(bulkItems) }
        };
    }

    json SerializeHotkeys()
    {
        return {
            { "worldSpeed", key.hotkeyToggleWorldSpeed },
            { "stamina", key.hotkeyStamina },
            { "esp", key.hotkeyToggleESP },
            { "relic", key.hotkeyToggleRelic },
            { "attack", key.hotkeyToggleAttack },
            { "repairWeapon", key.hotkeyRepairWeapon },
            { "teleportHome", key.hotkeyTeleportHome },
            { "refreshWeight", key.hotkeyRefreshWeight },
            { "fastTravelMap", key.hotkeyFastTravelMap }
        };
    }

    void DeserializeCheatState(const json& state)
    {
#define READ_STATE(name) ReadValue(state, #name, cheatState.name)
        READ_STATE(worldSpeed); READ_STATE(weaponDamage); READ_STATE(attack); READ_STATE(weight);
        READ_STATE(infAmmo); READ_STATE(infMag); READ_STATE(infStamina);
        READ_STATE(cameraFov); READ_STATE(cameraBrightness); READ_STATE(isSilent);
        READ_STATE(aimbotEnabled); READ_STATE(aimbotShowFov); READ_STATE(aimbotDrawFOV);
        READ_STATE(aimbotFov); READ_STATE(aimbotSmooth); READ_STATE(aimbotHotkey);
        READ_STATE(aimbotVisibilityCheck); READ_STATE(espEnabled); READ_STATE(espBoxes);
        READ_STATE(espBoxes3D); READ_STATE(espSkeleton); READ_STATE(espThermal);
        READ_STATE(espShowNames); READ_STATE(espShowDistance); READ_STATE(espDistance);
        READ_STATE(espShowPalHealth); READ_STATE(espShowPals); READ_STATE(espShowRelics);
        READ_STATE(espShowWaypoints); READ_STATE(isFly); READ_STATE(flySpeed); READ_STATE(craftingSpeed);
        READ_STATE(isInvicity); READ_STATE(statusImmune);
        READ_STATE(invisible); READ_STATE(aimbotAimPart);
        READ_STATE(foodNeverSpoil); READ_STATE(timeFreeze); READ_STATE(killRange);
        READ_STATE(defence); READ_STATE(speedMultiplier); READ_STATE(playerLevel);
        READ_STATE(useEnglishNames);
        // 菜单显隐状态由 core::input 持有 (原 cheatState.showMenu)
        {
            bool showMenu = pal::core::input::IsMenuOpen();
            ReadValue(state, "showMenu", showMenu);
            pal::core::input::SetMenuOpen(showMenu);
        }
        // Build Unlock (建造/制作解锁) 开关：从配置读取，恢复 UI 选择
        READ_STATE(buildUnlockEnabled); READ_STATE(buildIgnoreRequirements);
        READ_STATE(buildNoConsumeMaterial);
        READ_STATE(buildIgnoreGroundPlacement); READ_STATE(buildAllowOverlapTerminal);
        READ_STATE(buildIgnoreNearBoss); READ_STATE(buildIgnoreOil);
        READ_STATE(buildIgnoreBaseLimit); READ_STATE(buildIgnoreCampLimit);
        READ_STATE(buildIgnoreObstacle); READ_STATE(buildIgnoreOtherGuild);
        READ_STATE(buildIgnoreSupport); READ_STATE(buildIgnoreCeiling);
        READ_STATE(buildFastBuild); READ_STATE(buildIgnoreOverlap);
        READ_STATE(buildIgnoreGroundContact); READ_STATE(buildIgnoreNearCamp);
        READ_STATE(buildIgnoreUnderSea); READ_STATE(buildIgnoreHighPlace);
        READ_STATE(buildIgnoreSlope); READ_STATE(buildIgnoreBaseRange);
        READ_STATE(buildIgnoreIndoor); READ_STATE(buildIgnoreConnect);
        READ_STATE(buildIgnoreWall); READ_STATE(buildNoDismantle);
        // Player Movement / Capture 开关
        READ_STATE(infJump); READ_STATE(palCapture100); READ_STATE(canCatchTowerBoss);
        READ_STATE(mapFreeTeleport);
        // MiniMap Radar
        READ_STATE(minimapEnabled); READ_STATE(minimapRadius); READ_STATE(minimapRange);
        READ_STATE(minimapRotationUp); READ_STATE(minimapShowLabels);
        READ_STATE(minimapShowWildPals); READ_STATE(minimapShowTamedPals);
        READ_STATE(minimapShowPlayers); READ_STATE(minimapShowNPC);
        READ_STATE(minimapShowOre); READ_STATE(minimapShowEggs);
        READ_STATE(minimapShowTreasure); READ_STATE(minimapShowRelics);
        READ_STATE(minimapShowFastTravel);
        READ_STATE(minimapPosX); READ_STATE(minimapPosY);
        // Pathfinding (寻路系统)
        READ_STATE(pathfindingEnabled); READ_STATE(pathVisualizationEnabled);
        READ_STATE(pathAutoRecalc); READ_STATE(pathNavMeshOnly);
        READ_STATE(pathLineThickness); READ_STATE(pathWaypointSize);
        READ_STATE(pathRecalcThreshold); READ_STATE(pathMaxActive);
        READ_STATE(pathThrottleMs);
#undef READ_STATE

        cheatState.bulkItems.clear();
        if (const auto items = state.find("bulkItems"); items != state.end() && items->is_array())
        {
            for (const auto& item : *items)
            {
                BulkItem value;
                ReadValue(item, "id", value.id);
                ReadValue(item, "count", value.count);
                value.count = std::clamp(value.count, 1, 9999);
                if (!value.id.empty())
                    cheatState.bulkItems.push_back(std::move(value));
            }
        }

        // 加载 ESP 颜色数组 (RGBA 0-1)
        auto LoadColor = [&](const char* key, float* out) {
            const auto it = state.find(key);
            if (it == state.end() || !it->is_array() || it->size() < 4) return;
            try {
                for (int k = 0; k < 4; ++k) out[k] = (*it)[k].get<float>();
            }
            catch (const json::exception&) { /* keep default */ }
        };
        LoadColor("espColorBox", cheatState.espColorBox);
        LoadColor("espColorBox3D", cheatState.espColorBox3D);
        LoadColor("espColorSkeleton", cheatState.espColorSkeleton);
        LoadColor("espColorName", cheatState.espColorName);
        // Pathfinding 颜色数组
        LoadColor("pathColor", cheatState.pathColor);
        LoadColor("pathStartColor", cheatState.pathStartColor);
        LoadColor("pathEndColor", cheatState.pathEndColor);
        LoadColor("pathPartialColor", cheatState.pathPartialColor);

        cheatState.aimbotAimPart = std::clamp(cheatState.aimbotAimPart, 0, 2);
        cheatState.worldSpeed = std::clamp(cheatState.worldSpeed, 1.0f, 20.0f);
        cheatState.cameraFov = std::clamp(cheatState.cameraFov, 25.0f, 170.0f);
        cheatState.cameraBrightness = std::clamp(cheatState.cameraBrightness, 0.0f, 5.0f);
        cheatState.aimbotFov = std::clamp(cheatState.aimbotFov, 1.0f, 280.0f);
        cheatState.aimbotSmooth = std::clamp(cheatState.aimbotSmooth, 0.0f, 1.0f);
        cheatState.espDistance = std::clamp(cheatState.espDistance, 200.0f, 40000.0f);
        cheatState.killRange = std::clamp(cheatState.killRange, 100.0f, 40000.0f);
        // MiniMap Radar clamps
        cheatState.minimapRadius = std::clamp(cheatState.minimapRadius, 40.0f, 400.0f);
        cheatState.minimapRange = std::clamp(cheatState.minimapRange, 1000.0f, 100000.0f);
        cheatState.minimapPosX = std::clamp(cheatState.minimapPosX, 0.0f, 1.0f);
        cheatState.minimapPosY = std::clamp(cheatState.minimapPosY, 0.0f, 1.0f);
        // Pathfinding clamps
        cheatState.pathLineThickness = std::clamp(cheatState.pathLineThickness, 1.0f, 12.0f);
        cheatState.pathWaypointSize = std::clamp(cheatState.pathWaypointSize, 0.0f, 20.0f);
        cheatState.pathRecalcThreshold = std::clamp(cheatState.pathRecalcThreshold, 1.0f, 100.0f);
        cheatState.pathMaxActive = std::clamp(cheatState.pathMaxActive, 1, 50);
        cheatState.pathThrottleMs = std::clamp(cheatState.pathThrottleMs, 10, 5000);
    }

    void DeserializeHotkeys(const json& hotkeys)
    {
        ReadValue(hotkeys, "worldSpeed", key.hotkeyToggleWorldSpeed);
        ReadValue(hotkeys, "stamina", key.hotkeyStamina);
        ReadValue(hotkeys, "esp", key.hotkeyToggleESP);
        ReadValue(hotkeys, "relic", key.hotkeyToggleRelic);
        ReadValue(hotkeys, "attack", key.hotkeyToggleAttack);
        ReadValue(hotkeys, "repairWeapon", key.hotkeyRepairWeapon);
        ReadValue(hotkeys, "teleportHome", key.hotkeyTeleportHome);
        ReadValue(hotkeys, "refreshWeight", key.hotkeyRefreshWeight);
        ReadValue(hotkeys, "fastTravelMap", key.hotkeyFastTravelMap);
    }

    bool LoadLegacyConfig(const fs::path& path)
    {
        std::ifstream file(path);
        if (!file)
            return false;

        cheatState.bulkItems.clear();
        std::string line;
        while (std::getline(file, line))
        {
            const size_t separator = line.find('=');
            if (separator == std::string::npos)
                continue;

            const std::string name = line.substr(0, separator);
            const std::string value = line.substr(separator + 1);
            try
            {
                if (name == "worldSpeed") cheatState.worldSpeed = std::stof(value);
                else if (name == "infAmmo") cheatState.infAmmo = std::stoi(value) != 0;
                else if (name == "infStamina") cheatState.infStamina = std::stoi(value) != 0;
                else if (name == "cameraFov") cheatState.cameraFov = std::stof(value);
                else if (name == "cameraBrightness") cheatState.cameraBrightness = std::stof(value);
                else if (name == "attack") cheatState.attack = std::stoi(value);
                else if (name == "weight") cheatState.weight = std::stof(value);
                else if (name == "aimbotEnabled") cheatState.aimbotEnabled = std::stoi(value) != 0;
                else if (name == "aimbotShowFov") cheatState.aimbotShowFov = std::stoi(value) != 0;
                else if (name == "aimbotDrawFOV") cheatState.aimbotDrawFOV = std::stoi(value) != 0;
                else if (name == "aimbotFov") cheatState.aimbotFov = std::stof(value);
                else if (name == "aimbotSmooth") cheatState.aimbotSmooth = std::stof(value);
                else if (name == "aimbotHotkey") cheatState.aimbotHotkey = std::stoi(value);
                else if (name == "espEnabled") cheatState.espEnabled = std::stoi(value) != 0;
                else if (name == "espBoxes") cheatState.espBoxes = std::stoi(value) != 0;
                else if (name == "espShowNames") cheatState.espShowNames = std::stoi(value) != 0;
                else if (name == "espShowDistance") cheatState.espShowDistance = std::stoi(value) != 0;
                else if (name == "espDistance") cheatState.espDistance = std::stof(value);
                else if (name == "espShowPalHealth") cheatState.espShowPalHealth = std::stoi(value) != 0;
                else if (name == "espShowPals") cheatState.espShowPals = std::stoi(value) != 0;
                else if (name == "espShowRelics") cheatState.espShowRelics = std::stoi(value) != 0;
                else if (name == "espShowWaypoints") cheatState.espShowWaypoints = std::stoi(value) != 0;
                else if (name == "showMenu") pal::core::input::SetMenuOpen(std::stoi(value) != 0);
                else if (name == "bulkItem")
                {
                    const size_t comma = value.find(',');
                    if (comma != std::string::npos)
                        cheatState.bulkItems.push_back({ value.substr(0, comma), (std::max)(1, std::stoi(value.substr(comma + 1))) });
                }
            }
            catch (const std::exception&)
            {
                pal::log::Log(pal::log::Level::Warn, "config", "Ignoring invalid legacy value for {}", name);
            }
        }
        return true;
    }
}

namespace Config {
void Save(const std::string& filename)
{
    std::error_code error;
    fs::create_directories(GetConfigDirectory(), error);

    const fs::path destination = GetJsonConfigPath(filename);
    const fs::path temporary = destination.wstring() + L".tmp";
    const json document = {
        { "version", ConfigVersion },
        { "cheats", SerializeCheatState() },
        { "hotkeys", SerializeHotkeys() }
    };

    try
    {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error("could not open temporary config file");
            output << document.dump(2);
            output.flush();
            if (!output)
                throw std::runtime_error("could not write temporary config file");
        }

        fs::remove(destination, error);
        error.clear();
        fs::rename(temporary, destination, error);
        if (error)
            throw fs::filesystem_error("could not replace config file", temporary, destination, error);
    }
    catch (const std::exception& exception)
    {
        fs::remove(temporary, error);
        pal::log::Log(pal::log::Level::Error, "config", "Save failed: {}", exception.what());
    }
}

void ApplyCheatState()
{
    SetInfiniteAmmo();
    ChangeWorldSpeed(cheatState.worldSpeed);
    SetPlayerAttackParam();
    SetCameraFov();
    SetCameraBrightness();

    if (cheatState.weight != 600.0f)
        SetPlayerInventoryWeight();
}

void Load(const std::string& filename)
{
    const fs::path jsonPath = GetJsonConfigPath(filename);

    try
    {
        std::ifstream input(jsonPath, std::ios::binary);
        if (input)
        {
            const json document = json::parse(input);
            if (const auto state = document.find("cheats"); state != document.end() && state->is_object())
                DeserializeCheatState(*state);
            if (const auto hotkeys = document.find("hotkeys"); hotkeys != document.end() && hotkeys->is_object())
                DeserializeHotkeys(*hotkeys);
        }
        else
        {
            const fs::path legacyPath = GetConfigDirectory() / L"config.txt";
            if (!LoadLegacyConfig(legacyPath))
                return;
            Save(filename);
        }

        ApplyCheatState();
    }
    catch (const std::exception& exception)
    {
        pal::log::Log(pal::log::Level::Error, "config", "Load failed: {}", exception.what());
    }
}

} // namespace Config
