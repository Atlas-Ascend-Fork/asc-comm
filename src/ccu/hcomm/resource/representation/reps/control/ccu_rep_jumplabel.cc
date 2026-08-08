/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"

#include "string_util.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_base.h"

namespace hcomm {
namespace CcuRep {

    CcuRepJumpLabel::CcuRepJumpLabel(CcuInsGeneratorBase* insGeneratorPtr, const std::string& label)
        : CcuRepBlock(insGeneratorPtr, label)
    {
        type = CcuRepType::JUMP_LABEL;
        Append(std::make_shared<CcuRepNop>(insGeneratorPtr));
    }

    std::string CcuRepJumpLabel::Describe() { return Hccl::StringFormat("JumpLabel[%s]", GetLabel().c_str()); }

}; // namespace CcuRep
}; // namespace hcomm
