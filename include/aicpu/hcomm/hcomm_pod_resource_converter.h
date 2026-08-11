/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef HCOMM_POD_RESOURCE_CONVERTER_H
#define HCOMM_POD_RESOURCE_CONVERTER_H

#include "hcomm_res_defs.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/**
 * @brief 将控制面申请到的legacy ThreadHandle转换为POD ThreadHandle
 * @param[in] legacyThread legacy thread句柄，由HcclThreadAcquire等接口产出
 * @param[out] podThread 指向HcommThreadEntity的句柄，可直接传给Hcomm*OnThreadPod原语
 * @return int32_t 执行结果状态码
 * @note 1、入参已是POD句柄时原样返回；
 *       2、同一legacy句柄多次转换返回同一entity，转换结果由库内缓存并持有；
 *       3、转换出的entity在其legacy资源释放后失效，调用方不得跨资源生命周期使用。
 *
 * WARNING: experimental API, No compatibility is currently guaranteed for this API
 */
extern int32_t HcommThreadConvertToPod(ThreadHandle legacyThread, ThreadHandle *podThread);

/**
 * @brief 将控制面申请到的legacy ChannelHandle转换为POD ChannelHandle
 * @param[in] legacyChannel legacy channel句柄，由HcclChannelAcquire等接口产出
 * @param[out] podChannel 指向HcommChannelEntity的句柄，可直接传给Hcomm*OnThreadPod原语
 * @return int32_t 执行结果状态码
 * @note 1、入参已是POD句柄时原样返回；
 *       2、同一legacy句柄多次转换返回同一entity，转换结果由库内缓存并持有；
 *       3、当前仅支持AICPU_TS引擎上的UB系列协议channel。
 *
 * WARNING: experimental API, No compatibility is currently guaranteed for this API
 */
extern int32_t HcommChannelConvertToPod(ChannelHandle legacyChannel, ChannelHandle *podChannel);

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // HCOMM_POD_RESOURCE_CONVERTER_H
