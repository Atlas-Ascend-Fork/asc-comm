/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "user_log.h"
#include "dl_ccu_function.h"
#include "ccu_u_api.h"

#ifndef STATIC
#define STATIC static
#endif

int RsCtxCcuCustomChannel(const struct channel_info_in *in, struct channel_info_out *out)
{
    return RsCcuCustomChannel(in, out);
}

STATIC int RsCcuMissionExec(unsigned int udieId, ccu_u_opcode_t ccuOp)
{
    struct channel_info_out chanOut = {0};
    struct channel_info_in chanIn = {0};
    int ret = 0;

    chanIn.data.data_info.udie_idx = udieId;
    chanIn.op = ccuOp;
    ret = RsCtxCcuCustomChannel(&chanIn, &chanOut);
    CHK_PRT_RETURN(ret != 0, hccp_run_warn("ccu_custom_channel unsuccessful, ccuOp[%u], ret[%d] udieId[%u]",
        ccuOp, ret, udieId), ret);
    CHK_PRT_RETURN(chanOut.op_ret != 0, hccp_run_warn("ccu_u_op unsuccessful, ccuOp[%u], op_ret[%d] udieId[%u]",
        ccuOp, chanOut.op_ret, udieId), chanOut.op_ret);

    return 0;
}

int RsCtxCcuMissionKill(unsigned int dieId)
{
    return RsCcuMissionExec(dieId, CCU_U_OP_SET_TASKKILL);
}

int RsCtxCcuMissionDone(unsigned int dieId)
{
    return RsCcuMissionExec(dieId, CCU_U_OP_CLEAN_TASKKILL_STATE);
}
