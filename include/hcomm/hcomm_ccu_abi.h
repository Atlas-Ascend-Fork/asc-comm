/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file hcomm_ccu_abi.h
 * @brief CCU 跨 SO（hcomm ↔ asc-comm）POD 结构的统一 ABI 头。
 *
 * 所有跨动态库传递的 POD 均以 CommAbiHeader 开头：
 * version 标识 ABI 版本、magicWord 标识结构类型、size 标识结构体大小，
 * 用于在读取业务字段前快速校验两端布局是否兼容，避免二进制不兼容导致的内存越界。
 */
#ifndef HCOMM_CCU_ABI_H
#define HCOMM_CCU_ABI_H

/** 支持的最大 DIE 数量 */
enum {
    HCOMM_CCU_MAX_DIE_NUM = 2,
};

#endif // HCOMM_CCU_ABI_H
