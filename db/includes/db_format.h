#ifndef MINI_LEVELDB_DB_FORMAT_H
#define MINI_LEVELDB_DB_FORMAT_H
#include <string>
#include <string_view>
#include <cstdint>

// --- 记录定义 ---
enum class OperationType : uint8_t // 物理状态标识：替代物理删除
{
    kDeletion = 0x0,
    kValue = 0x1
};
struct ParsedRecord // 栈上临时记录
{
    std::string key;
    uint64_t seq;
    OperationType type;
    std::string value;
};
struct ParsedRecordView // 零拷贝内存观测窗口
{
    std::string_view key;
    uint64_t seq;
    OperationType type;
    std::string_view value;
};

// --- 检索定义 ---
enum class SearchStatus: uint8_t // 搜索状态
{
    kNotFound,
    kFoundValue,
    kFoundDeletion,
    FNotOpen
};
struct SearchResult // 内部检索结果
{
    SearchStatus status;
    std::string value;
};

#endif //MINI_LEVELDB_DB_FORMAT_H
