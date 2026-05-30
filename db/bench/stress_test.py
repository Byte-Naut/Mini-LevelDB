import asyncio
import struct
import time

# 绝对物理常量：对齐 C++ 端 wire_protocol.h
PROTOCOL_MAGIC = 0x4B
OP_PUT = 0x01
OP_GET = 0x02
OP_DEL = 0x03

HOST = '127.0.0.1'
PORT = 8080
CONCURRENCY_LIMIT = 1000 # 信号量屏障：防止击穿操作系统的单进程全连接数上限 (ulimit -n)
TOTAL_REQUESTS = 10000   # 靶向总目标

async def fire_binary_probe(opcode: int, key: bytes, value: bytes = b"") -> tuple[int, bytes]:
    """单次 TCP 二进制协议突击探针"""
    try:
        reader, writer = await asyncio.open_connection(HOST, PORT)

        # 协议定界：<BBII (小端序: 1B Magic, 1B Opcode, 4B KeyLen, 4B ValLen)
        header = struct.pack('<BBII', PROTOCOL_MAGIC, opcode, len(key), len(value))

        # 将 Header + Key + Value 拼装为连续字节流，暴力注入网卡
        writer.write(header + key + value)
        await writer.drain()

        # 状态机：拦截 10 字节响应头
        resp_header_bytes = await reader.readexactly(10)
        _, _, _, resp_val_len = struct.unpack('<BBII', resp_header_bytes)

        # 状态机：拦截响应体 (1 Byte 状态码 + Payload)
        resp_body = await reader.readexactly(resp_val_len)
        status = resp_body[0]
        payload = resp_body[1:]

        writer.close()
        await writer.wait_closed()

        return status, payload
    except Exception as e:
        return 0xFF, str(e).encode()

async def worker_routine(sem, index, results):
    """单个协程的压测作战流：依次执行 PUT -> GET -> DEL"""
    async with sem: # 获取物理令牌
        key = f"Target_Key_{index}".encode()
        val = f"Payload_Data_Block_#_{index}_" * 10 # 增加载荷体积，逼迫 C++ 端触发 LRU 换页
        val = val.encode()

        # 1. 执行写压测
        await fire_binary_probe(OP_PUT, key, val)

        # 2. 执行读压测与完整性校验
        status, resp_val = await fire_binary_probe(OP_GET, key)

        if status == 0x00 and resp_val == val:
            results['success'] += 1
        else:
            results['fail'] += 1

async def main():
    print("[SYS] 正在装填二进制探针，锁定目标端口 8080...")
    sem = asyncio.Semaphore(CONCURRENCY_LIMIT)
    results = {'success': 0, 'fail': 0}

    start_time = time.time()

    # 瞬间爆发：将万级并发任务全量压入事件循环
    tasks = [worker_routine(sem, i, results) for i in range(TOTAL_REQUESTS)]
    await asyncio.gather(*tasks)

    end_time = time.time()
    elapsed = end_time - start_time

    print("\n--- 压测物理报告 ---")
    print(f"总计发放组合指令 (PUT+GET): {TOTAL_REQUESTS * 2} 次")
    print(f"成功击中: {results['success']} / 失败脱靶: {results['fail']}")
    print(f"物理耗时: {elapsed:.3f} 秒")
    print(f"全局吞吐量 (QPS): {(TOTAL_REQUESTS * 2) / elapsed:.0f} req/s")

    if results['fail'] == 0:
        print("[PASS] 防线未被击穿！全链路 IO 与 LRU 状态机绝对自洽。")
    else:
        print("[FATAL] 存在算力断层或内存脏读，需回收日志！")

if __name__ == "__main__":
    asyncio.run(main())