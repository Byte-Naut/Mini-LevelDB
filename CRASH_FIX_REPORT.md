# mini_kv_store 崩溃定位与修复记录

## 1. 问题现象
在较高负载压测下，进程出现如下崩溃：

- `malloc(): unsorted double linked list corrupted`
- 偶发表现为提前 abort/segfault，导致 perf 采样窗口极短。

## 2. 复现方法
在 WSL 的 ext4 临时目录中构建带 ASan 的最小复现程序，直接压测 `mini_kv_store::put` 路径：

```bash
/usr/bin/c++ -std=c++20 -fsanitize=address -fno-omit-frame-pointer -g -pthread \
  -I/tmp/minikv-crash \
  -o /tmp/minikv-crash/local_driver \
  /tmp/minikv-crash/local_driver.cpp \
  /tmp/minikv-crash/db/mini_kv_store.cpp \
  /tmp/minikv-crash/mem_table/mem_table.cpp
```

ASan 在复现时稳定报错 `heap-use-after-free`。

## 3. 根因分析
根因是 **同一 `wal_file_` 被前台写线程和后台 compaction 线程并发访问**，且缺少独立互斥保护。

触发路径：

1. 前台线程在 `append_to_wal` 中执行写入与 `flush`。
2. 后台线程在 `background_compaction_routine` 中执行 `wal_file_.close()` / `open(..., trunc)`。
3. `ofstream` 内部缓冲在后台线程被销毁后，前台线程继续访问，形成 UAF，最终演化为堆破坏。

关键位置：

- `db/mini_kv_store.cpp:217` (`append_to_wal`)
- `db/mini_kv_store.cpp:266` (`background_compaction_routine` 中 close/open)

ASan 栈显示：

- 读发生在 `append_to_wal -> wal_file_.flush()`
- 释放发生在后台线程 `wal_file_.close()`

## 4. 修复策略
保持现有架构与风格，仅做最小修复：

1. 为 WAL 增加独立互斥 `wal_mutex_`。
2. `append_to_wal` 全路径加锁，串行化 `write/flush`。
3. 后台线程执行 `wal_file_.close/open(trunc)` 时同样加锁。
4. 析构时关闭 `wal_file_` 前加锁，避免停机阶段竞态。

## 5. 代码变更

- `db/includes/mini_kv_store.h`
  - 新增头文件：`#include <mutex>`
  - 新增成员：`mutable std::mutex wal_mutex_;`

- `db/mini_kv_store.cpp`
  - `append_to_wal(...)` 内新增 `std::lock_guard wal_lock(wal_mutex_);`
  - `background_compaction_routine()` 中 WAL 截断操作改为加锁块
  - `~mini_kv_store()` 中 WAL flush/close 改为加锁块

## 6. 修复验证

### 6.1 ASan 复现回归
使用同样复现程序与参数重新运行，结果：

- 进程正常退出
- `stderr` 无 ASan 报错
- 业务输出正常（`Manifest_Persistence_Confirmed`）

### 6.2 基线构建验证
对当前 CMake 入口目标重新构建：

```bash
cmake -S /tmp/minikv-fix -B /tmp/minikv-fix/build
cmake --build /tmp/minikv-fix/build -j
```

`Mini_LevelDB` 构建成功。

## 7. 结论与后续
本次崩溃的主因已定位并修复为 WAL 文件句柄并发竞态。修复后，原先的高频 UAF/堆破坏已消失，满足后续继续进行高密度 perf 采样的前置条件。

建议后续继续两项工作：

1. 在更高 `ops` 下做长时间 ASan/TSan 压测，确认无新增并发缺陷。
2. 在稳定运行窗口内重新执行原生 perf 火焰图采集，以获得可解释性更高的热点分布。
