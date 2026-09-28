/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 * See LICENSE in the root of the software repository.
 */
#pragma once
#include <sstream>
#include <string>
#include <vector>
#include "tiling.h"
#include "utils/arg_parser.h"

namespace ain_agmm {
// 示例的 Host 配置：tileM/cores 为 0 表示自动选择，devices 保存 rank 到 ACL device 的映射。
struct Options {
    std::string endpoint;
    std::string mode = "both";
    uint32_t ranks = 0, m = 0, n = 0, k = 0;
    uint32_t tileM = 0, cores = 0, warmup = 10, iterations = 100, verifyIterations = 2;
    uint32_t timeout = 300;
    uint32_t delayRank = UINT32_MAX, delayMs = 0;
    bool bf16 = false, random = false, trace = false;
    std::vector<uint32_t> devices;
};

// 统一解析无符号参数并检查最小值，避免负数或超范围值被静默截断。
inline uint32_t Number(const char* text, uint32_t minimum = 0)
{
    uint32_t result;
    if (!examples::ParseUint32(text, result, minimum)) {
        throw std::invalid_argument(std::string("invalid integer: ") + text);
    }
    return result;
}

// 解析 endpoint ranks M K N 及可选项，在 fork/设备分配前拒绝非法配置。
inline Options ParseOptions(int argc, char** argv)
{
    if (argc < 6) {
        throw std::invalid_argument("Usage: ain_all_gather_matmul tcp://ip:port ranks M K N "
            "[--dtype fp16|bf16] [--mode both|fused|serial|hccl|comm|matmul] "
            "[--devices 1,2] [--tile-m M] [--cores C] [--warmup 10] [--iters 100] "
            "[--verify-iters 2] [--random] [--trace] [--timeout 300] [--delay-rank R --delay-ms MS]");
    }
    Options o;
    o.endpoint = argv[1];
    o.ranks = Number(argv[2], 2);
    o.m = Number(argv[3], 1);
    o.k = Number(argv[4], 1);
    o.n = Number(argv[5], 1);
    for (int i = 6; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--random") { o.random = true; continue; }
        if (key == "--trace") { o.trace = true; continue; }
        if (++i == argc) { throw std::invalid_argument("missing value for " + key); }
        const std::string value = argv[i];
        if (key == "--dtype") {
            if (value != "fp16" && value != "bf16") { throw std::invalid_argument("dtype must be fp16 or bf16"); }
            o.bf16 = value == "bf16";
        } else if (key == "--mode") { o.mode = value; }
        else if (key == "--tile-m") { o.tileM = Number(argv[i], 1); }
        else if (key == "--cores") { o.cores = Number(argv[i], 1); }
        else if (key == "--warmup") { o.warmup = Number(argv[i]); }
        else if (key == "--iters") { o.iterations = Number(argv[i], 1); }
        else if (key == "--verify-iters") { o.verifyIterations = Number(argv[i], 1); }
        else if (key == "--timeout") { o.timeout = Number(argv[i], 1); }
        else if (key == "--delay-rank") { o.delayRank = Number(argv[i]); }
        else if (key == "--delay-ms") { o.delayMs = Number(argv[i]); }
        else if (key == "--devices") {
            std::istringstream input(value);
            std::string part;
            if (value.empty() || value.back() == ',') { throw std::invalid_argument("invalid devices list"); }
            while (std::getline(input, part, ',')) { o.devices.push_back(Number(part.c_str())); }
        } else { throw std::invalid_argument("unknown option " + key); }
    }
    const std::vector<std::string> modes{"both", "fused", "serial", "hccl", "comm", "matmul"};
    if (std::find(modes.begin(), modes.end(), o.mode) == modes.end()) { throw std::invalid_argument("invalid mode"); }
    // 尚未查询硬件核数，先用占位核数检查 shape；RunRank 会按真实核数重新生成 tiling。
    (void)MakeTiling(o.m, o.n, o.k, o.ranks, o.cores ? o.cores : 1, o.tileM);
    if (o.devices.empty()) {
        for (uint32_t i = 0; i < o.ranks; ++i) { o.devices.push_back(i); }
    }
    // 只排序副本检查重复，不改变用户指定的 rank->device 顺序；互联可达性需由运行者选择。
    auto sorted = o.devices;
    std::sort(sorted.begin(), sorted.end());
    if (sorted.size() != o.ranks || std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
        throw std::invalid_argument("devices must contain one unique ACL device ID per rank");
    }
    if (o.delayMs != 0 && o.delayRank >= o.ranks) { throw std::invalid_argument("delay-rank must be in team"); }
    return o;
}
} // namespace ain_agmm
