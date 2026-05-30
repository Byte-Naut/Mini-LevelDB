# Mini-LevelDB 内存泄漏检测与原生 perf 火焰图完整流程

## 1. 目标与边界
本流程用于在 WSL 环境下，对 Mini-LevelDB 做可重复的：

1. 内存泄漏检测（优先覆盖 `mini_kv_store` 主路径）
2. 原生 `perf` 火焰图生成（`perf record -> perf script -> stackcollapse -> flamegraph`）
3. 产物归档与历史痕迹清理

边界说明：

- 当前 CMake 入口为 `main.cpp` 对应 `Mini_LevelDB`，该目标保留为基线构建验证。
- 性能与泄漏分析以存储引擎路径为主，网络层仅做可运行性确认。

## 2. 环境要求

1. 运行环境：WSL2 + Linux 用户态工具链
2. 建议在 ext4 目录执行压测与 perf（如 `/tmp/...`），避免 DrvFS 行为差异
3. 需要工具：
   - `cmake`, `g++`
   - `valgrind`
   - `perf`（本次使用：`/usr/lib/linux-tools/6.8.0-117-generic/perf`）
   - FlameGraph 脚本（`stackcollapse-perf.pl`、`flamegraph.pl`）

## 3. 目录约定

- 仓库目录：`/mnt/d/Resource/Projects/Mini-LevelDB`
- 临时工作目录（ext4）：
  - `/tmp/minikv-leak`（泄漏检测）
  - `/tmp/minikv-native`（perf 采样）
- 归档目录：`D:/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts`

## 4. 内存泄漏检测流程

### 4.1 准备与构建

```bash
wsl -e bash -lc '
set -e
rm -rf /tmp/minikv-leak
mkdir -p /tmp/minikv-leak
cp -a /mnt/d/Resource/Projects/Mini-LevelDB/. /tmp/minikv-leak/
cmake -S /tmp/minikv-leak -B /tmp/minikv-leak/build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/minikv-leak/build-debug -j
'
```

### 4.2 运行 Valgrind（基线目标）

```bash
wsl -e bash -lc '
set -e
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
  /tmp/minikv-leak/build-debug/Mini_LevelDB \
  > /tmp/minikv-leak/valgrind_main.out \
  2> /tmp/minikv-leak/valgrind_main.txt || true
'
```

说明：

- `Mini_LevelDB` 为网络事件循环程序，可能长期运行。必要时请在受控输入或短时运行条件下采样。
- 结果以 `valgrind_main.txt` 中的泄漏摘要为准（重点看 `definitely lost`, `indirectly lost`, `possibly lost`）。

### 4.3 引擎路径泄漏检测（推荐）

建议增加一个短生命周期的引擎驱动程序（仅调用 `mini_kv_store` 的 `put/get/erase` 并退出）后，用同样的 Valgrind 参数执行。这样可避免网络层常驻行为干扰泄漏判断。

## 5. 原生 perf 火焰图流程

### 5.1 准备 FlameGraph 脚本

```bash
wsl -e bash -lc '
set -e
if [ ! -d /tmp/FlameGraph ]; then
  git clone --depth 1 https://github.com/brendangregg/FlameGraph /tmp/FlameGraph
fi
test -x /tmp/FlameGraph/stackcollapse-perf.pl
test -x /tmp/FlameGraph/flamegraph.pl
'
```

### 5.2 构建可采样目标（带符号 + 帧指针）

```bash
wsl -e bash -lc '
set -e
rm -rf /tmp/minikv-native
mkdir -p /tmp/minikv-native
cp -a /mnt/d/Resource/Projects/Mini-LevelDB/. /tmp/minikv-native/
cmake -S /tmp/minikv-native -B /tmp/minikv-native/build-prof \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer"
cmake --build /tmp/minikv-native/build-prof -j
'
```

### 5.3 运行引擎负载并采样

使用短生命周期驱动程序（直接调用 `mini_kv_store`）执行采样。

```bash
wsl -e bash -lc '
set -e
PERF_BIN="/usr/lib/linux-tools/6.8.0-117-generic/perf"
cd /tmp/minikv-native
$PERF_BIN record -F 299 -g --call-graph fp -- ./local_perf_driver \
  > /tmp/minikv-native/perf_run.out \
  2> /tmp/minikv-native/perf_record.err
$PERF_BIN script -i perf.data > /tmp/minikv-native/perf.script
'
```

### 5.4 生成火焰图

```bash
wsl -e bash -lc '
set -e
/tmp/FlameGraph/stackcollapse-perf.pl /tmp/minikv-native/perf.script > /tmp/minikv-native/perf.folded
/tmp/FlameGraph/flamegraph.pl /tmp/minikv-native/perf.folded > /tmp/minikv-native/perf_flame_native.svg
wc -l /tmp/minikv-native/perf.script /tmp/minikv-native/perf.folded
'
```

本次成功生成示例（参考）：

- `perf.script`: 578 行
- `perf.folded`: 31 行
- `perf_flame_native.svg`: 已生成

## 6. 产物归档

将最新有效产物复制到仓库归档目录：

```bash
wsl -e bash -lc '
set -e
mkdir -p /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts
cp -f /tmp/minikv-native/perf_flame_native.svg /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/perf_flame_native.svg
cp -f /tmp/minikv-native/perf.folded /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/perf_folded.txt
cp -f /tmp/minikv-native/perf.script /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/perf_script.txt
'
```

建议同时归档：

- `valgrind_main.txt`
- 引擎驱动对应的 `valgrind_*.txt`
- `perf_record.err`（用于排查采样失败）

## 7. 清理策略（保留最新，移除旧版）

### 7.1 清理旧产物

```bash
wsl -e bash -lc '
set -e
rm -f /mnt/d/Resource/Projects/Mini-LevelDB/perf.data
rm -f /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/mini_kv_flame.svg
rm -f /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/callgrind.out.25288
rm -f /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/callgrind_top.txt
rm -f /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/valgrind_minikv.txt
rm -f /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/valgrind_v2.txt
rm -f /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/bench_results.csv
rm -f /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/bench_v2.csv
rm -f /mnt/d/Resource/Projects/Mini-LevelDB/._session_artifacts/run_artifacts/stress_output.txt
'
```

### 7.2 清理临时目录

```bash
wsl -e bash -lc '
set -e
rm -rf /tmp/minikv-leak /tmp/minikv-native /tmp/minikv-crash /tmp/minikv-fix
'
```

## 8. 常见失败与处理

1. `filesystem error: cannot rename ... [MANIFEST.tmp] [MANIFEST]`
- 多见于工作目录/文件系统行为差异。
- 优先在 WSL ext4 路径运行，减少 DrvFS 影响。

2. `perf` 与内核版本提示不匹配
- 显式指定可用 perf 二进制路径（例如本次路径）。

3. 样本量过少
- 提高稳定负载时长前，先确保程序无崩溃。
- 若出现 `malloc(): unsorted double linked list corrupted`，先修复内存并发问题再采样。

## 9. 本仓库当前推荐最小执行顺序

1. 修复后代码同步到仓库
2. 在 `/tmp/minikv-native` 重建并跑 native perf
3. 归档 `perf_flame_native.svg/perf_folded.txt/perf_script.txt`
4. 跑 Valgrind 生成泄漏报告
5. 清理旧版与临时目录

以上流程可直接复用为后续版本回归检查模板。
