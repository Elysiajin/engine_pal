#pragma once
// ============================================================================
// CoordinatesLoader.h
// 传送器坐标数据加载模块（数据读取层）。
//
// 功能：
//   - 从本地 JSON 文件（默认 D:\Games\Palworld.Coords.json）加载传送器坐标；
//   - 提供文件路径验证、文件存在性检查、JSON 格式校验、错误处理；
//   - 将 JSON 中分类嵌套的坐标数据解析为扁平化的、可用于 UI 展示的结构。
//
// 该模块与业务逻辑（传送/传送点管理）解耦，只负责"读取并解析数据"，
// 上层（TabTeleporter / 业务层）通过本模块提供的查询接口获取数据。
// ============================================================================

#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// 坐标加载结果枚举：用于向调用方清晰反馈加载/解析过程中出现的各类问题。
// ----------------------------------------------------------------------------
enum class CoordLoadStatus
{
    Ok = 0,                 // 加载并解析成功
    FileNotFound,           // 文件不存在
    FileNotReadable,        // 文件存在但无法打开/读取（权限、占用等）
    InvalidJson,            // 文件内容不是合法 JSON
    InvalidStructure,       // JSON 结构不符合预期（非数组 / 缺少字段 / 值类型错误）
    EmptyData               // 解析成功但没有可用数据
};

// ----------------------------------------------------------------------------
// 单个坐标点：包含显示名称与三维坐标（X/Y/Z）。
// 坐标类型使用 float，与游戏引擎 SDK::FVector 一致，可直接用于传送。
// ----------------------------------------------------------------------------
struct TeleporterPoint
{
    std::string name;       // 坐标点名称（如 "*Lv11-野外-叶泥泥-1108"）
    float x = 0.0f;         // X 轴坐标
    float y = 0.0f;         // Y 轴坐标
    float z = 0.0f;         // Z 轴坐标

    // 判断该点坐标是否为有效数值（非 NaN/Inf 且非全 0）。
    bool IsValid() const;
};

// ----------------------------------------------------------------------------
// 坐标分类：JSON 顶层按分类组织（如 "基地选址"、"战斗--Boss位置" 等），
// 每个分类下包含若干坐标点。
// ----------------------------------------------------------------------------
struct TeleporterCategory
{
    std::string name;                     // 分类名称
    std::vector<TeleporterPoint> points;  // 该分类下的全部坐标点
};

// ----------------------------------------------------------------------------
// 坐标加载器：负责 JSON 文件的验证、解析与缓存。
// 采用单例模式，避免每次打开 UI 都重复读取大文件。
// ----------------------------------------------------------------------------
class CoordinatesLoader
{
public:
    // 获取全局唯一实例。
    static CoordinatesLoader& Get();

    // 禁止拷贝 / 赋值。
    CoordinatesLoader(const CoordinatesLoader&) = delete;
    CoordinatesLoader& operator=(const CoordinatesLoader&) = delete;

    // ------------------------------------------------------------------
    // 加载指定路径的 JSON 坐标文件。成功返回 CoordLoadStatus::Ok。
    // path 为空时使用默认路径（D:\Games\Palworld.Coords.json）。
    // 加载成功后可通过 Categories() / PointCount() 访问数据。
    // ------------------------------------------------------------------
    CoordLoadStatus Load(const std::string& path = DefaultPath());

    // 返回当前已加载的全部分类（只读）。
    const std::vector<TeleporterCategory>& Categories() const { return m_categories; }

    // 返回当前已加载的坐标点总数。
    size_t PointCount() const;

    // 返回默认的坐标文件路径。
    static std::string DefaultPath();

    // 返回上一次加载操作的错误描述（可用于 UI 提示 / 日志）。
    const std::string& LastError() const { return m_lastError; }

    // 返回上一次加载操作的状态码。
    CoordLoadStatus LastStatus() const { return m_status; }

private:
    CoordinatesLoader() = default;        // 私有构造（单例）
    ~CoordinatesLoader() = default;

    // 清空已加载数据并复位状态。
    void Reset();

    std::vector<TeleporterCategory> m_categories; // 已解析的分类数据（缓存）
    CoordLoadStatus m_status = CoordLoadStatus::EmptyData;
    std::string m_lastError;
};

// ----------------------------------------------------------------------------
// 便捷函数：返回指向全局单例的引用（写法更贴近项目中现有 database 用法）。
// ----------------------------------------------------------------------------
CoordinatesLoader& GetCoordinatesLoader();
