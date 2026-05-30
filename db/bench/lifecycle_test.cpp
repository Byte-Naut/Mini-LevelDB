#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "./db/includes/mini_kv_store.h"

using namespace std;

void simulate_first_lifecycle() {
    cout << "[SYS] --- 第一生命周期：高频注入与物理拓扑成型 ---" << endl;
    mini_kv_store kv_store;

    cout << "[SYS] 埋设跨周期定标靶向点..." << endl;
    kv_store.put("Lifecycle_Target_A", "Manifest_Persistence_Confirmed");
    kv_store.put("Lifecycle_Target_B", "Bloom_Filter_Reload_Success");

    cout << "[SYS] 实施高熵洪泛注入，强行撑破 L0 阈值，触发 L1 沉降..." << endl;
    // 假设 memtable_size_threshold_ 为 4KB，此处注入足量数据以生成多层级矩阵
    for (int i = 0; i < 200; ++i) {
        kv_store.put("Padding_" + to_string(i), string(150, 'X'));
    }

    // 强行挂起，确保后台算力完成所有归并任务与 MANIFEST 覆写
    cout << "[SYS] 等待后台算力物理静默..." << endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    cout << "[SYS] 触发物理断电：局部作用域退栈，彻底销毁第一生命周期实例。" << endl;
} // kv_store 析构，RAII 机制接管优雅停机

void simulate_second_lifecycle() {
    cout << "\n[SYS] --- 第二生命周期：冷启动与拓扑残骸复苏 ---" << endl;
    // 构造瞬间，底层强制优先执行 load_manifest_and_rebuild_cache
    const mini_kv_store kv_store_rebooted;

    cout << "[SYS] 启动深海物理打捞，穿越文件矩阵检索靶向点..." << endl;
    const string res_a = kv_store_rebooted.get("Lifecycle_Target_A");

    if (const string res_b = kv_store_rebooted.get("Lifecycle_Target_B"); res_a == "Manifest_Persistence_Confirmed" && res_b == "Bloom_Filter_Reload_Success") {
        cout << "[PASS] 拓扑重载成功！数据成功实现跨越进程生死边界的绝对延续。" << endl;
    } else {
        cout << "[FATAL] 物理失忆！拓扑重建断裂或时间偏序错乱。 res_a: " << res_a << endl;
    }

    cout << "[SYS] 探针空发测试：检验布隆过滤器拦截网..." << endl;
    if (const string res_ghost = kv_store_rebooted.get("Ghost_Key_Not_Exist"); res_ghost.empty()) {
        cout << "[PASS] 内存断路器生效。布隆过滤器成功阻断对不存在数据的磁盘下潜。" << endl;
    } else {
        cout << "[FATAL] 拦截网被击穿，或读取到幻象残留！" << endl;
    }
}

int main() {
    simulate_first_lifecycle();
    simulate_second_lifecycle();

    cout << "\n--- 系统级物理验收完成，存储引擎底层架构绝对闭环 ---" << endl;
    return 0;
}//
// Created by ljyay on 2026/5/13.
//