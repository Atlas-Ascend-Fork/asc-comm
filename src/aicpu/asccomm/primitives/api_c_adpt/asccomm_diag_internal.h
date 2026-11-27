/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_DIAG_INTERNAL_H
#define ASCCOMM_DIAG_INTERNAL_H

// 诊断探针内部导出头：卡死窗口 dump SQ 现场（只读）。devkit mc2 server 经弱符号桥调用，
// 未链接时自动跳过；本符号恒导出（不随 ASCCOMM_ENABLE_INTERNAL_APIS 裁剪，靠符号名解析）。
#include "asccomm_primitives.h"

#ifdef __cplusplus
extern "C" {
#endif

// 恒开无开关，只在 server 退避路径被调（正常路径零成本），单线程语义（调用方逐线程遍历）；
// 输出 12 条 SQE hex + [Head] 解码 + [Summary] 硬件 query + [Mirror] 软件镜像对账；
// 失败返回 ASCCOMM_E_*（调用方应忽略，退避路径不可被打断）。
ASCCOMM_API int32_t asccomm_diag_dump_sq(uint64_t threadHandle);

#ifdef __cplusplus
}
#endif

#endif // ASCCOMM_DIAG_INTERNAL_H
