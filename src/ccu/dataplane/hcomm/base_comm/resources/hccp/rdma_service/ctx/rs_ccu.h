/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef RS_CCU_H
#define RS_CCU_H

#include "ccu_u_api.h"
#include "rs.h"

int RsCtxCcuCustomChannel(const struct channel_info_in *in, struct channel_info_out *out);
int RsCtxCcuMissionKill(unsigned int dieId);
int RsCtxCcuMissionDone(unsigned int dieId);
#endif // RS_CCU_H
