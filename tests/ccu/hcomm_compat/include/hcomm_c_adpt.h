/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_UT_HCOMM_C_ADPT_H
#define ASCCOMM_CCU_UT_HCOMM_C_ADPT_H

#include <hccl/hccl_types.h>

#include "hcomm_primitives.h"

typedef HcclResult HcommResult;
HcommResult HcommChannelGet(ChannelHandle channelHandle, void **channel);

#endif
