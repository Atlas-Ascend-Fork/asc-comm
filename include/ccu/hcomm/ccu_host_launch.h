/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_HOST_LAUNCH_H
#define ASCCOMM_CCU_HOST_LAUNCH_H

#include <stddef.h>
#include <stdint.h>

#include "acl/acl_rt.h"
#include "ccu/hcomm/ccu_launch.h"

#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

enum {
    ASCCOMM_CCU_HOST_LAUNCH_ABI_VERSION = 1,
    ASCCOMM_CCU_HOST_LAUNCH_ATTRS_MAGIC_WORD = 0x43484C41U, // "CHLA"
};

typedef struct {
    uint32_t num_blocks;
    uint32_t reserved;
    uint64_t phy_die_mask;
    uint64_t binary_cache_tag;
} asccomm_ccu_schd;

typedef struct {
    asccomm_ccu_schd ccu_schd;
    CcuInsHandle ccu_ins;
    aclrtStream stream;
    /* Optional AsccommCcuHostLaunchAttrsV1 pointer; it is borrowed for this synchronous call only. */
    void *attrs;
} asccomm_launch_kernel_cfg;

/**
 * Versioned context consumed by the <<<>>> Host adapter.
 * registerContext remains owned by hcomm. launchContext is copied for each synchronous launch.
 */
typedef struct {
    HcommCcuAbiHeaderPod header;
    HcommCcuRegisterContextHandle registerContext;
    HcommCcuLaunchContextPod launchContext;
    uint64_t generation;
    uint64_t reserved[2];
} AsccommCcuHostLaunchAttrsV1;

/* Compatibility names used by existing compiler-generated Host stubs. */
typedef struct {
    uint32_t numBlocks;
    uint32_t reserved;
    uint64_t phyDieMask;
    uint64_t binaryCacheTag;
} HcommCcuSchd;

typedef struct {
    HcommCcuSchd ccuSchd;
    CcuInsHandle ccuIns;
    aclrtStream stream;
    void *attrs;
} HcommLaunchKernelCfg;

typedef CcuResult ccu_result;

/** Initialize a versioned attrs object from contexts supplied by hcomm. */
extern CcuResult asccomm_ccu_host_launch_attrs_init(HcommCcuRegisterContextHandle register_context,
    const HcommCcuLaunchContextPod *launch_context, AsccommCcuHostLaunchAttrsV1 *attrs);

/**
 * Bind a launch context to the current Host thread for compiler stubs that leave cfg->attrs null.
 * A non-null cfg->attrs takes precedence over this binding. Bind, launch, and unbind must run on the same thread.
 */
extern CcuResult asccomm_ccu_host_context_bind(const AsccommCcuHostLaunchAttrsV1 *attrs);
extern void asccomm_ccu_host_context_unbind(void);

/** Return the result of the most recent Host adapter launch on the current Host thread. */
extern CcuResult asccomm_ccu_host_get_last_error(void);

extern ccu_result asccomm_ccu_host_kernel_launch(
    const void *kernel_func, const asccomm_launch_kernel_cfg *cfg, void *args);
extern CcuResult HcommCcuHostKernelLaunch(
    const void *kernel_func, const HcommLaunchKernelCfg *cfg, void *args);

extern uint64_t asccomm_ccu_get_launch_hash_tag(const char *tag);
extern uint64_t HcommCcuGetLaunchHashTag(const char *tag);

#ifdef __cplusplus
}

static_assert(std::is_standard_layout<AsccommCcuHostLaunchAttrsV1>::value,
    "AsccommCcuHostLaunchAttrsV1 must be standard-layout");
static_assert(std::is_trivially_copyable<AsccommCcuHostLaunchAttrsV1>::value,
    "AsccommCcuHostLaunchAttrsV1 must be trivially copyable");
static_assert(sizeof(asccomm_ccu_schd) == 24, "asccomm_ccu_schd ABI size changed");
static_assert(sizeof(asccomm_launch_kernel_cfg) == 48, "asccomm_launch_kernel_cfg ABI size changed");
static_assert(offsetof(asccomm_launch_kernel_cfg, attrs) == 40,
    "asccomm_launch_kernel_cfg.attrs ABI offset changed");
static_assert(sizeof(HcommCcuSchd) == sizeof(asccomm_ccu_schd), "CCU schedule compatibility ABI changed");
static_assert(sizeof(HcommLaunchKernelCfg) == sizeof(asccomm_launch_kernel_cfg),
    "CCU launch config compatibility ABI changed");
static_assert(sizeof(AsccommCcuHostLaunchAttrsV1) == 136,
    "AsccommCcuHostLaunchAttrsV1 ABI size changed");
static_assert(offsetof(AsccommCcuHostLaunchAttrsV1, launchContext) == 24,
    "AsccommCcuHostLaunchAttrsV1.launchContext ABI offset changed");
#endif

#endif // ASCCOMM_CCU_HOST_LAUNCH_H
