// ============================================================================
// CoordinatesLoader.cpp
// 传送器坐标数据加载模块实现（数据读取层）。
//
// 实现思路：
//   1. 文件路径验证 + 存在性检查（Load 入口）；
//   2. 以二进制方式读取文件内容，交由 nlohmann::json 解析；
//   3. 捕获解析异常并区分"非法 JSON"与"结构不符合预期"两类错误；
//   4. 遍历 JSON 顶层数组，逐条提取分类与坐标点，转换为内部结构；
//   5. 解析结果缓存于单例，避免重复读盘。
// ============================================================================

#include <pch.h>
#include "engine/CoordinatesLoader.hpp"

// nlohmann/json：使用项目内 third_party 自带的头文件，避免依赖外部路径。
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cmath>

using json = nlohmann::json;

// ----------------------------------------------------------------------------
// 内部常量与工具（文件作用域，对外不可见）
// ----------------------------------------------------------------------------
namespace
{
    // 默认坐标文件路径（项目约定的数据来源）。
    constexpr const char* kDefaultCoordPath = "D:\\Games\\Palworld.Coords.json";

    // JSON 键名（与数据文件保持一致）。
    constexpr const char* kKeyCategory = "name";   // 分类/条目名称
    constexpr const char* kKeyName     = "name";   // 单个坐标点名称（与分类同键名）
    constexpr const char* kKeyItems    = "items";  // 分类下的坐标条目数组
    constexpr const char* kKeyValue    = "value";  // 单个条目的坐标数组 [x, y, z]

    // 校验坐标值是否为有限数值（排除 NaN / Inf / 无效值）。
    bool IsFinite(float v)
    {
        return std::isfinite(v);
    }
}

// ----------------------------------------------------------------------------
// TeleporterPoint
// ----------------------------------------------------------------------------
bool TeleporterPoint::IsValid() const
{
    return IsFinite(x) && IsFinite(y) && IsFinite(z) &&
           (x != 0.0f || y != 0.0f || z != 0.0f); // 全 0 视为无效占位数据
}

// ----------------------------------------------------------------------------
// CoordinatesLoader
// ----------------------------------------------------------------------------
CoordinatesLoader& CoordinatesLoader::Get()
{
    static CoordinatesLoader instance;
    return instance;
}

std::string CoordinatesLoader::DefaultPath()
{
    return kDefaultCoordPath;
}

size_t CoordinatesLoader::PointCount() const
{
    size_t total = 0;
    for (const auto& cat : m_categories)
        total += cat.points.size();
    return total;
}

void CoordinatesLoader::Reset()
{
    m_categories.clear();
    m_status = CoordLoadStatus::EmptyData;
    m_lastError.clear();
}

CoordLoadStatus CoordinatesLoader::Load(const std::string& path)
{
    // ---- 复位上一次的结果，保证状态一致 ----
    Reset();

    const std::string target = path.empty() ? DefaultPath() : path;

    // =====================================================================
    // 1. 文件存在性检查
    // =====================================================================
    {
        std::ifstream probe(target, std::ios::binary);
        if (!probe)
        {
            m_status = CoordLoadStatus::FileNotFound;
            m_lastError = "坐标文件不存在或无法访问: " + target;
            return m_status;
        }
    }

    // =====================================================================
    // 2. 读取文件全部内容
    // =====================================================================
    std::ifstream file(target, std::ios::binary);
    if (!file)
    {
        m_status = CoordLoadStatus::FileNotReadable;
        m_lastError = "坐标文件已存在但打开失败: " + target;
        return m_status;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    if (file.bad())
    {
        m_status = CoordLoadStatus::FileNotReadable;
        m_lastError = "读取坐标文件内容时发生 I/O 错误: " + target;
        return m_status;
    }

    const std::string content = buffer.str();
    if (content.empty())
    {
        m_status = CoordLoadStatus::InvalidJson;
        m_lastError = "坐标文件内容为空: " + target;
        return m_status;
    }

    // =====================================================================
    // 3. JSON 格式解析（捕获异常以区分"非法 JSON"）
    // =====================================================================
    json document;
    try
    {
        document = json::parse(content);
    }
    catch (const json::parse_error& e)
    {
        m_status = CoordLoadStatus::InvalidJson;
        m_lastError = std::string("坐标文件不是合法 JSON (位置 ") + std::to_string(e.byte) + "): " + target;
        return m_status;
    }
    catch (const std::exception& e)
    {
        m_status = CoordLoadStatus::InvalidJson;
        m_lastError = std::string("坐标文件解析失败: ") + e.what();
        return m_status;
    }

    // =====================================================================
    // 4. 结构校验 + 数据提取
    // =====================================================================
    // 顶层必须为数组：每个元素为 { "name": 分类名, "items": [...] }。
    if (!document.is_array())
    {
        m_status = CoordLoadStatus::InvalidStructure;
        m_lastError = "坐标文件顶层不是 JSON 数组: " + target;
        return m_status;
    }

    m_categories.reserve(document.size());

    for (const auto& categoryNode : document)
    {
        if (!categoryNode.is_object())
            continue; // 跳过非对象条目，容忍个别脏数据

        TeleporterCategory category;
        category.name = categoryNode.value(kKeyCategory, std::string());

        const auto itemsIt = categoryNode.find(kKeyItems);
        if (itemsIt != categoryNode.end() && itemsIt->is_array())
        {
            for (const auto& item : *itemsIt)
            {
                if (!item.is_object())
                    continue;

                const auto valueIt = item.find(kKeyValue);
                if (valueIt == item.end() || !valueIt->is_array() || valueIt->size() < 3)
                    continue; // 缺少有效坐标数组的条目跳过

                TeleporterPoint point;
                point.name = item.value(kKeyName, std::string()); // 坐标点名称
                point.x = (*valueIt)[0].get<float>();
                point.y = (*valueIt)[1].get<float>();
                point.z = (*valueIt)[2].get<float>();

                if (point.IsValid())
                    category.points.push_back(std::move(point));
            }
        }

        // 分类无有效点也保留（便于 UI 显示分类结构），空分类同样加入。
        m_categories.push_back(std::move(category));
    }

    // =====================================================================
    // 5. 结果判定：完全无数据时给出明确反馈
    // =====================================================================
    if (m_categories.empty())
    {
        m_status = CoordLoadStatus::EmptyData;
        m_lastError = "坐标文件中没有可用的分类数据: " + target;
        return m_status;
    }

    m_status = CoordLoadStatus::Ok;
    m_lastError.clear();
    return m_status;
}

// ----------------------------------------------------------------------------
// 便捷函数实现
// ----------------------------------------------------------------------------
CoordinatesLoader& GetCoordinatesLoader()
{
    return CoordinatesLoader::Get();
}
