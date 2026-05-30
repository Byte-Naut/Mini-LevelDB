// db/bench/mini_kv_bench.cpp
// Engine benchmark: throughput, per-operation latency percentiles (P50/P95/P99).
// Scenarios: write_heavy / mixed / read_heavy / mixed_with_delete
// Usage:
//   mini_kv_bench [--ops N] [--keys N] [--value-size N] [--seed N]
//                 [--read-ratio N] [--delete-ratio N]
//                 [--scenario NAME] [--correctness]

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../includes/mini_kv_store.h"

using clock_type = std::chrono::steady_clock;
using us         = std::chrono::microseconds;

struct Scenario
{
    std::string name;
    int         total_ops;
    int         read_ratio;
    int         delete_ratio;
    int         key_count;
    int         value_size;
    uint32_t    seed;
};

struct ScenarioResult
{
    double throughput;
    double elapsed_s;
    int    success;
    int    fail;
    double put_p50, put_p95, put_p99;
    double get_p50, get_p95, get_p99;
    double del_p50, del_p95, del_p99;
};

static double percentile(std::vector<double>& v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const auto idx = static_cast<size_t>(
        std::min<double>(v.size() - 1, (p / 100.0) * (v.size() - 1)));
    return v[idx];
}

static void correctness_check()
{
    std::cout << "[correctness] PUT->GET->DEL->GET cycle...\n";
    mini_kv_store store;

    for (int i = 0; i < 200; ++i)
        store.put("ckey_" + std::to_string(i), "cval_" + std::to_string(i));

    int ok = 0, missing = 0;
    for (int i = 0; i < 200; ++i)
    {
        auto v = store.get("ckey_" + std::to_string(i));
        if (v == "cval_" + std::to_string(i)) ++ok; else ++missing;
    }
    std::cout << "[correctness] after PUT: found=" << ok
              << " missing=" << missing << "\n";

    for (int i = 0; i < 200; i += 2)
        store.erase("ckey_" + std::to_string(i));

    int tombstone = 0, alive = 0, wrong = 0;
    for (int i = 0; i < 200; ++i)
    {
        auto v = store.get("ckey_" + std::to_string(i));
        if (i % 2 == 0) { if (v.empty()) ++tombstone; else ++wrong; }
        else            { if (v == "cval_" + std::to_string(i)) ++alive; else ++wrong; }
    }
    std::cout << "[correctness] after DELETE: tombstone=" << tombstone
              << " alive=" << alive << " wrong=" << wrong << "\n";
    std::cout << "[correctness] "
              << (tombstone == 100 && alive == 100 && wrong == 0 ? "PASS" : "FAIL")
              << "\n";
}

static ScenarioResult run_scenario(const Scenario& s)
{
    mini_kv_store store;
    std::mt19937  rng(s.seed);
    std::uniform_int_distribution<int> key_dist(0, s.key_count - 1);
    std::uniform_int_distribution<int> op_dist(1, 100);

    std::vector<double> put_lat, get_lat, del_lat;
    put_lat.reserve(s.total_ops);
    get_lat.reserve(s.total_ops);
    del_lat.reserve(s.total_ops);

    // warm-up: pre-populate keys so reads have something to hit
    for (int i = 0; i < s.key_count; ++i)
        store.put("bench_key_" + std::to_string(i),
                  std::string(s.value_size, static_cast<char>('a' + (i % 26))));

    int success = 0, fail = 0;
    const auto g0 = clock_type::now();

    for (int i = 0; i < s.total_ops; ++i)
    {
        const int  kid = key_dist(rng);
        std::string key = "bench_key_" + std::to_string(kid);
        const int  roll = op_dist(rng);

        if (roll <= s.read_ratio)
        {
            auto t0 = clock_type::now();
            auto v  = store.get(key);
            auto t1 = clock_type::now();
            get_lat.push_back(static_cast<double>(
                std::chrono::duration_cast<us>(t1 - t0).count()));
            if (v.empty()) ++fail; else ++success;
        }
        else if (s.delete_ratio > 0 && roll <= s.read_ratio + s.delete_ratio)
        {
            auto t0 = clock_type::now();
            store.erase(key);
            auto t1 = clock_type::now();
            del_lat.push_back(static_cast<double>(
                std::chrono::duration_cast<us>(t1 - t0).count()));
            ++success;
        }
        else
        {
            std::string v(s.value_size, static_cast<char>('a' + (i % 26)));
            auto t0 = clock_type::now();
            store.put(key, v);
            auto t1 = clock_type::now();
            put_lat.push_back(static_cast<double>(
                std::chrono::duration_cast<us>(t1 - t0).count()));
            ++success;
        }
    }

    const auto g1 = clock_type::now();
    double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(g1 - g0).count();

    return {
        static_cast<double>(s.total_ops) / elapsed, elapsed, success, fail,
        percentile(put_lat, 50), percentile(put_lat, 95), percentile(put_lat, 99),
        percentile(get_lat, 50), percentile(get_lat, 95), percentile(get_lat, 99),
        percentile(del_lat, 50), percentile(del_lat, 95), percentile(del_lat, 99),
    };
}

static int parse_int(const char* v, int fallback)
{
    if (!v) return fallback;
    char* e = nullptr;
    long  n = std::strtol(v, &e, 10);
    if (e == v || *e != '\0') return fallback;
    return static_cast<int>(n);
}

int main(int argc, char** argv)
{
    int      ops = 8000, keys = 1000, vsize = 64;
    uint32_t seed = 42;
    bool     single = false;
    std::string sname;
    int rratio = 70, dratio = 0;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if      (a == "--ops"          && i + 1 < argc) ops    = parse_int(argv[++i], ops);
        else if (a == "--keys"         && i + 1 < argc) keys   = parse_int(argv[++i], keys);
        else if (a == "--value-size"   && i + 1 < argc) vsize  = parse_int(argv[++i], vsize);
        else if (a == "--seed"         && i + 1 < argc) seed   = static_cast<uint32_t>(parse_int(argv[++i], static_cast<int>(seed)));
        else if (a == "--read-ratio"   && i + 1 < argc) rratio = parse_int(argv[++i], rratio);
        else if (a == "--delete-ratio" && i + 1 < argc) dratio = parse_int(argv[++i], dratio);
        else if (a == "--scenario"     && i + 1 < argc) { single = true; sname = argv[++i]; }
        else if (a == "--correctness")                  { correctness_check(); return 0; }
    }

    std::vector<Scenario> scenarios;
    if (single)
        scenarios.push_back({sname, ops, rratio, dratio, keys, vsize, seed});
    else
        scenarios = {
            {"write_heavy",       ops, 10,  0, keys, vsize, seed},
            {"mixed",             ops, 60, 10, keys, vsize, seed},
            {"read_heavy",        ops, 90,  0, keys, vsize, seed},
            {"mixed_with_delete", ops, 50, 20, keys, vsize, seed},
        };

    std::cout << "scenario,elapsed_s,throughput_ops_s,success,fail,"
                 "put_p50_us,put_p95_us,put_p99_us,"
                 "get_p50_us,get_p95_us,get_p99_us,"
                 "del_p50_us,del_p95_us,del_p99_us\n";

    for (auto& s : scenarios)
    {
        auto r = run_scenario(s);
        std::cout << s.name << ","
                  << r.elapsed_s    << "," << r.throughput << ","
                  << r.success      << "," << r.fail       << ","
                  << r.put_p50      << "," << r.put_p95    << "," << r.put_p99 << ","
                  << r.get_p50      << "," << r.get_p95    << "," << r.get_p99 << ","
                  << r.del_p50      << "," << r.del_p95    << "," << r.del_p99 << "\n";
    }
    return 0;
}
