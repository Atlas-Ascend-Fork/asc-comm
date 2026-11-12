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
 * \file perf_topology.h
 * \brief Ascend C样例共享的通信拓扑工具（12/13 号 URMA 性能样例共用）。
 *
 * 把「--ranks N + --perf_type p2p|ring」翻译成每个 rank 的通信角色（RankTopology），
 * 拓扑语义见各使用样例的 README 与 ParsePerfType 处注释。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_PERF_TOPOLOGY_H
#define ASC_COMM_EXAMPLES_UTILS_PERF_TOPOLOGY_H

#include <cstdint>
#include <cstring>

namespace examples {

// ---------------- 拓扑类型（--perf_type） ----------------
enum class PerfType : uint32_t {
    P2P = 0, // 两卡互打（当前样例的原生形态）
    RING = 1 // 环形收发：发给下一卡、从上一卡拉
};

// 命令行文本 → 拓扑类型（"p2p"/"ring"），非法文本返回 false。
inline bool ParsePerfType(const char* text, PerfType& type)
{
    if (text == nullptr) {
        return false;
    }
    if (std::strcmp(text, "p2p") == 0) {
        type = PerfType::P2P;
        return true;
    }
    if (std::strcmp(text, "ring") == 0) {
        type = PerfType::RING;
        return true;
    }
    return false;
}

// 拓扑类型名（csv / PERF_DATA 行用，与 --perf_type 的合法取值一一对应）。
inline const char* PerfTypeName(PerfType type) { return type == PerfType::P2P ? "p2p" : "ring"; }

// 一个 rank 在给定拓扑下的通信角色（由 rank / nranks / 拓扑类型决定）：
struct RankTopology {
    bool active{false};   // 是否参与通信（p2p 且 rank >= 2 时为 false，见上）
    uint32_t sendRank{0}; // write/notify 的目标卡（数据发往的卡）
    uint32_t recvRank{0}; // read 的数据来源卡；同时也是写进本卡落点区的卡
};

// 推导本 rank 的通信角色：
//   p2p ：active = rank < 2，send = recv = 1 - rank；
//   ring：全员 active，send = (rank+1)%nranks，recv = (rank+nranks-1)%nranks。
// nranks 由调用方保证 >= 2（命令行解析处校验），此处不再防御。
inline RankTopology BuildRankTopology(uint32_t rank, uint32_t nranks, PerfType type)
{
    RankTopology topo{};
    if (type == PerfType::RING) {
        topo.active = true;
        topo.sendRank = (rank + 1U) % nranks;
        topo.recvRank = (rank + nranks - 1U) % nranks;
        return topo;
    }
    if (rank < 2U) {
        topo.active = true;
        topo.sendRank = 1U - rank;
        topo.recvRank = 1U - rank;
    }
    return topo;
}

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_PERF_TOPOLOGY_H
