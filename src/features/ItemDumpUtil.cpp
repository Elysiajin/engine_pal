#include <pch.h>
#include "core/Log.hpp"
#include "ItemDumpUtil.hpp"
#include "PalEditor.hpp"
#include "engine/GameHelper.hpp"
#include <fstream>
#include <sstream>
#include <Windows.h>

using namespace SDK;

static FILE* g_ConsoleFile = nullptr;
static bool g_ConsoleCreated = false;

static void CreateSimpleConsole()
{
    if (g_ConsoleCreated) return;
    
    AllocConsole();
    freopen_s(&g_ConsoleFile, "CONOUT$", "w", stdout);
    freopen_s(&g_ConsoleFile, "CONOUT$", "w", stderr);
    
    SetConsoleTitleA("Item Dump Console");
    g_ConsoleCreated = true;
}

static std::string FNameToString(const FName& name)
{
    static UKismetStringLibrary* lib = nullptr;
    if (!lib) 
    {
        UClass* cls = UKismetStringLibrary::StaticClass();
        if (cls && cls->DefaultObject)
            lib = (UKismetStringLibrary*)cls->DefaultObject;
    }
    if (!lib) return "";
    
    FString fs = lib->Conv_NameToString(name);
    return fs.ToString();
}

// Get StaticItemDataAsset via UPalGameInstance->ItemIDManager
static UPalStaticItemDataAsset* GetStaticItemDataAsset()
{
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 1: Trying UPalItemIDManager::GetDefaultObj()...\n");
    
    // Method 1: Direct default object
    auto* itemManager1 = UPalItemIDManager::GetDefaultObj();
    if (itemManager1 && itemManager1->StaticItemDataAsset)
    {
        pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 1 SUCCESS!\n");
        return itemManager1->StaticItemDataAsset;
    }
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 1 failed (StaticItemDataAsset is null)\n");

    // Method 2: Via UPalStaticItemDataManager
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 2: Trying UPalStaticItemDataManager::GetDefaultObj()...\n");
    auto* staticManager = UPalStaticItemDataManager::GetDefaultObj();
    if (staticManager && staticManager->StaticItemDataAsset)
    {
        pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 2 SUCCESS!\n");
        return staticManager->StaticItemDataAsset;
    }
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 2 failed\n");

    // Method 3: Via UGameInstance
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 3: Trying UWorld::GetWorld()...\n");
    UWorld* world = UWorld::GetWorld();
    if (world && world->OwningGameInstance)
    {
        auto* gameInstance = static_cast<UPalGameInstance*>(world->OwningGameInstance);
        if (gameInstance && gameInstance->ItemIDManager)
        {
            pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 3 SUCCESS! Found ItemIDManager in GameInstance\n");
            if (gameInstance->ItemIDManager->StaticItemDataAsset)
            {
                return gameInstance->ItemIDManager->StaticItemDataAsset;
            }
        }
    }
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 3 failed\n");

    // Method 4: Scan GObjects for PalItemIDManager
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 4: Scanning GObjects for PalItemIDManager...\n");
    if (UObject::GObjects)
    {
        int maxProcess = UObject::GObjects->Num();
        pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] GObjects count: {}\n", maxProcess);
        
        for (int idx = 0; idx < maxProcess && idx < 100000; idx++)
        {
            UObject* obj = UObject::GObjects->GetByIndex(idx);
            if (!obj || !obj->Class) continue;
            
            std::string className = obj->Class->GetName();
            if (className == "PalItemIDManager")
            {
                pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Found PalItemIDManager at index {}\n", idx);
                auto* manager = static_cast<UPalItemIDManager*>(obj);
                if (manager && manager->StaticItemDataAsset)
                {
                    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 4 SUCCESS!\n");
                    return manager->StaticItemDataAsset;
                }
            }
            else if (className == "PalStaticItemDataAsset")
            {
                pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Found PalStaticItemDataAsset directly at index {}\n", idx);
                return static_cast<UPalStaticItemDataAsset*>(obj);
            }
        }
    }
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Method 4 failed - no objects found\n");

    return nullptr;
}

void ItemDumpUtil::DumpAllItemsToConsole()
{
    CreateSimpleConsole();
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Starting dump...\n");

    auto* asset = GetStaticItemDataAsset();
    if (!asset)
    {
        pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] ERROR: Could not find StaticItemDataAsset!\n");
        pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] All methods failed. The game might not have loaded item data yet.\n");
        return;
    }

    int count = asset->StaticItemDataMap.Num();
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Found StaticItemDataAsset with {} items!\n", count);

    int total = 0;
    for (auto& pair : asset->StaticItemDataMap)
    {
        std::string id = FNameToString(pair.First);
        auto* itemData = pair.Second;
        
        if (itemData)
        {
            pal::log::Log(pal::log::Level::Debug, "dump", "[Item] {}\n", id.c_str());
            total++;
        }
    }

    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Complete! Listed {} items.\n", total);
}

std::string ItemDumpUtil::DumpAllItemsToFile(const std::string& filepath)
{
    CreateSimpleConsole();
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Starting file dump to: {}\n", filepath.c_str());

    auto* asset = GetStaticItemDataAsset();
    if (!asset)
    {
        pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] ERROR: Could not find StaticItemDataAsset!\n");
        return "";
    }

    int count = asset->StaticItemDataMap.Num();
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Found StaticItemDataAsset with {} items!\n", count);

    std::vector<DumpedItem> items;
    
    for (auto& pair : asset->StaticItemDataMap)
    {
        std::string id = FNameToString(pair.First);
        auto* itemData = pair.Second;
        
        if (itemData)
        {
            DumpedItem item;
            item.id = id;
            item.base_name = FNameToString(itemData->ItemBaseName);
            item.type_a = (uint8_t)itemData->TypeA;
            item.type_b = (uint8_t)itemData->TypeB;
            item.rank = itemData->Rank;
            item.rarity = itemData->Rarity;
            item.display_name = itemData->GetName();
            items.push_back(item);
        }
    }

    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] Writing {} items to file...\n", (int)items.size());

    std::ofstream ofs(filepath);
    if (!ofs.is_open())
    {
        pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] ERROR: Failed to open file!\n");
        return "";
    }

    ofs << "id,base_name,display_name,type_a,type_b,rank,rarity\n";
    
    for (const auto& item : items)
    {
        ofs << item.id << "," << item.base_name << "," << item.display_name << ","
            << (int)item.type_a << "," << (int)item.type_b << "," << item.rank << "," << item.rarity << "\n";
    }

    ofs.close();
    
    pal::log::Log(pal::log::Level::Debug, "dump", "[ItemDump] SUCCESS! Wrote {} items to: {}\n", (int)items.size(), filepath.c_str());
    return filepath;
}

std::vector<DumpedItem> ItemDumpUtil::LoadDumpedItems(const std::string& filepath)
{
    std::vector<DumpedItem> items;
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) return items;

    std::string line;
    std::getline(ifs, line);

    while (std::getline(ifs, line))
    {
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        std::string token;
        DumpedItem item;
        
        std::getline(iss, item.id, ',');
        std::getline(iss, item.base_name, ',');
        std::getline(iss, item.display_name, ',');
        std::getline(iss, token, ','); item.type_a = (uint8_t)std::stoi(token);
        std::getline(iss, token, ','); item.type_b = (uint8_t)std::stoi(token);
        std::getline(iss, token, ','); item.rank = std::stoi(token);
        std::getline(iss, token); item.rarity = std::stoi(token);
        
        items.push_back(item);
    }

    return items;
}

std::vector<std::string> ItemDumpUtil::GenerateItemListFormat(const std::string& filepath)
{
    std::vector<DumpedItem> items = LoadDumpedItems(filepath);
    std::vector<std::string> result;
    
    for (const auto& item : items)
    {
        std::string displayName = !item.display_name.empty() ? item.display_name : item.base_name;
        result.push_back(item.id + "|" + displayName);
    }
    
    return result;
}

bool ItemDumpUtil::ItemExistsInGame(const std::string& item_id)
{
    return true;
}