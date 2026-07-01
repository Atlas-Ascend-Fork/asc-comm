/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/hcomm.h"
#include "kernel_operator.h"

namespace {
constexpr uint32_t HCOMM_TMP_BUF_LEN = 512;
}

extern "C" __global__ __aicore__ void hcomm_p2p_kernel(
    AscendC::ChannelHandle channel,
    GM_ADDR remoteDst,
    GM_ADDR localSrc,
    GM_ADDR localDst,
    GM_ADDR remoteSrc,
    GM_ADDR notifyAddr,
    uint64_t notifyVal,
    uint64_t len)
{
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> tmpBuf;
    pipe.InitBuffer(tmpBuf, HCOMM_TMP_BUF_LEN);

    AscendC::LocalTensor<uint8_t> tmp = tmpBuf.Get<uint8_t>();
    __ubuf__ uint8_t* tmpPtr = reinterpret_cast<__ubuf__ uint8_t*>(tmp.GetPhyAddr());

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
    if (hcomm.Init(tmpPtr, HCOMM_TMP_BUF_LEN) != 0) {
        return;
    }

    int32_t ret = hcomm.WriteNbi<false>(channel, remoteDst, localSrc, len);
    if (ret != 0) {
        return;
    }

    ret = hcomm.ReadNbi<false>(channel, localDst, remoteSrc, len);
    if (ret != 0) {
        return;
    }

    ret = hcomm.WriteWithNotifyNbi<false>(channel, remoteDst, localSrc, len, notifyAddr, notifyVal);
    if (ret != 0) {
        return;
    }

    ret = hcomm.Commit(channel);
    if (ret != 0) {
        return;
    }

    (void)hcomm.Drain(channel);
}
