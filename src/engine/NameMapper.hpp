#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>

// 中文名称映射器
// 从 名称映射.txt 加载物品/帕鲁的中文名称映射，提供查询接口。
// 映射文件格式（UTF-8）：ID\t中文名\t描述
class NameMapper
{
public:
    static NameMapper& Get();

    // 加载映射文件（path 为 DLL 目录下的相对路径，如 "名称映射.txt"，UTF-8 编码）
    bool LoadFromFile(const std::string& filename);
    // 加载映射文件（宽字符路径重载，避免 ANSI/UTF-8 中文路径乱码打不开）
    bool LoadFromFileW(const std::wstring& filename);

    // 查询物品中文名（按 ID 精确匹配）
    // 返回 true 表示找到，out 中为中文名；false 表示未找到。
    bool GetItemChineseName(const std::string& itemId, std::string& out) const;

    // 查询帕鲁中文名（按帕鲁角色名精确匹配）
    // 返回 true 表示找到，out 中为中文名；false 表示未找到。
    bool GetPalChineseName(const std::string& palName, std::string& out) const;

    // 查询任意 ID 的中文名（先查物品，再查帕鲁）
    bool GetChineseName(const std::string& id, std::string& out) const;

    // 是否已加载
    bool IsLoaded() const { return m_loaded; }

    // 获取加载条目数
    size_t GetCount() const { return m_itemMap.size() + m_palMap.size(); }

private:
    NameMapper() = default;
    ~NameMapper() = default;

    // 解析已打开的 UTF-8 映射流并填充本对象。
    // filename 仅用于日志显示；dir 为日志目录。
    bool ParseFromFile(std::ifstream& file, const std::string& dir, const std::string& displayName);

    std::unordered_map<std::string, std::string> m_itemMap;       // 物品 ID 原文 -> 中文名
    std::unordered_map<std::string, std::string> m_itemLowerMap;  // 物品 ID 小写 -> 中文名（大小写回退）
    std::unordered_map<std::string, std::string> m_palMap;        // 标准帕鲁名 -> 中文名
    mutable bool m_lowerIndexDirty = true;                        // palMap 小写索引脏标记
    bool m_loaded = false;
};