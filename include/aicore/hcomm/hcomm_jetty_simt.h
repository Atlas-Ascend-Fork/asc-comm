/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 *
 * Public SIMT Jetty entry point.  The device implementation lives under the
 * internal hcomm implementation tree so SIMD and SIMT public headers remain
 * separate while sharing the same UB metadata ABI.
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_SIMT_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_SIMT_H

/**
 * SIMT Jetty API.
 *
 * `Write` is the submission API. Calls without a batch commit the WQE by default;
 * use the batch overload when several writers must join before one `Commit`.
 * `Commit` publishes the packed SQ head to GM but does not ring the doorbell;
 * an AIV can call the SIMD `HcommJetty::PublishSq` on the same 80-byte metadata
 * object. `PublishSq` remains available here for a SIMT-only path.
 */
#include "../../../src/aicore/hcomm/impl/platform_v310/hcomm_simt_jetty_impl.h"

#endif
