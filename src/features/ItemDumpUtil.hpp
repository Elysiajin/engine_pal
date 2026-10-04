#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>

// Forward declarations from SDK
namespace SDK
{
    class UPalStaticItemDataBase;
    class UPalStaticItemDataAsset;
    class UPalStaticItemDataManager;
}

struct DumpedItem
{
    std::string id;           // FName (e.g. "Accessory_AT_1")
    std::string base_name;    // ItemBaseName
    std::string display_name; // from OverrideNameMsgID or base_name
    uint8_t type_a;           // EPalItemTypeA
    uint8_t type_b;           // EPalItemTypeB
    int32_t rank;
    int32_t rarity;
};

class ItemDumpUtil
{
public:
    // Dump all items from the game's StaticItemDataMap to console
    static void DumpAllItemsToConsole();

    // Dump all items to a file, returns path of dumped file
    static std::string DumpAllItemsToFile(const std::string& filepath);

    // Load dumped data and return as vector
    static std::vector<DumpedItem> LoadDumpedItems(const std::string& filepath);

    // Generate ItemList.hpp compatible format: "ID|DisplayName"
    static std::vector<std::string> GenerateItemListFormat(const std::string& filepath);

    // Check if an item ID exists in the game's data
    static bool ItemExistsInGame(const std::string& item_id);

private:
    // Get item display name - will be resolved via game's localization system
    static std::string ResolveDisplayName(SDK::UPalStaticItemDataBase* itemData);

    // Get type A name string
    static std::string GetTypeAName(uint8_t typeA);

    // Get type B name string
    static std::string GetTypeBName(uint8_t typeB);
};
