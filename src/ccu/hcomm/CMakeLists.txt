# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

target_sources(asccomm_ccu_dataplane_obj PRIVATE
    common/ccu_device_context.cc
    common/ccu_dev_mem.cc
    primitives/ccu_primitives_impl.cc
    primitives/ccu_launch.cc
)

target_sources(asccomm_ccu_dataplane_obj PRIVATE
    resource/channel/ccu_channel.cc
    resource/common/asc_ccu_res_snapshot.cc
    resource/common/ccu_var_event_res_mgr.cc
    resource/kernel/ccu_kernel_registry.cc
    resource/kernel/ccu_kernel_registry_mgr.cc
    resource/kernel/ccu_kernel.cc
    resource/kernel/ccu_dfx.cc
    resource/kernel/ccu_kernel_res_allocator.cc
    resource/kernel/ccu_kernel_mgr.cc
    resource/microcode/ccu_microcode.cc
    resource/microcode/ccu_assist.cc
    resource/microcode_opt/extract_operands.cc
    resource/microcode_opt/instruction_scheduler.cc
    resource/microcode_opt/microcode_optimizer.cc
)

target_sources(asccomm_ccu_dataplane_obj PRIVATE
    resource/representation/context/ccu_rep_context.cc
    resource/representation/interface/ccu_condition.cc
    resource/representation/interface/ccu_datatype.cc
    resource/representation/interface/ccu_funcblock.cc
    resource/representation/interface/ccu_funccall.cc
    resource/representation/interface/ccu_interface_assist.cc
    resource/representation/interface/ccu_loopblock.cc
    resource/representation/interface/ccu_loopcall.cc
    resource/representation/interface/ccu_repeat.cc
    resource/representation/reps/arithmetic/ccu_operator.cc
    resource/representation/reps/arithmetic/ccu_rep_add.cc
    resource/representation/reps/arithmetic/ccu_rep_assign.cc
    resource/representation/reps/arithmetic/ccu_rep_mul.cc
    resource/representation/reps/arithmetic/ccu_rep_sub.cc
    resource/representation/reps/common/ccu_rep_base.cc
    resource/representation/reps/common/ccu_rep_block.cc
    resource/representation/reps/common/ccu_rep_load.cc
    resource/representation/reps/common/ccu_rep_load_var.cc
    resource/representation/reps/common/ccu_rep_loadarg.cc
    resource/representation/reps/common/ccu_rep_nop.cc
    resource/representation/reps/common/ccu_rep_store.cc
    resource/representation/reps/common/ccu_rep_store_var.cc
    resource/representation/reps/control/ccu_rep_funcblock.cc
    resource/representation/reps/control/ccu_rep_funccall.cc
    resource/representation/reps/control/ccu_rep_jump.cc
    resource/representation/reps/control/ccu_rep_jumplabel.cc
    resource/representation/reps/data/ccu_rep_buflocread.cc
    resource/representation/reps/data/ccu_rep_buflocwrite.cc
    resource/representation/reps/data/ccu_rep_bufread.cc
    resource/representation/reps/data/ccu_rep_bufreduce.cc
    resource/representation/reps/data/ccu_rep_bufwrite.cc
    resource/representation/reps/data/ccu_rep_loccpy.cc
    resource/representation/reps/data/ccu_rep_read.cc
    resource/representation/reps/data/ccu_rep_remMem.cc
    resource/representation/reps/data/ccu_rep_write.cc
    resource/representation/reps/logical/ccu_rep_and.cc
    resource/representation/reps/logical/ccu_rep_not.cc
    resource/representation/reps/logical/ccu_rep_or.cc
    resource/representation/reps/logical/ccu_rep_xor.cc
    resource/representation/reps/loop/ccu_rep_loop.cc
    resource/representation/reps/loop/ccu_rep_loopblock.cc
    resource/representation/reps/loop/ccu_rep_loopcall.cc
    resource/representation/reps/loop/ccu_rep_loopgroup_bundle.cc
    resource/representation/reps/loop/ccu_rep_setloop.cc
    resource/representation/reps/shift/ccu_rep_shl.cc
    resource/representation/reps/shift/ccu_rep_shr.cc
    resource/representation/reps/sync/ccu_rep_loc_record_event.cc
    resource/representation/reps/sync/ccu_rep_loc_wait_event.cc
    resource/representation/reps/sync/ccu_rep_loc_wait_notify.cc
    resource/representation/reps/sync/ccu_rep_record_shared_notify.cc
    resource/representation/reps/sync/ccu_rep_rempostsem.cc
    resource/representation/reps/sync/ccu_rep_rempostvar.cc
    resource/representation/reps/sync/ccu_rep_remwaitsem.cc
    resource/representation/reps/translator/ccu_ins_generater_v1.cc
    resource/representation/reps/translator/ccu_rep_reference_manager.cc
    resource/representation/reps/translator/ccu_rep_translator.cc
)
