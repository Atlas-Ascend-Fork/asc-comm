/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_UT_FIXTURE_H
#define ASCCOMM_UT_FIXTURE_H

#include "gtest/gtest.h"

#include "asccomm_primitives.h"
#include "hcomm_res_entity_defs.h"
#include "asccomm_runtime_stub.h"

namespace asccomm_ut {

inline HcommThreadEntity MakeThread()
{
    HcommThreadEntity thread{};
    thread.abiHeader = {HCOMM_THREAD_ENTITY_VERSION, HCOMM_THREAD_ENTITY_MAGIC_WORD, sizeof(HcommThreadEntity), 0};
    static HcommThreadSqContext rtsq{};
    thread.sqContextAddr = &rtsq;
    return thread;
}

inline ChannelEntity MakeChannel()
{
    ChannelEntity channel{};
    channel.abiHeader = {HCOMM_CHANNEL_ENTITY_VERSION, HCOMM_CHANNEL_ENTITY_MAGIC_WORD, sizeof(ChannelEntity), 0};
    return channel;
}

inline ThreadHandle ThreadHandleOf(HcommThreadEntity& thread) { return reinterpret_cast<ThreadHandle>(&thread); }

inline ChannelHandle ChannelHandleOf(ChannelEntity& channel) { return reinterpret_cast<ChannelHandle>(&channel); }

class AsccommPrimitivesTest : public testing::Test {
protected:
    void SetUp() override
    {
        g_runtimeStubState = {};
        asccomm_batch_mode_end(nullptr);
        g_runtimeStubState = {};
    }

    HcommThreadEntity thread_{MakeThread()};
    ChannelEntity channel_{MakeChannel()};
};

} // namespace asccomm_ut

#endif // ASCCOMM_UT_FIXTURE_H
