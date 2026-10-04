#include <pch.h>
#include "engine/NameMapper.hpp"
#include "features/Database.hpp"
#include "core/Log.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <Windows.h>

// 无控制台环境下的文件日志：写到与映射文件同目录的 name_mapper_debug.log，
// 方便确认加载失败/解析跳过原因。
static void NameMapperLog(const std::string& dir, const std::string& msg)
{
    try
    {
        std::string path = dir;
        if (!path.empty() && path.back() != '\\' && path.back() != '/')
            path += "\\";
        path += "name_mapper_debug.log";
        std::ofstream ofs(path, std::ios::app);
        if (ofs)
            ofs << msg << "\n";
    }
    catch (...) {}
}

NameMapper& NameMapper::Get()
{
    static NameMapper instance;
    return instance;
}

// 将字符串转为小写（用于大小写不敏感比较）
static std::string ToLower(const std::string& s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

// 判断 ID 是否为合法的游戏标识符：
//   只允许 [A-Za-z0-9_]，且必须以字母开头。
// 用户提供的映射文件并非标准导出——中间可能夹杂任意文字行
// （例如随手写了"帕鲁"两个字），这类行的第一列不可能通过校验，
// 直接跳过，防止把垃圾数据灌进映射表。
static bool IsValidIdFormat(const std::string& id)
{
    if (id.empty() || id.size() > 128)
        return false;
    // 必须以字母开头（游戏 ID 均如此：Item_Wood / Accessory_AT_1 / Lamball）
    if (!std::isalpha(static_cast<unsigned char>(id[0])))
        return false;
    for (char c : id)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    }
    return true;
}

// 中文名是否有效：至少包含一个非 ASCII 字节（UTF-8 中文占 >=3 字节）
// 或为已知合法的英文占位。纯 ASCII 的"中文名"说明该行是坏行。
static bool IsValidChineseName(const std::string& name)
{
    if (name.empty() || name.size() > 128)
        return false;
    const std::string lower = ToLower(name);
    // 导出工具未翻译的占位符
    if (lower == "zh-hans text" || lower == "zh_hans_text" ||
        lower == "text" || lower == "none")
        return false;
    if (name.rfind("ITEM_DESC_", 0) == 0 || name.rfind("ITEM_NAME_", 0) == 0)
        return false;
    // 至少含一个多字节字符才视为真正的中文翻译
    for (unsigned char c : name)
    {
        if (c >= 0x80)
            return true;
    }
    return false;
}

// ============================================================================
// 解析映射文件的核心逻辑（file 已打开，UTF-8 内容）。
// dir 为日志目录；displayName 仅用于日志展示。
// ============================================================================
bool NameMapper::ParseFromFile(std::ifstream& file, const std::string& dir, const std::string& displayName)
{
    // 跳过 UTF-8 BOM (0xEF 0xBB 0xBF)
    char bom[3] = {};
    file.read(bom, 3);
    if (!(static_cast<unsigned char>(bom[0]) == 0xEF &&
          static_cast<unsigned char>(bom[1]) == 0xBB &&
          static_cast<unsigned char>(bom[2]) == 0xBF))
    {
        file.clear();
        file.seekg(0, std::ios::beg);
    }

    int itemCount = 0, palCount = 0, skipped = 0, lineNo = 0;
    int skipNoTab = 0, skipBadId = 0, skipBadName = 0;

    // 帕鲁名小写 -> 标准大小写（来自 database.h 的 palNames，161 个）
    std::unordered_map<std::string, std::string> palLowerMap;
    for (const auto& pal : database::palNames)
        palLowerMap[ToLower(pal)] = pal;

    // 记录若干坏行样例（方便用户自查映射文件里乱写的那一行）
    std::string sampleNoTab, sampleBadId, sampleBadName;

    std::string line;
    while (std::getline(file, line))
    {
        ++lineNo;

        // 去掉行尾 \r（Windows 换行）
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty())
            continue;

        // 严格按第一个 Tab 分割出 ID；没有 Tab 的行是用户混入的杂乱文本，
        // 不可能构成有效映射，直接跳过。
        const size_t tab = line.find('\t');
        if (tab == std::string::npos)
        {
            ++skipped; ++skipNoTab;
            if (sampleNoTab.empty() && line.size() < 120)
                sampleNoTab = line;
            continue;
        }

        std::string id = line.substr(0, tab);
        std::string rest = line.substr(tab + 1);

        // 第二列 = 中文名（到下一个 Tab 为止）；描述列忽略
        const size_t tab2 = rest.find('\t');
        std::string chineseName = (tab2 == std::string::npos) ? rest : rest.substr(0, tab2);

        // trim 首尾空白
        auto trim = [](std::string& s) {
            auto isSp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r'; };
            size_t b = 0, e = s.size();
            while (b < e && isSp(s[b])) ++b;
            while (e > b && isSp(s[e - 1])) --e;
            s = s.substr(b, e - b);
        };
        trim(id);
        trim(chineseName);

        // 三重校验：ID 格式 + 中文名有效性 + 长度
        bool idOk = IsValidIdFormat(id);
        bool cnOk = IsValidChineseName(chineseName);
        if (!idOk || !cnOk)
        {
            ++skipped;
            if (!idOk) { ++skipBadId; if (sampleBadId.empty() && id.size() < 120) sampleBadId = id; }
            if (!cnOk) { ++skipBadName; if (sampleBadName.empty() && chineseName.size() < 120) sampleBadName = chineseName; }
            continue;
        }

        // 归类：先按 database.h 帕鲁表精确归类（大小写不敏感），
        // 否则一律当物品存（原文 key + 小写 key 双写以便回退查询）
        std::string lowerId = ToLower(id);
        auto it = palLowerMap.find(lowerId);
        if (it != palLowerMap.end())
        {
            m_palMap[it->second] = chineseName;   // 标准大小写作 key
            ++palCount;
        }
        else
        {
            if (m_itemMap.emplace(id, chineseName).second)
                ++itemCount;
            m_itemLowerMap[lowerId] = chineseName; // 小写索引
        }
    }

    m_loaded = (itemCount + palCount) > 0;
    pal::log::Log(pal::log::Level::Info, "namemapper",
             "Loaded from {}: {} items + {} pals = {} entries, {} lines skipped",
             displayName, itemCount, palCount, itemCount + palCount, skipped);

    // 写文件日志（无控制台环境可查）
    {
        std::string msg = "[NameMapper] loaded=" + std::string(m_loaded ? "true" : "false") +
            " items=" + std::to_string(itemCount) +
            " pals=" + std::to_string(palCount) +
            " skipped=" + std::to_string(skipped) +
            " (noTab=" + std::to_string(skipNoTab) +
            ", badId=" + std::to_string(skipBadId) +
            ", badName=" + std::to_string(skipBadName) + ")";
        if (!sampleNoTab.empty()) msg += "\n  sampleNoTab : " + sampleNoTab;
        if (!sampleBadId.empty()) msg += "\n  sampleBadId : " + sampleBadId;
        if (!sampleBadName.empty()) msg += "\n  sampleBadName : " + sampleBadName;
        NameMapperLog(dir, msg);
    }
    return m_loaded;
}

bool NameMapper::LoadFromFile(const std::string& filename)
{
    m_itemMap.clear();
    m_palMap.clear();
    m_itemLowerMap.clear();
    m_lowerIndexDirty = true;
    m_loaded = false;

    // 推导映射文件所在目录（用于写调试日志）
    std::string dir;
    {
        size_t slash = filename.find_last_of("\\/");
        dir = (slash != std::string::npos) ? filename.substr(0, slash + 1) : "";
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file)
    {
        pal::log::Log(pal::log::Level::Warn, "namemapper", "Failed to open: {}", filename);
        NameMapperLog(dir, "[ERROR] Failed to open: " + filename);
        return false;
    }

    return ParseFromFile(file, dir, filename);
}

// 宽字符路径版本：修复 UC-2 中文路径 "名称映射.txt" 在 GetModuleFileNameA
// (ANSI/GBK) 与 UTF-8 字面量混拼下打不开的问题。
bool NameMapper::LoadFromFileW(const std::wstring& filename)
{
    m_itemMap.clear();
    m_palMap.clear();
    m_itemLowerMap.clear();
    m_lowerIndexDirty = true;
    m_loaded = false;

    // 推导目录（宽字符）
    std::wstring wdir;
    {
        size_t slash = filename.find_last_of(L"\\/");
        wdir = (slash != std::wstring::npos) ? filename.substr(0, slash + 1) : L"";
    }
    // 目录转 UTF-8 供日志使用
    std::string dir;
    if (!wdir.empty())
    {
        const int need = WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (need > 0)
        {
            dir.resize(need - 1);
            WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), -1, &dir[0], need, nullptr, nullptr);
        }
    }

    std::ifstream file(filename, std::ios::binary);   // MSVC 支持 wchar_t* 路径
    if (!file)
    {
        pal::log::Log(pal::log::Level::Warn, "namemapper", "Failed to open (wide path)");
        NameMapperLog(dir, "[ERROR] Failed to open wide path.");
        return false;
    }

    return ParseFromFile(file, dir, "名称映射.txt(wide)");
}

bool NameMapper::GetItemChineseName(const std::string& itemId, std::string& out) const
{
    if (!m_loaded || itemId.empty())
        return false;

    // 1) 原文精确匹配
    const auto it = m_itemMap.find(itemId);
    if (it != m_itemMap.end())
    {
        out = it->second;
        return true;
    }

    // 2) 小写不敏感匹配（物品 ID 大小写在不同来源中不一致很常见）
    const auto lit = m_itemLowerMap.find(ToLower(itemId));
    if (lit != m_itemLowerMap.end())
    {
        out = lit->second;
        return true;
    }

    // 3) 前缀剥离匹配：调用方可能传 "Item_Wood" 而表中是 "Wood"，
    //    也可能反过来。逐个尝试去掉常见前缀再查。
    static const char* kPrefixes[] = { "Item_", "item_" };
    for (const char* p : kPrefixes)
    {
        const size_t plen = strlen(p);
        if (itemId.size() > plen && itemId.compare(0, plen, p) == 0)
        {
            const std::string stripped = itemId.substr(plen);
            const auto it2 = m_itemMap.find(stripped);
            if (it2 != m_itemMap.end()) { out = it2->second; return true; }
            const auto lit2 = m_itemLowerMap.find(ToLower(stripped));
            if (lit2 != m_itemLowerMap.end()) { out = lit2->second; return true; }
        }
    }

    // 反向：表里带 Item_ 前缀而传入不带
    {
        const std::string prefixed = "Item_" + itemId;
        const auto it3 = m_itemMap.find(prefixed);
        if (it3 != m_itemMap.end()) { out = it3->second; return true; }
        const auto lit3 = m_itemLowerMap.find(ToLower(prefixed));
        if (lit3 != m_itemLowerMap.end()) { out = lit3->second; return true; }
    }

    return false;
}

bool NameMapper::GetPalChineseName(const std::string& palName, std::string& out) const
{
    if (!m_loaded || palName.empty())
        return false;

    // 1) 精确匹配
    const auto it = m_palMap.find(palName);
    if (it != m_palMap.end())
    {
        out = it->second;
        return true;
    }

    // 2) 大小写不敏感（预建的小写索引，避免每次线性扫描）
    static thread_local std::unordered_map<std::string, std::string> lowerIndex;
    if (m_lowerIndexDirty)
    {
        lowerIndex.clear();
        for (const auto& [k, v] : m_palMap)
            lowerIndex[ToLower(k)] = v;
        m_lowerIndexDirty = false;
    }
    const auto lit = lowerIndex.find(ToLower(palName));
    if (lit != lowerIndex.end())
    {
        out = lit->second;
        return true;
    }

    // 3) 剥离 Pal_ 前缀后重试（如传入 "Pal_Lamball" 而表中是 "Lamball"）
    if (palName.rfind("Pal_", 0) == 0 || palName.rfind("PAL_", 0) == 0)
    {
        if (GetPalChineseName(palName.substr(4), out))
            return true;
    }

    return false;
}

bool NameMapper::GetChineseName(const std::string& id, std::string& out) const
{
    return GetItemChineseName(id, out) || GetPalChineseName(id, out);
}
