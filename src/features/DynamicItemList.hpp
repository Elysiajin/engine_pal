#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace itemlist
{
    // Dynamic item list manager
    class DynamicItemList
    {
    public:
        struct ItemInfo
        {
            std::string id;
            std::string displayName;
            std::string category;
            int typeA = 0;
            int typeB = 0;
            int rank = 0;
            int rarity = 0;
        };

        static DynamicItemList& Get();
        
        // Refresh from game (loads all items from game memory)
        bool RefreshFromGame();
        
        // Load from external JSON file
        bool LoadFromJson(const std::string& filepath);
        
        // Save to external JSON file
        bool SaveToJson(const std::string& filepath);
        
        // Get all items as "ID|DisplayName" format
        std::vector<std::string> GetAllItems() const;
        
        // Get items by category
        std::vector<std::string> GetItemsByCategory(const std::string& category) const;
        
        // Get all categories
        std::vector<std::string> GetCategories() const;
        
        // Check if data is loaded
        bool IsLoaded() const { return m_loaded; }
        
        // Get item count
        size_t GetItemCount() const;

    private:
        DynamicItemList() = default;
        ~DynamicItemList() = default;
        
        void ClassifyItem(ItemInfo& item);
        
        std::vector<ItemInfo> m_items;
        std::map<std::string, std::vector<ItemInfo>> m_categorizedItems;
        bool m_loaded = false;
        mutable std::mutex m_mutex;
    };
}