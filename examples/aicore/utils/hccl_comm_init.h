/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file hccl_comm_init.h
 * \brief 基于RankSyncContext的HCCL通信域创建工具。
 *
 * rank_sync.h只提供纯TCP的rank间信息共享与同步，不感知传输内容；本工具在其上组合出
 * 样例公共的"root info交换 + 创建通信域"流程：
 *   - rank 0调用HcclGetRootInfo生成root info；
 *   - 全体经RankSyncContext::Allgather交换，得到一致的root info表；
 *   - 各rank调用HcclCommInitRootInfo(Config)创建通信域。
 *
 * Allgather失败与HCCL建域失败分开打印，HCCL错误码原样透传，便于定位。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_HCCL_COMM_INIT_H
#define ASC_COMM_EXAMPLES_UTILS_HCCL_COMM_INIT_H

#include <cstdio>
#include <vector>

#include "hccl/hccl.h"
#include "hccl/hccl_comm.h"

#include "utils/rank_sync.h"

namespace examples {

// root info交换使用的allgather tag，取"RTNI"魔数避免与样例自定义tag（kTagUserBase起）冲突。
constexpr uint32_t kTagRootInfo = 0x52544E49U;

// root info交换并创建通信域。返回HcclResult，HCCL建域错误码原样透传。
inline HcclResult InitCommByRootInfo(RankSyncContext& sync, HcclComm* comm)
{
    HcclRootInfo info = {};
    if (sync.Rank() == 0U) {
        HcclResult ret = HcclGetRootInfo(&info);
        if (ret != HCCL_SUCCESS) {
            return ret;
        }
    }
    std::vector<HcclRootInfo> all(sync.Nranks());
    if (!sync.Allgather(kTagRootInfo, &info, sizeof(info), all.data())) {
        std::fprintf(stderr, "[ERROR] rank %u: root info allgather failed\n", sync.Rank());
        return HCCL_E_INTERNAL;
    }
    // 全体一致使用rank 0的root info。
    return HcclCommInitRootInfo(sync.Nranks(), &all[0], sync.Rank(), comm);
}

// root info交换并创建通信域（带config版本）。
inline HcclResult InitCommByRootInfoConfig(RankSyncContext& sync, HcclCommConfig* config, HcclComm* comm)
{
    HcclRootInfo info = {};
    if (sync.Rank() == 0U) {
        HcclResult ret = HcclGetRootInfo(&info);
        if (ret != HCCL_SUCCESS) {
            return ret;
        }
    }
    std::vector<HcclRootInfo> all(sync.Nranks());
    if (!sync.Allgather(kTagRootInfo, &info, sizeof(info), all.data())) {
        std::fprintf(stderr, "[ERROR] rank %u: root info allgather failed\n", sync.Rank());
        return HCCL_E_INTERNAL;
    }
    // 全体一致使用rank 0的root info。
    return HcclCommInitRootInfoConfig(sync.Nranks(), &all[0], sync.Rank(), config, comm);
}

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_HCCL_COMM_INIT_H
