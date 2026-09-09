/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION
#define ASCCOMM_CCU_REPRESENTATION

#include "hcomm/resource/representation/reps/common/ccu_rep_nop_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_loadarg_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_add_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_assign_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_mul_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_sub_v1.h"

#include "hcomm/resource/representation/reps/loop/ccu_rep_setloop_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loop_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopcall_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopgroup_bundle_v1.h"

#include "hcomm/resource/representation/reps/sync/ccu_rep_loc_record_event.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_loc_wait_event.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_loc_wait_notify.h"

#include "hcomm/resource/representation/reps/data/ccu_rep_read_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_write_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_loccpy_v1.h"

#include "hcomm/resource/representation/reps/sync/ccu_rep_rempostsem_v1.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_rempostvar_v1.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_remwaitsem_v1.h"

#include "hcomm/resource/representation/reps/sync/ccu_rep_record_shared_notify.h"

#include "hcomm/resource/representation/reps/data/ccu_rep_buflocread_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_buflocwrite_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_bufreduce_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_bufread_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_bufwrite_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_remMem_v1.h"

#include "hcomm/resource/representation/reps/control/ccu_rep_funccall_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_funcblock_v1.h"
#include "hcomm/resource/representation/interface/ccu_condition_v1.h"
#include "hcomm/resource/representation/interface/ccu_repeat_v1.h"
#include "hcomm/resource/representation/interface/ccu_loopblock_v1.h"
#include "hcomm/resource/representation/interface/ccu_funcblock_v1.h"
#include "hcomm/resource/representation/interface/ccu_funccall_v1.h"
#include "hcomm/resource/representation/interface/ccu_interface_assist_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_load_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_store_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_store_var_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_load_var_v1.h"

#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"

#include "hcomm/resource/representation/reps/logical/ccu_rep_and.h"
#include "hcomm/resource/representation/reps/logical/ccu_rep_or.h"
#include "hcomm/resource/representation/reps/logical/ccu_rep_not.h"
#include "hcomm/resource/representation/reps/logical/ccu_rep_xor.h"

#include "hcomm/resource/representation/reps/shift/ccu_rep_shl.h"
#include "hcomm/resource/representation/reps/shift/ccu_rep_shr.h"

#endif // HCCL_CCU_REPRESENTATION
