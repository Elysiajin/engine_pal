#include "pch.h"
#include "DynamicItemList.hpp"
#include "ItemDumpUtil.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

// JSON library (assuming nlohmann/json is available)
#ifdef USE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#else
// Simple JSON parser fallback
#include <fstream>
#endif

namespace itemlist
{

DynamicItemList& DynamicItemList::Get()
{
    static DynamicItemList instance;
    return instance;
}

void DynamicItemList::ClassifyItem(ItemInfo& item)
{
    // Auto-classify items based on ID patterns
    const std::string& id = item.id;
    
    if (id.find("Accessory_") == 0 || id.find("Otomo_") == 0 || id.find("AdditionalInventory_") == 0)
        item.category = "Accessories";
    else if (id.find("Armor") != std::string::npos || id.find("Cloth") != std::string::npos || 
             id.find("SFArmor") != std::string::npos || id.find("YakushimaArmor") != std::string::npos)
        item.category = "Armor";
    else if (id.find("Head") == 0 || id.find("HeadEquip") == 0 || id.find("Helmet") != std::string::npos ||
             id.find("YakushimaHeadEquip") != std::string::npos || id.find("SFHelmet") != std::string::npos)
        item.category = "Hats";
    else if (id.find("Bullet") != std::string::npos || id.find("Arrow") != std::string::npos)
        item.category = "Ammo";
    else if (id.find("Blueprint_") == 0)
        item.category = "Blueprints";
    else if (id.find("PalEgg") == 0)
        item.category = "Eggs";
    else if (id.find("Seeds") != std::string::npos)
        item.category = "Seeds";
    else if (id.find("Sphere") != std::string::npos || id.find("PalSphere") != std::string::npos)
        item.category = "Pal Spheres";
    else if (id.find("Meat") != std::string::npos || id.find("Food") != std::string::npos ||
             id.find("Berry") != std::string::npos || id.find("Mushroom") != std::string::npos ||
             id.find("Milk") != std::string::npos || id.find("Cheese") != std::string::npos ||
             id.find("Baked") != std::string::npos || id.find("Grilled") != std::string::npos ||
             id == "Cake" || id.find("Cake") == 0 || id.find("Bread") != std::string::npos ||
             id.find("Hamburger") != std::string::npos || id.find("Pizza") != std::string::npos ||
             id.find("Potion_Extreme") != std::string::npos)
        item.category = "Food";
    else if (id.find("Medicine") != std::string::npos || id.find("Potion") != std::string::npos ||
             id.find("Drug") != std::string::npos || id.find("Revive") != std::string::npos ||
             id.find("Reset") != std::string::npos)
        item.category = "Medicine";
    else if (id.find("Weapon") != std::string::npos || id.find("Gun") != std::string::npos ||
             id.find("Rifle") != std::string::npos || id.find("Bow") != std::string::npos ||
             id.find("Sword") != std::string::npos || id.find("Spear") != std::string::npos ||
             id.find("Grenade") != std::string::npos || id.find("Launcher") != std::string::npos ||
             id.find("Shotgun") != std::string::npos || id.find("Katana") != std::string::npos ||
             id.find("YakushimaBlade") != std::string::npos || id.find("YakushimaGun") != std::string::npos ||
             id.find("Bat") == 0 || id.find("BeamSword") != std::string::npos)
        item.category = "Weapons";
    else if (id.find("Glider") != std::string::npos || id.find("Pickaxe") != std::string::npos ||
             id.find("Axe") != std::string::npos || id.find("Shield") != std::string::npos ||
             id.find("Lantern") != std::string::npos || id.find("Torch") != std::string::npos ||
             id.find("Key") != std::string::npos || id.find("Grappling") != std::string::npos ||
             id.find("MetalDetector") != std::string::npos)
        item.category = "Tools";
    else if (id == "Money" || id == "Diamond" || id == "Ruby" || id == "Sapphire" ||
             id == "Emerald" || id.find("Coin") != std::string::npos)
        item.category = "Money";
    else if (id.find("Crystal") != std::string::npos || id.find("Ore") != std::string::npos ||
             id.find("Ingot") != std::string::npos || id.find("Organ") != std::string::npos ||
             id.find("Leather") != std::string::npos || id.find("Cloth") != std::string::npos ||
             id.find("Fiber") != std::string::npos || id.find("Wood") != std::string::npos ||
             id.find("Stone") != std::string::npos || id.find("Bone") != std::string::npos ||
             id.find("Horn") != std::string::npos || id.find("Venom") != std::string::npos ||
             id.find("Polymer") != std::string::npos || id.find("Carbon") != std::string::npos ||
             id.find("Pal_crystal") != std::string::npos || id.find("PalOil") != std::string::npos ||
             id.find("PalFluid") != std::string::npos || id.find("_Ancient") != std::string::npos)
        item.category = "Crafting Materials";
    else if (id.find("SkillUnlock_") == 0 || id.find("BossDefeatReward_") == 0 ||
             id.find("PalPassiveSkillChange_") == 0 || id.find("QuestItem_") == 0 ||
             id.find("PalSummon_") == 0 || id.find("TechnologyBook") != std::string::npos ||
             id.find("BattleTicket") != std::string::npos)
        item.category = "Other";
    else
        item.category = "Other";
}

bool DynamicItemList::RefreshFromGame()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Get items from game
    std::vector<DumpedItem> dumpedItems = ItemDumpUtil::LoadDumpedItems("Items.csv");
    if (dumpedItems.empty())
    {
        // Try to dump from game first
        std::string path = ItemDumpUtil::DumpAllItemsToFile("Items.csv");
        if (path.empty()) return false;
        dumpedItems = ItemDumpUtil::LoadDumpedItems(path);
        if (dumpedItems.empty()) return false;
    }
    
    m_items.clear();
    m_categorizedItems.clear();
    
    for (const auto& dumped : dumpedItems)
    {
        ItemInfo info;
        info.id = dumped.id;
        info.displayName = dumped.display_name.empty() ? dumped.base_name : dumped.display_name;
        info.typeA = dumped.type_a;
        info.typeB = dumped.type_b;
        info.rank = dumped.rank;
        info.rarity = dumped.rarity;
        
        ClassifyItem(info);
        
        m_items.push_back(info);
        m_categorizedItems[info.category].push_back(info);
    }
    
    m_loaded = true;
    return true;
}

bool DynamicItemList::LoadFromJson(const std::string& filepath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) return false;
    
    // Simple JSON parsing (line by line)
    std::string line;
    std::vector<ItemInfo> items;
    
    // Check if it's a simple line-by-line format
    std::getline(ifs, line);
    if (line.find("{") != std::string::npos)
    {
        // JSON array format - use simple parsing
        ifs.seekg(0);
        
        while (std::getline(ifs, line))
        {
            // Look for "id": pattern
            size_t idPos = line.find("\"id\"");
            if (idPos == std::string::npos) continue;
            
            ItemInfo info;
            // Extract id value
            size_t idStart = line.find(":", idPos);
            if (idStart != std::string::npos)
            {
                size_t quote1 = line.find("\"", idStart);
                size_t quote2 = line.find("\"", quote1 + 1);
                if (quote1 != std::string::npos && quote2 != std::string::npos)
                    info.id = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
            
            // Extract display_name
            size_t namePos = line.find("\"display_name\"");
            if (namePos != std::string::npos)
            {
                size_t nameStart = line.find(":", namePos);
                if (nameStart != std::string::npos)
                {
                    size_t quote1 = line.find("\"", nameStart);
                    size_t quote2 = line.find("\"", quote1 + 1);
                    if (quote1 != std::string::npos && quote2 != std::string::npos)
                        info.displayName = line.substr(quote1 + 1, quote2 - quote1 - 1);
                }
            }
            
            if (!info.id.empty())
            {
                ClassifyItem(info);
                items.push_back(info);
            }
        }
    }
    else
    {
        // CSV fallback
        ifs.seekg(0);
        std::getline(ifs, line); // skip header
        
        while (std::getline(ifs, line))
        {
            std::istringstream iss(line);
            std::string token;
            ItemInfo info;
            
            std::getline(iss, info.id, ',');
            std::getline(iss, info.displayName, ',');
            if (info.displayName.empty()) std::getline(iss, info.displayName, ',');
            
            if (!info.id.empty())
            {
                ClassifyItem(info);
                items.push_back(info);
            }
        }
    }
    
    if (items.empty()) return false;
    
    m_items = std::move(items);
    m_categorizedItems.clear();
    for (const auto& item : m_items)
    {
        m_categorizedItems[item.category].push_back(item);
    }
    
    m_loaded = true;
    return true;
}

bool DynamicItemList::SaveToJson(const std::string& filepath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::ofstream ofs(filepath);
    if (!ofs.is_open()) return false;
    
    ofs << "{\n  \"items\": [\n";
    
    for (size_t i = 0; i < m_items.size(); i++)
    {
        const auto& item = m_items[i];
        ofs << "    {\"id\": \"" << item.id << "\", \"display_name\": \"" << item.displayName 
            << "\", \"category\": \"" << item.category << "\"}";
        if (i < m_items.size() - 1) ofs << ",";
        ofs << "\n";
    }
    
    ofs << "  ]\n}\n";
    ofs.close();
    
    return true;
}

std::vector<std::string> DynamicItemList::GetAllItems() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::string> result;
    result.reserve(m_items.size());
    
    for (const auto& item : m_items)
    {
        // Format: "ID | DisplayName" for easy identification
        if (item.displayName.empty() || item.displayName == item.id)
            result.push_back(item.id);
        else
            result.push_back(item.id + " | " + item.displayName);
    }
    
    return result;
}

std::vector<std::string> DynamicItemList::GetItemsByCategory(const std::string& category) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::string> result;
    
    auto it = m_categorizedItems.find(category);
    if (it != m_categorizedItems.end())
    {
        for (const auto& item : it->second)
        {
            // Format: "ID | DisplayName"
            if (item.displayName.empty() || item.displayName == item.id)
                result.push_back(item.id);
            else
                result.push_back(item.id + " | " + item.displayName);
        }
    }
    
    return result;
}

std::vector<std::string> DynamicItemList::GetCategories() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::string> result;
    for (const auto& pair : m_categorizedItems)
    {
        result.push_back(pair.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

size_t DynamicItemList::GetItemCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_items.size();
}

} // namespace itemlist