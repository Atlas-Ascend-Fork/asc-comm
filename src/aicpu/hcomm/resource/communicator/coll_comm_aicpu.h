/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */


#ifndef __COLL_COMM_AICPU_H__
#define __COLL_COMM_AICPU_H__

#include "aicpu_init_param.h"
#include "aicpu_topo_info.h"
#include "hcomm_primitives.h"
#include "thread.h"
#include "local_notify.h"
#include "ub_transport_lite_impl.h"
#include "p2p_transport_lite_impl.h"
#include "mem_transport_common.h"
#include "aicpu_launch_manager.h"
#include "channel_param.h"
#include <atomic>
#include <shared_mutex>
#include "roce_transport_lite_impl.h"
#include "hccl/hccl_types.h"

using namespace hccl;

class CollCommAicpu {
public:
    ~CollCommAicpu();
    HcclResult InitAicpuIndOp(CommAicpuParam *commAicpuParam);
    HcclResult InitThreads(ThreadMgrAicpuParam *param);
    HcclResult AllocChannelResource(HcclChannelUrmaRes *commParam);
    HcclResult NotifyFree(NotifyMgrAicpuParam *param);
    HcclResult NotifyAlloc(NotifyMgrAicpuParam *param);
    const std::vector<std::shared_ptr<Thread>>& GetAllThread() { return threads_; };
    const hccl::AicpuTopoInfo& GetTopoInfo() { return topoInfo_; }
    const std::string& GetIdentifier() { return identifier_; }

    std::shared_mutex& GetThreadMutex() { return threadMutex_; }

    u32 UpdateIndex();

    HcclCommStatus GetCommmStatus() { return commStatus_; }
    void SetCommmStatus(HcclCommStatus status);

    // N秒快恢
    HcclResult Clean();
    HcclResult Resume(HcclChannelUrmaRes *commParam);

private:
    // 初始化
    void InitIndopEnv(CommAicpuParam *commAicpuParam);

    HcclResult InitUrmaChannel(HcclChannelUrmaRes *commParam);
    HcclResult ParsePackData(std::vector<char> &data, ChannelHandle &handle);
    HcclResult ResumePackData(std::vector<char> &data, ChannelHandle &handle);
    HcclResult ProcessUrmaRes(HcclChannelUrmaRes *commParam, bool isInit);

    u32 devId_{0};
    std::string identifier_;
    HcclCommStatus commStatus_{HcclCommStatus::HCCL_COMM_STATUS_INVALID};
    hccl::AicpuTopoInfo topoInfo_;
    std::shared_mutex threadMutex_;
    std::vector<std::shared_ptr<Thread>> threads_;
    std::vector<std::unique_ptr<LocalNotify>> notifys_;
    // A5 独立算子
    std::unordered_map<ChannelHandle, std::unique_ptr<Hccl::UbTransportLiteImpl>> ubTransportMap_;
    std::unordered_map<ChannelHandle, std::unique_ptr<Hccl::P2PTransportLiteImpl>> p2pTransportMap_;
    std::unordered_map<ChannelHandle, std::unique_ptr<Hccl::RoceTransportLiteImpl>> roceTransportMap_;

    u32 index_{0};

};

#endif // __COLL_COMM_AICPU_H__
