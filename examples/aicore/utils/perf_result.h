/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file perf_result.h
 * \brief Ascend C样例共享的性能结果行收集与跨 rank 汇聚工具。
 *
 * 供 12/13 号 URMA 性能样例共用：结果行与定长线上格式（PerfRow / PerfRowWire）、
 * 造行与 PERF_DATA 打印（CollectPerfRows / PrintPerfRow）、csv 落盘（WritePerfCsv）、
 * 跨 rank 汇聚（GatherPerfRowsToRoot）。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_PERF_RESULT_H
#define ASC_COMM_EXAMPLES_UTILS_PERF_RESULT_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "rank_sync.h"
#include "perf_topology.h"

namespace examples {

// ---------------- 结果行（数值列 + 有限枚举字符串列） ----------------

// 一行 PERF_DATA / csv 结果；派生列（均值 / 单核耗时 / 带宽）由 host 侧浮点计算。
// api / commit / topology / drain 是有限枚举，存 const char*（指向各样例自己的
// 静态串或公共枚举名）。
struct PerfRow {
    uint32_t rank;
    const char* api;      // "WriteNbi" / "ReadNbi" / "WriteWithNotifyNbi"
    uint64_t len;         // DataSize/B
    uint32_t batch;       // BatchSize（无批维度的样例恒填 1，csv 照写）
    const char* commit;   // 提交方式名（各样例自己的 "inline"/"final" 等）
    const char* topology; // 拓扑名："p2p" / "ring"（公共 perf_topology.h）
    const char* drain;    // "lat" / "bw"
    uint32_t samples;     // 计时轮数（rounds）
    uint64_t cycles;      // 计时段原始 cycles
    double avgCycles;
    double singleCoreUs;
    double bandwidthGbs;
};

// ---------------- 线上格式（跨 rank 传输的定长编码） ----------------

// Allgather 是等长载荷协议：行集打包成「头 + 定容量行」。容量由两卡一致的档位表
// × 接口/口径掩码推出（命令行相同 → 表相同），无需协商；commit 列两卡恒同
// （来自同一份命令行 opt），不编码、rank 0 收侧重导；api / drain 是有限枚举，
// 编成小整数。各 rank 由同一二进制 fork、ABI 相同，可直接按结构体内存布局传输。
constexpr uint32_t PERF_WIRE_API_WRITE = 0U;
constexpr uint32_t PERF_WIRE_API_READ = 1U;
constexpr uint32_t PERF_WIRE_API_NOTIFY = 2U;
constexpr uint32_t PERF_WIRE_DRAIN_LAT = 0U;
constexpr uint32_t PERF_WIRE_DRAIN_BW = 1U;

struct PerfRowWire {
    uint64_t len;    // DataSize/B
    uint64_t cycles; // 计时段原始 cycles
    double avgCycles;
    double singleCoreUs;
    double bandwidthGbs;
    uint32_t rank;
    uint32_t batch;   // BatchSize
    uint32_t samples; // 计时轮数
    uint32_t api;     // PERF_WIRE_API_*
    uint32_t drain;   // PERF_WIRE_DRAIN_*
};

struct PerfWireHeader {
    uint32_t rowCount; // 有效行数（≤ 容量，失败档位不产生行）
    uint32_t reserved;
};

// ---------------- 枚举名 ↔ 线上编码 ----------------

inline uint32_t PerfApiNameToWire(const char* name)
{
    if (std::strcmp(name, "WriteNbi") == 0) {
        return PERF_WIRE_API_WRITE;
    }
    return std::strcmp(name, "ReadNbi") == 0 ? PERF_WIRE_API_READ : PERF_WIRE_API_NOTIFY;
}

inline const char* PerfApiWireToName(uint32_t code)
{
    switch (code) {
        case PERF_WIRE_API_WRITE:
            return "WriteNbi";
        case PERF_WIRE_API_READ:
            return "ReadNbi";
        default:
            return "WriteWithNotifyNbi";
    }
}

inline uint32_t PerfDrainNameToWire(const char* name)
{
    return std::strcmp(name, "lat") == 0 ? PERF_WIRE_DRAIN_LAT : PERF_WIRE_DRAIN_BW;
}

inline const char* PerfDrainWireToName(uint32_t code) { return code == PERF_WIRE_DRAIN_LAT ? "lat" : "bw"; }

// ---------------- 造行常量（与 12/13 号样例 def.h 同值同义） ----------------

// 样例的 API 位掩码 / 口径掩码 / timing 下标定义在各自的 def.h（12/13 号同值
// 同义，但分属 UrmaXxxExample 名字空间）。公共头不回引样例头（utils 不依赖
// 单个样例），自带一套带 PERF_ROW_ 前缀的定义——与 PERF_WIRE_* 同一防撞思路：
// 前缀不与任何样例常量重名（含 11 号的 PERF_API_*），样例侧把 examples 名字
// 空间整体 using 进来也安全。调用方直接传样例命令行里的掩码值即可。
constexpr uint32_t PERF_ROW_API_WRITE = 1U << 0;   // WriteNbi
constexpr uint32_t PERF_ROW_API_READ = 1U << 1;    // ReadNbi
constexpr uint32_t PERF_ROW_API_NOTIFY = 1U << 2;  // WriteWithNotifyNbi
constexpr uint32_t PERF_ROW_METRIC_LAT = 1U << 0;  // 时延口径
constexpr uint32_t PERF_ROW_METRIC_BW = 1U << 1;   // 带宽口径
constexpr uint32_t PERF_ROW_LAT_CYCLES_INDEX = 1U; // timing[] 时延档 cycles 下标
constexpr uint32_t PERF_ROW_BW_CYCLES_INDEX = 2U;  // timing[] 带宽档 cycles 下标
// 计数器换算：Ascend950 1GHz，1000 cycles = 1us（换 SOC 需同步改样例侧 def.h）
constexpr uint64_t PERF_ROW_CYCLES_PER_US = 1000ULL;

// apiBit → PERF_DATA 的 API 列名（"WriteNbi" 等）。
inline const char* PerfApiBitName(uint32_t apiBit)
{
    return apiBit == PERF_ROW_API_WRITE ? "WriteNbi" : (apiBit == PERF_ROW_API_READ ? "ReadNbi" : "WriteWithNotifyNbi");
}

// ---------------- PERF_DATA 行与 csv 落盘 ----------------

// 一行 PERF_DATA：口径 lat=提交时延（不含 Drain）/ bw=端到端带宽（含 Drain）。
// 数值含义由调用方保证（avgCycles 等），本函数只管格式。
// 小数位：AvgCycles / Bandwidth 4 位，SingleCoreTime 6 位。
inline void PrintPerfRow(const PerfRow& row)
{
    std::cout << std::fixed << "PERF_DATA | Rank=" << row.rank << " | API=" << row.api << " | DataSize/B=" << row.len
              << " | BatchSize=" << row.batch << " | Commit=" << row.commit << " | Topology=" << row.topology
              << " | Metric=" << row.drain << " | Rounds=" << row.samples << " | Cycles=" << row.cycles
              << " | AvgCycles=" << std::setprecision(4) << row.avgCycles
              << " | SingleCoreTime/us=" << std::setprecision(6) << row.singleCoreUs
              << " | Bandwidth/GB/s=" << std::setprecision(4) << row.bandwidthGbs << std::endl;
}

// 从 kernel timing 据实造行并打印：每个「操作 × 口径」组合一行（metricMask 选中
// 的口径各一行），压入 rows。apiMask / metricMask 直接用样例命令行里的掩码值，
// 位定义同上面的 PERF_ROW_* 常量（两样例 def.h 同值同义）。
// rounds：Rounds 列显示值（12 号 = 计时轮数，13 号 = 批轮次）；
// batch：BatchSize 列（无批维度的样例传 1 占位）。样本数 = rounds × batch
// （12 号 batch=1 时即 rounds），一个样本 = 一条请求，作 avgCycles 的分母。
// commitName / topologyName：提交方式与拓扑名（同 GatherPerfRowsToRoot 的
// 重导参数，两卡恒同）。
// kernel 失败时调用方不进本函数（timing 无效）。
inline void CollectPerfRows(
    const uint64_t (&timing)[3], uint32_t rank, uint32_t apiMask, uint32_t metricMask, uint32_t rounds, uint64_t len,
    uint32_t batch, const char* commitName, const char* topologyName, std::vector<PerfRow>& rows)
{
    for (uint32_t apiBit = PERF_ROW_API_WRITE; apiBit <= PERF_ROW_API_NOTIFY; apiBit <<= 1) {
        if ((apiMask & apiBit) == 0U) {
            continue;
        }
        if ((metricMask & PERF_ROW_METRIC_LAT) != 0U) {
            PerfRow row{};
            row.rank = rank;
            row.api = PerfApiBitName(apiBit);
            row.len = len;
            row.batch = batch;
            row.commit = commitName;
            row.topology = topologyName;
            row.drain = "lat";
            row.samples = rounds;
            row.cycles = timing[PERF_ROW_LAT_CYCLES_INDEX];
            row.avgCycles = static_cast<double>(row.cycles) / (static_cast<double>(rounds) * batch);
            row.singleCoreUs = row.avgCycles / PERF_ROW_CYCLES_PER_US;
            row.bandwidthGbs = row.avgCycles > 0.0 ? static_cast<double>(len) / row.singleCoreUs / 1000.0 : 0.0;
            PrintPerfRow(row);
            rows.push_back(row);
        }
        if ((metricMask & PERF_ROW_METRIC_BW) != 0U) {
            PerfRow row{};
            row.rank = rank;
            row.api = PerfApiBitName(apiBit);
            row.len = len;
            row.batch = batch;
            row.commit = commitName;
            row.topology = topologyName;
            row.drain = "bw";
            row.samples = rounds;
            row.cycles = timing[PERF_ROW_BW_CYCLES_INDEX];
            row.avgCycles = static_cast<double>(row.cycles) / (static_cast<double>(rounds) * batch);
            row.singleCoreUs = row.avgCycles / PERF_ROW_CYCLES_PER_US;
            row.bandwidthGbs = row.avgCycles > 0.0 ? static_cast<double>(len) / row.singleCoreUs / 1000.0 : 0.0;
            PrintPerfRow(row);
            rows.push_back(row);
        }
    }
}

// rows 落盘为 csv（rank 0 调用；列与 PERF_DATA 行一一对应）。
inline void WritePerfCsv(const std::vector<PerfRow>& rows, const std::string& path)
{
    std::ofstream csv(path);
    if (!csv.is_open()) {
        std::cerr << "[rank ?] cannot open " << path << " for writing, skip csv export" << std::endl;
        return;
    }
    csv << "rank,api,DataSize/B,batch,commit,topology,metric,rounds,cycles,avg_cycles,SingleCoreTime/us,"
           "Bandwidth/GB/s\n";
    for (const PerfRow& row : rows) {
        csv << row.rank << "," << row.api << "," << row.len << "," << row.batch << "," << row.commit << ","
            << row.topology << "," << row.drain << "," << row.samples << "," << row.cycles << "," << std::fixed
            << std::setprecision(4) << row.avgCycles << "," << std::setprecision(6) << row.singleCoreUs << ","
            << std::setprecision(4) << row.bandwidthGbs << "\n";
    }
}

// ---------------- 跨 rank 汇聚 ----------------

// 掩码位计数（apiMask / metricMask → 选中维度数，推容量上界用）。
inline uint32_t Popcount32(uint32_t v)
{
    uint32_t n = 0U;
    while (v != 0U) {
        n += v & 1U;
        v >>= 1U;
    }
    return n;
}

// 各 rank 终局交换结果行：rank 0 把其他 rank 的有效行追加到自己 rows 尾部（本
// rank 块在前），随后由调用方落盘一份全 rank 的 csv。集合操作，各 rank 都必须
// 无条件到达本调用——正常跑完与受控失败（全体同步停）都会到；环境错误提前
// 退出的一侧不在，对端 Allgather 超时（60s）返回 false，调用方退回只写自己的行。
// capacity：行容量上界，两卡按同一命令行推出（如 档数 × 选中接口数 × 选中口径数）。
// commitName / topologyName：两卡恒同（同一命令行），rank 0 收侧重导（传入本样例
// 的名字，不占线上字节）。
inline bool GatherPerfRowsToRoot(
    RankSyncContext& sync, uint32_t rank, uint32_t nranks, size_t capacity, const char* commitName,
    const char* topologyName, std::vector<PerfRow>& rows)
{
    std::vector<uint8_t> sendBuf(sizeof(PerfWireHeader) + capacity * sizeof(PerfRowWire), 0);
    auto* sendHeader = reinterpret_cast<PerfWireHeader*>(sendBuf.data());
    sendHeader->rowCount = static_cast<uint32_t>(rows.size() < capacity ? rows.size() : capacity);
    auto* sendWire = reinterpret_cast<PerfRowWire*>(sendBuf.data() + sizeof(PerfWireHeader));
    for (size_t i = 0; i < rows.size() && i < capacity; ++i) {
        const PerfRow& row = rows[i];
        sendWire[i].len = row.len;
        sendWire[i].cycles = row.cycles;
        sendWire[i].avgCycles = row.avgCycles;
        sendWire[i].singleCoreUs = row.singleCoreUs;
        sendWire[i].bandwidthGbs = row.bandwidthGbs;
        sendWire[i].rank = row.rank;
        sendWire[i].batch = row.batch;
        sendWire[i].samples = row.samples;
        sendWire[i].api = PerfApiNameToWire(row.api);
        sendWire[i].drain = PerfDrainNameToWire(row.drain);
    }

    const uint32_t payload = static_cast<uint32_t>(sendBuf.size());
    std::vector<uint8_t> recvBuf(static_cast<size_t>(nranks) * payload, 0);
    if (!sync.Allgather(kTagPerfResult, sendBuf.data(), payload, recvBuf.data())) {
        return false;
    }

    if (rank != 0U) {
        return true; // 全表只有 rank 0 落盘要用；非 0 rank 到此即完成集合义务
    }
    for (uint32_t r = 0U; r < nranks; ++r) {
        if (r == rank) {
            continue;
        }
        const uint8_t* slot = recvBuf.data() + static_cast<size_t>(r) * payload;
        const auto* header = reinterpret_cast<const PerfWireHeader*>(slot);
        const auto* wire = reinterpret_cast<const PerfRowWire*>(slot + sizeof(PerfWireHeader));
        const uint32_t count =
            header->rowCount < static_cast<uint32_t>(capacity) ? header->rowCount : static_cast<uint32_t>(capacity);
        for (uint32_t i = 0U; i < count; ++i) {
            PerfRow row{};
            row.rank = wire[i].rank;
            row.api = PerfApiWireToName(wire[i].api);
            row.len = wire[i].len;
            row.batch = wire[i].batch;
            row.commit = commitName;
            row.topology = topologyName;
            row.drain = PerfDrainWireToName(wire[i].drain);
            row.samples = wire[i].samples;
            row.cycles = wire[i].cycles;
            row.avgCycles = wire[i].avgCycles;
            row.singleCoreUs = wire[i].singleCoreUs;
            row.bandwidthGbs = wire[i].bandwidthGbs;
            rows.push_back(row);
        }
    }
    return true;
}

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_PERF_RESULT_H
