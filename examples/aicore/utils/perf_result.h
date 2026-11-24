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
struct perf_row {
    uint32_t rank;
    const char* api;      // "write_nbi" / "read_nbi" / "write_with_notify_nbi"
    uint64_t len;         // DataSize/B
    uint32_t batch;       // BatchSize（无批维度的样例恒填 1，csv 照写）
    const char* commit;   // 提交方式名（各样例自己的 "inline"/"final" 等）
    const char* topology; // 拓扑名："p2p" / "ring"（公共 perf_topology.h）
    const char* drain;    // "lat" / "bw" / "commit-lat"
    uint32_t samples;     // 计时轮数（rounds）
    uint64_t cycles;      // 计时段原始 cycles
    double avg_cycles;
    double single_core_us;
    double bandwidth_gbs;
};

// ---------------- 线上格式（跨 rank 传输的定长编码） ----------------

// allgather 是等长载荷协议：行集打包成「头 + 定容量行」。容量由两卡一致的档位表
// × 接口/口径掩码推出（命令行相同 → 表相同），无需协商；commit 列两卡恒同
// （来自同一份命令行 opt），不编码、rank 0 收侧重导；api / drain 是有限枚举，
// 编成小整数。各 rank 由同一二进制 fork、ABI 相同，可直接按结构体内存布局传输。
constexpr uint32_t PERF_WIRE_API_WRITE = 0U;
constexpr uint32_t PERF_WIRE_API_READ = 1U;
constexpr uint32_t PERF_WIRE_API_NOTIFY = 2U;
constexpr uint32_t PERF_WIRE_DRAIN_LAT = 0U;
constexpr uint32_t PERF_WIRE_DRAIN_BW = 1U;
constexpr uint32_t PERF_WIRE_DRAIN_COMMIT_LAT = 2U;

struct perf_row_wire {
    uint64_t len;    // DataSize/B
    uint64_t cycles; // 计时段原始 cycles
    double avg_cycles;
    double single_core_us;
    double bandwidth_gbs;
    uint32_t rank;
    uint32_t batch;   // BatchSize
    uint32_t samples; // 计时轮数
    uint32_t api;     // PERF_WIRE_API_*
    uint32_t drain;   // PERF_WIRE_DRAIN_*
};

struct perf_wire_header {
    uint32_t row_count; // 有效行数（≤ 容量，失败档位不产生行）
    uint32_t reserved;
};

// ---------------- 枚举名 ↔ 线上编码 ----------------

inline uint32_t perf_api_name_to_wire(const char* name)
{
    if (std::strcmp(name, "write_nbi") == 0) {
        return PERF_WIRE_API_WRITE;
    }
    return std::strcmp(name, "read_nbi") == 0 ? PERF_WIRE_API_READ : PERF_WIRE_API_NOTIFY;
}

inline const char* perf_api_wire_to_name(uint32_t code)
{
    switch (code) {
        case PERF_WIRE_API_WRITE:
            return "write_nbi";
        case PERF_WIRE_API_READ:
            return "read_nbi";
        default:
            return "write_with_notify_nbi";
    }
}

inline uint32_t perf_drain_name_to_wire(const char* name)
{
    if (std::strcmp(name, "lat") == 0) {
        return PERF_WIRE_DRAIN_LAT;
    }
    return std::strcmp(name, "commit-lat") == 0 ? PERF_WIRE_DRAIN_COMMIT_LAT : PERF_WIRE_DRAIN_BW;
}

inline const char* perf_drain_wire_to_name(uint32_t code)
{
    return code == PERF_WIRE_DRAIN_LAT ? "lat" : (code == PERF_WIRE_DRAIN_COMMIT_LAT ? "commit-lat" : "bw");
}

// ---------------- 造行常量（与 12/13 号样例 def.h 同值同义） ----------------

// 样例的 API 位掩码 / 口径掩码 / timing 下标定义在各自的 def.h（12/13 号同值
// 同义，但分属 urma_xxx_example 名字空间）。公共头不回引样例头（utils 不依赖
// 单个样例），自带一套带 PERF_ROW_ 前缀的定义——与 PERF_WIRE_* 同一防撞思路：
// 前缀不与任何样例常量重名（含 11 号的 PERF_API_*），样例侧把 examples 名字
// 空间整体 using 进来也安全。调用方直接传样例命令行里的掩码值即可。
constexpr uint32_t PERF_ROW_API_WRITE = 1U << 0;      // write_nbi
constexpr uint32_t PERF_ROW_API_READ = 1U << 1;       // read_nbi
constexpr uint32_t PERF_ROW_API_NOTIFY = 1U << 2;     // write_with_notify_nbi
constexpr uint32_t PERF_ROW_METRIC_LAT = 1U << 0;     // 时延口径
constexpr uint32_t PERF_ROW_METRIC_BW = 1U << 1;      // 带宽口径
constexpr uint32_t PERF_ROW_LAT_CYCLES_INDEX = 1U;    // timing[] 时延档 cycles 下标
constexpr uint32_t PERF_ROW_BW_CYCLES_INDEX = 2U;     // timing[] 带宽档 cycles 下标
constexpr uint32_t PERF_ROW_COMMIT_CYCLES_INDEX = 3U; // timing[] split 时 batch_commit cycles 下标
// 计数器换算：Ascend950 1GHz，1000 cycles = 1us（换 SOC 需同步改样例侧 def.h）
constexpr uint64_t PERF_ROW_CYCLES_PER_US = 1000ULL;

// api_bit → PERF_DATA 的 API 列名（"write_nbi" 等）。
inline const char* perf_api_bit_name(uint32_t api_bit)
{
    return api_bit == PERF_ROW_API_WRITE ? "write_nbi" :
                                           (api_bit == PERF_ROW_API_READ ? "read_nbi" : "write_with_notify_nbi");
}

// ---------------- PERF_DATA 行与 csv 落盘 ----------------

// 一行 PERF_DATA：口径 lat=提交时延（不含 Drain）/ bw=端到端带宽（含 Drain）。
// 数值含义由调用方保证（avg_cycles 等），本函数只管格式。
// 小数位：AvgCycles / Bandwidth 4 位，SingleCoreTime 6 位。
inline void print_perf_row(const perf_row& row)
{
    std::cout << std::fixed << "PERF_DATA | Rank=" << row.rank << " | API=" << row.api << " | DataSize/B=" << row.len
              << " | BatchSize=" << row.batch << " | Commit=" << row.commit << " | Topology=" << row.topology
              << " | Metric=" << row.drain << " | Rounds=" << row.samples << " | Cycles=" << row.cycles
              << " | AvgCycles=" << std::setprecision(4) << row.avg_cycles
              << " | SingleCoreTime/us=" << std::setprecision(6) << row.single_core_us
              << " | Bandwidth/GB/s=" << std::setprecision(4) << row.bandwidth_gbs << std::endl;
}

// 从 kernel timing 据实造行并打印：每个「操作 × 口径」组合一行（metric_mask 选中
// 的口径各一行），压入 rows。api_mask / metric_mask 直接用样例命令行里的掩码值，
// 位定义同上面的 PERF_ROW_* 常量（两样例 def.h 同值同义）。
// rounds：Rounds 列显示值（12 号 = 计时轮数，13 号 = 批轮次）；
// batch：BatchSize 列（无批维度的样例传 1 占位）。样本数 = rounds × batch
// （12 号 batch=1 时即 rounds），一个样本 = 一条请求，作 avg_cycles 的分母。
// commit_name / topology_name：提交方式与拓扑名（同 gather_perf_rows_to_root 的
// 重导参数，两卡恒同）。
// commit_latency=true（13 号 split 提交方式）时额外生成 commit-lat 行：一个样本 =
// 一次 batch_commit 调用（样本数 = rounds），不派生带宽（固定输出 0）。
// kernel 失败时调用方不进本函数（timing 无效）。
template <size_t timing_words>
inline void collect_perf_rows(
    const uint64_t (&timing)[timing_words], uint32_t rank, uint32_t api_mask, uint32_t metric_mask, uint32_t rounds,
    uint64_t len, uint32_t batch, const char* commit_name, const char* topology_name, std::vector<perf_row>& rows,
    bool commit_latency = false)
{
    for (uint32_t api_bit = PERF_ROW_API_WRITE; api_bit <= PERF_ROW_API_NOTIFY; api_bit <<= 1) {
        if ((api_mask & api_bit) == 0U) {
            continue;
        }
        if ((metric_mask & PERF_ROW_METRIC_LAT) != 0U) {
            perf_row row{};
            row.rank = rank;
            row.api = perf_api_bit_name(api_bit);
            row.len = len;
            row.batch = batch;
            row.commit = commit_name;
            row.topology = topology_name;
            row.drain = "lat";
            row.samples = rounds;
            row.cycles = timing[PERF_ROW_LAT_CYCLES_INDEX];
            row.avg_cycles = static_cast<double>(row.cycles) / (static_cast<double>(rounds) * batch);
            row.single_core_us = row.avg_cycles / PERF_ROW_CYCLES_PER_US;
            row.bandwidth_gbs = row.avg_cycles > 0.0 ? static_cast<double>(len) / row.single_core_us / 1000.0 : 0.0;
            print_perf_row(row);
            rows.push_back(row);
        }
        if ((metric_mask & PERF_ROW_METRIC_BW) != 0U) {
            perf_row row{};
            row.rank = rank;
            row.api = perf_api_bit_name(api_bit);
            row.len = len;
            row.batch = batch;
            row.commit = commit_name;
            row.topology = topology_name;
            row.drain = "bw";
            row.samples = rounds;
            row.cycles = timing[PERF_ROW_BW_CYCLES_INDEX];
            row.avg_cycles = static_cast<double>(row.cycles) / (static_cast<double>(rounds) * batch);
            row.single_core_us = row.avg_cycles / PERF_ROW_CYCLES_PER_US;
            row.bandwidth_gbs = row.avg_cycles > 0.0 ? static_cast<double>(len) / row.single_core_us / 1000.0 : 0.0;
            print_perf_row(row);
            rows.push_back(row);
        }
        if constexpr (timing_words > PERF_ROW_COMMIT_CYCLES_INDEX) {
            if (commit_latency && (metric_mask & PERF_ROW_METRIC_LAT) != 0U) {
                perf_row row{};
                row.rank = rank;
                row.api = perf_api_bit_name(api_bit);
                row.len = len;
                row.batch = batch;
                row.commit = commit_name;
                row.topology = topology_name;
                row.drain = "commit-lat";
                row.samples = rounds;
                row.cycles = timing[PERF_ROW_COMMIT_CYCLES_INDEX];
                row.avg_cycles = static_cast<double>(row.cycles) / static_cast<double>(rounds);
                row.single_core_us = row.avg_cycles / PERF_ROW_CYCLES_PER_US;
                row.bandwidth_gbs = 0.0;
                print_perf_row(row);
                rows.push_back(row);
            }
        }
    }
}

// rows 落盘为 csv（rank 0 调用；列与 PERF_DATA 行一一对应）。
inline void write_perf_csv(const std::vector<perf_row>& rows, const std::string& path)
{
    std::ofstream csv(path);
    if (!csv.is_open()) {
        std::cerr << "[rank ?] cannot open " << path << " for writing, skip csv export" << std::endl;
        return;
    }
    csv << "rank,api,DataSize/B,batch,commit,topology,metric,rounds,cycles,avg_cycles,SingleCoreTime/us,"
           "Bandwidth/GB/s\n";
    for (const perf_row& row : rows) {
        csv << row.rank << "," << row.api << "," << row.len << "," << row.batch << "," << row.commit << ","
            << row.topology << "," << row.drain << "," << row.samples << "," << row.cycles << "," << std::fixed
            << std::setprecision(4) << row.avg_cycles << "," << std::setprecision(6) << row.single_core_us << ","
            << std::setprecision(4) << row.bandwidth_gbs << "\n";
    }
}

// ---------------- 跨 rank 汇聚 ----------------

// 掩码位计数（api_mask / metric_mask → 选中维度数，推容量上界用）。
inline uint32_t popcount32(uint32_t v)
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
// 退出的一侧不在，对端 allgather 超时（60s）返回 false，调用方退回只写自己的行。
// capacity：行容量上界，两卡按同一命令行推出（如 档数 × 选中接口数 × 选中口径数）。
// commit_name / topology_name：两卡恒同（同一命令行），rank 0 收侧重导（传入本样例
// 的名字，不占线上字节）。
inline bool gather_perf_rows_to_root(
    rank_sync_context& sync, uint32_t rank, uint32_t nranks, size_t capacity, const char* commit_name,
    const char* topology_name, std::vector<perf_row>& rows)
{
    std::vector<uint8_t> send_buf(sizeof(perf_wire_header) + capacity * sizeof(perf_row_wire), 0);
    auto* send_header = reinterpret_cast<perf_wire_header*>(send_buf.data());
    send_header->row_count = static_cast<uint32_t>(rows.size() < capacity ? rows.size() : capacity);
    auto* send_wire = reinterpret_cast<perf_row_wire*>(send_buf.data() + sizeof(perf_wire_header));
    for (size_t i = 0; i < rows.size() && i < capacity; ++i) {
        const perf_row& row = rows[i];
        send_wire[i].len = row.len;
        send_wire[i].cycles = row.cycles;
        send_wire[i].avg_cycles = row.avg_cycles;
        send_wire[i].single_core_us = row.single_core_us;
        send_wire[i].bandwidth_gbs = row.bandwidth_gbs;
        send_wire[i].rank = row.rank;
        send_wire[i].batch = row.batch;
        send_wire[i].samples = row.samples;
        send_wire[i].api = perf_api_name_to_wire(row.api);
        send_wire[i].drain = perf_drain_name_to_wire(row.drain);
    }

    const uint32_t payload = static_cast<uint32_t>(send_buf.size());
    std::vector<uint8_t> recv_buf(static_cast<size_t>(nranks) * payload, 0);
    if (!sync.allgather(TAG_PERF_RESULT, send_buf.data(), payload, recv_buf.data())) {
        return false;
    }

    if (rank != 0U) {
        return true; // 全表只有 rank 0 落盘要用；非 0 rank 到此即完成集合义务
    }
    for (uint32_t r = 0U; r < nranks; ++r) {
        if (r == rank) {
            continue;
        }
        const uint8_t* slot = recv_buf.data() + static_cast<size_t>(r) * payload;
        const auto* header = reinterpret_cast<const perf_wire_header*>(slot);
        const auto* wire = reinterpret_cast<const perf_row_wire*>(slot + sizeof(perf_wire_header));
        const uint32_t count =
            header->row_count < static_cast<uint32_t>(capacity) ? header->row_count : static_cast<uint32_t>(capacity);
        for (uint32_t i = 0U; i < count; ++i) {
            perf_row row{};
            row.rank = wire[i].rank;
            row.api = perf_api_wire_to_name(wire[i].api);
            row.len = wire[i].len;
            row.batch = wire[i].batch;
            row.commit = commit_name;
            row.topology = topology_name;
            row.drain = perf_drain_wire_to_name(wire[i].drain);
            row.samples = wire[i].samples;
            row.cycles = wire[i].cycles;
            row.avg_cycles = wire[i].avg_cycles;
            row.single_core_us = wire[i].single_core_us;
            row.bandwidth_gbs = wire[i].bandwidth_gbs;
            rows.push_back(row);
        }
    }
    return true;
}

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_PERF_RESULT_H
