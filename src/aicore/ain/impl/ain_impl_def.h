/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file ain_impl_def.h
 * \brief ain implementation definition
 */

#if !defined(ASCENDC_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal ain header file and must not be used directly. " \
                "Please use public interface headers.")
#define ASCENDC_INCLUDE_INTERNAL_HEADERS
#define UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_DEF_H
#endif

#ifndef IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_DEF_H
#define IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_DEF_H

#include "../../hcomm/impl/hcomm_impl_def.h"
#include "hcomm/hcomm_team_entity_defs.h"

namespace AscendC {

constexpr comm_protocol mask_to_comm_protocol(unsigned comm_engine_mask)
{
    switch (comm_engine_mask) {
        case ain_mask_default:
            return comm_protocol_ubc_ctp;
        default:
            return comm_protocol_reserved;
    }
}

template <unsigned comm_engine_mask>
class ain;

template <unsigned comm_engine_mask>
class ain_barrier_session_impl {
public:
    static constexpr comm_protocol protocol = mask_to_comm_protocol(comm_engine_mask);
    template <bool enable_timeout, typename descriptor_ubuf>
    AIN_DEVICE int32_t sync_impl(ain_memory_order order, uint64_t timeout_cycles, const descriptor_ubuf& ubuf);

private:
    template <typename descriptor_ubuf>
    AIN_DEVICE void signal(asccomm_team_handle team, uint32_t peer, const descriptor_ubuf& ubuf);

    AIN_DEVICE uint64_t read_signal(
        asccomm_team_handle team, uint32_t signal_id, uint32_t bits = 64,
        ain_memory_order order = ain_memory_order_relax) const;

    AIN_DEVICE void wait_signal(
        asccomm_team_handle team, uint32_t signal_id, uint64_t least, uint32_t bits = 64,
        ain_memory_order order = ain_memory_order_relax) const;

public:
    ain<comm_engine_mask>* ain;
    asccomm_team_handle team;
    uint32_t index;
    size_t signal_base_id;
    asccomm_impl<protocol> asccomm_;
};

} // namespace AscendC

#endif // IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_DEF_H

#if defined(UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_DEF_H)
#undef ASCENDC_INCLUDE_INTERNAL_HEADERS
#undef UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_DEF_H
#endif
