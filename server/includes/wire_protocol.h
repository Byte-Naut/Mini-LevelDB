#ifndef MINI_LEVELDB_WIRE_PROTOCOL_H
#define MINI_LEVELDB_WIRE_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>

namespace net
{
    // 绝对物理常量：魔数拦截器（'K'），用于在解析第一字节时直接击杀非法端口扫描
    constexpr uint8_t PROTOCOL_MAGIC = 0x4B;

    // 操作码映射
    enum class NetOpcode : uint8_t
    {
        kPut = 0x01,
        kGet = 0x02,
        kDelete = 0x03,
        kError = 0xFF
    };

    // 强制剥离 C++ 结构体内存对齐填充（Padding）
#pragma pack(push, 1)
    struct MsgHeader
    {
        uint8_t magic; // 1B
        NetOpcode opcode; // 1B
        uint32_t key_len; // 4B
        uint32_t value_len; // 4B
    };
#pragma pack(pop)

// 编译期物理校验：确立 10 字节绝对红线
static_assert(sizeof(MsgHeader) == 10, "FATAL: MsgHeader 物理体积异常，网络反序列化将发生指针错位");
static_assert(std::is_trivial_v<MsgHeader> && std::is_standard_layout_v<MsgHeader>, "FATAL: MsgHeader 必须是标量载体以支持底层 read_raw 零拷贝");

    // 内存态请求解包载体
    struct NetRequest
    {
        NetOpcode opcode;
        std::string key;
        std::string value;
    };

    // 内存态回执载体
    struct NetResponse
    {
        uint8_t status; // 0x00：成功， 0x01: 失败/未找到
        std::string value;
    };

    // TCP 会话生命周期状态机
    enum class SessionState
    {
        READ_HEADER, // 状态 1：正在从 Socket 提取 10 字节定长协议头
        READ_BODY, // 状态2：依据头部解算的长度，抽取变长 KV 数据流
        PROCESS, // 状态3：数据包物理闭环，移交后台 Worker 线程进行算力处理
        WRITE_REPLY // 状态4：存储引擎计算完毕，等待 Epoll 唤醒可写事件回注网卡
    };

} // namespace net

#endif //MINI_LEVELDB_WIRE_PROTOCOL_H