/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CCU_RES_PACK_H
#define CCU_RES_PACK_H

#include "ccu_dataplane_abi.h"
#include "ccu_types.h"
#include "ccu_provider_internal.h"

namespace hcomm {

class CcuResPack {
public:
    CcuResPack() = default;
    ~CcuResPack() = default;

    CcuResult Init(const AscCommCcuInstanceCreateParam &param, const AscCommCcuResourceRequest &translatorRequest);
    CcuResult Reset();

    CcuResRepository &GetCcuResRepo();
    const CcuResRepository &GetTranslatorResRepo() const;

private:
    CcuResPack(const CcuResPack &that) = delete;
    CcuResPack &operator=(const CcuResPack &that) = delete;
    CcuResPack(CcuResPack &&that) = delete;
    CcuResPack &operator=(CcuResPack &&that) = delete;

    CcuResRepository initialResRepo_{};
    CcuResRepository resRepo_{};
    CcuResRepository translatorResRepo_{};
};

} // namespace hcomm

#endif // CCU_RES_PACK_H
