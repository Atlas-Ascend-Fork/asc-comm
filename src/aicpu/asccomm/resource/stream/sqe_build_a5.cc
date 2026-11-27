/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "sqe_build_a5.h"

#include <cstddef>

namespace Asc {

namespace {

void construct_lhwi(const Rt91095StarsCondIsaRegister_t dstReg, const uint64_t immd, Rt91095StarsCondOpLHWI_t& opLHWI)
{
    opLHWI.opCode = static_cast<uint32_t>(Rt91095StarsCondIsaOpCode_t::RT_91095_STARS_COND_ISA_OP_CODE_LWI);
    opLHWI.func3 = static_cast<uint32_t>(Rt91095StarsCondIsaLwiFunc3_t::RT_91095_STARS_COND_ISA_LWI_FUNC3_LHWI);
    opLHWI.rd = static_cast<uint32_t>(dstReg);
    opLHWI.immd = static_cast<uint32_t>((immd >> 49U) & 0x7FFFU); // High15-immd[63:49]
}

void construct_llwi(const Rt91095StarsCondIsaRegister_t dstReg, const uint64_t immd, Rt91095StarsCondOpLLWI_t& opLLWI)
{
    opLLWI.opCode = static_cast<uint32_t>(Rt91095StarsCondIsaOpCode_t::RT_91095_STARS_COND_ISA_OP_CODE_LWI);
    opLLWI.func3 = static_cast<uint32_t>(Rt91095StarsCondIsaLwiFunc3_t::RT_91095_STARS_COND_ISA_LWI_FUNC3_LLWI);
    opLLWI.rd = static_cast<uint32_t>(dstReg);
    opLLWI.immdHigh = static_cast<uint32_t>((immd >> 32U) & 0x1FFFFU); // Low49-immd[48:32]
    opLLWI.immdLow = static_cast<uint32_t>(immd & 0xFFFFFFFFU);        // Low49-immd[31:0]
}

void construct_load_imm(
    const Rt91095StarsCondIsaRegister_t dstReg, const uint64_t addr, const Rt91095StarsCondIsaLoadImmFunc3_t func3,
    Rt91095StarsCondOpLoadImm_t& loadImm)
{
    loadImm.opCode = static_cast<uint32_t>(Rt91095StarsCondIsaOpCode_t::RT_91095_STARS_COND_ISA_OP_CODE_LOAD_IMM);
    loadImm.rd = static_cast<uint32_t>(dstReg);
    loadImm.func3 = static_cast<uint32_t>(func3);
    loadImm.immdAddrHigh = static_cast<uint32_t>((addr >> 32U) & 0X1FFFFU); // bit[48:32]
    loadImm.immdAddrLow = static_cast<uint32_t>(addr & 0xFFFFFFFFU);        // bit[31:0]
}

void construct_branch(
    const Rt91095StarsCondIsaRegister_t rs1Reg, const Rt91095StarsCondIsaRegister_t rs2Reg,
    const Rt91095StarsCondIsaBranchFunc3_t func3, const uint8_t instrOffset, Rt91095StarsCondOpBranch_t& opBranch)
{
    opBranch.opCode = static_cast<uint32_t>(Rt91095StarsCondIsaOpCode_t::RT_91095_STARS_COND_ISA_OP_CODE_BRANCH);
    opBranch.func3 = static_cast<uint32_t>(func3);
    opBranch.rs1 = static_cast<uint32_t>(rs1Reg);
    opBranch.rs2 = static_cast<uint32_t>(rs2Reg);
    opBranch.jumpInstrOffset = instrOffset & 0xFU; // Jump-immd[3:0]
}

void construct_store(
    const Rt91095StarsCondIsaRegister_t addrReg, const Rt91095StarsCondIsaRegister_t valReg, const uint16_t immdOffset,
    const Rt91095StarsCondIsaStoreFunc3_t func3, Rt91095StarsCondOpStore_t& opStore)
{
    opStore.opCode = static_cast<uint32_t>(Rt91095StarsCondIsaOpCode_t::RT_91095_STARS_COND_ISA_OP_CODE_STORE);
    opStore.immdLow = static_cast<uint8_t>(immdOffset & 0x1FU); // S-immd[4:0]
    opStore.func3 = static_cast<uint32_t>(func3);
    opStore.rs1 = static_cast<uint32_t>(addrReg);
    opStore.rs2 = static_cast<uint32_t>(valReg);
    opStore.immdHigh = static_cast<uint8_t>((immdOffset & 0xFE0U) >> 5U); // S-immd[11:5]
}

void construct_nop(Rt91095StarsCondOpNop_t& nop)
{
    nop.opCode = static_cast<uint32_t>(Rt91095StarsCondIsaOpCode_t::RT_91095_STARS_COND_ISA_OP_CODE_NOP);
    nop.rd = static_cast<uint32_t>(Rt91095StarsCondIsaRegister_t::RT_91095_STARS_COND_ISA_REGISTER_R0);
    nop.func3 = static_cast<uint32_t>(Rt91095StarsCondIsaOpImmFunc3_t::RT_91095_STARS_COND_ISA_OP_IMM_FUNC3_NOP);
    nop.rs1 = static_cast<uint32_t>(Rt91095StarsCondIsaRegister_t::RT_91095_STARS_COND_ISA_REGISTER_R0);
    nop.immd = 0U;
}

} // namespace

void build_a5_sqe_rdma_db_send(uint32_t streamId, uint32_t taskId, uint64_t dbAddr, uint64_t dbValue, uint8_t* sqeIn)
{
    auto sqe = reinterpret_cast<Rt91095StarsWriteValueSqe*>(sqeIn);
    set_sqe_header_task_fields(sqe, taskId);
    sqe->header.type = static_cast<uint8_t>(Rt91095StarsSqeType::RT_91095_SQE_TYPE_WRITE_VALUE);
    sqe->kernelCredit = RT_STARS_DEFAULT_KERNEL_CREDIT;
    sqe->header.rtStreamId = streamId;
    sqe->header.taskId = taskId;
    sqe->writeAddrLow = dbAddr & MASK_32_BIT;
    sqe->writeAddrHigh = (dbAddr >> UINT32_BIT_NUM) & MASK_17_BIT;
    sqe->awsize = RtStarsWriteValueSizeType::RT_STARS_WRITE_VALUE_SIZE_TYPE_64BIT;
    sqe->writeValuePart[0] = static_cast<uint32_t>(dbValue & MASK_32_BIT);
    sqe->writeValuePart[1] = static_cast<uint32_t>((dbValue >> UINT32_BIT_NUM) & MASK_32_BIT);
    sqe->va = 1U;
    set_sqe_prof_field(sqe, asccomm_is_sqe_profiling_enabled());
}

void build_a5_sqe_ccore_notify_wait(
    uint32_t streamId, uint32_t taskId, uint64_t waitAddr, uint64_t actAddr, uint8_t last, uint8_t* sqeIn)
{
    (void)streamId;
    auto* sqe = reinterpret_cast<Rt91095StarsCCoreSqeNotifyWait*>(sqeIn);
    sqe->header.type = static_cast<uint8_t>(Rt91095StarsSqeType::RT_91095_SQE_TYPE_COND);
    sqe->header.rtStreamId = static_cast<uint16_t>(taskId);
    sqe->header.taskId = static_cast<uint16_t>(taskId >> LOW_BITS);

    sqe->kernelCredit = RT_STARS_DEFAULT_KERNEL_CREDIT;
    sqe->csc = 1U;

    constexpr Rt91095StarsCondIsaRegister_t r0 = Rt91095StarsCondIsaRegister_t::RT_91095_STARS_COND_ISA_REGISTER_R0;
    constexpr Rt91095StarsCondIsaRegister_t r1 = Rt91095StarsCondIsaRegister_t::RT_91095_STARS_COND_ISA_REGISTER_R1;
    constexpr Rt91095StarsCondIsaRegister_t r2 = Rt91095StarsCondIsaRegister_t::RT_91095_STARS_COND_ISA_REGISTER_R2;
    constexpr Rt91095StarsCondIsaRegister_t r3 = Rt91095StarsCondIsaRegister_t::RT_91095_STARS_COND_ISA_REGISTER_R3;

    // load current Turn to r3
    construct_load_imm(
        r3, actAddr, Rt91095StarsCondIsaLoadImmFunc3_t::RT_91095_STARS_COND_ISA_LOAD_IMM_FUNC3_LHU, sqe->ldrImm1);

    // load sendcnt to r2
    construct_load_imm(
        r2, waitAddr, Rt91095StarsCondIsaLoadImmFunc3_t::RT_91095_STARS_COND_ISA_LOAD_IMM_FUNC3_LHU, sqe->ldrImm2);
    uint8_t loadInstrOff =
        (offsetof(Rt91095StarsCCoreSqeNotifyWait, ldrImm2) - offsetof(Rt91095StarsCCoreSqeNotifyWait, ldrImm1));
    loadInstrOff = loadInstrOff / sizeof(uint32_t);

    // r2(sendCnt) < r3(curTurn)，goto reload r2
    construct_branch(
        r2, r3, Rt91095StarsCondIsaBranchFunc3_t::RT_91095_STARS_COND_ISA_BRANCH_FUNC3_BLTU, loadInstrOff, sqe->branch);

    if (last != 0U) {
        // load sendcount addr to r1
        construct_llwi(r1, waitAddr, sqe->clear.llwi1);
        construct_lhwi(r1, waitAddr, sqe->clear.lhwi1);
        // the last turn clear sendCnt, r0(0) value store to r1(sendCnt),
        construct_store(
            r1, r0, 0U, Rt91095StarsCondIsaStoreFunc3_t::RT_91095_STARS_COND_ISA_STORE_FUNC3_SH, sqe->clear.sw);
        for (Rt91095StarsCondOpNop_t& nop : sqe->clear.nop) {
            construct_nop(nop);
        }
    } else {
        for (Rt91095StarsCondOpNop_t& nop : sqe->nop) {
            construct_nop(nop);
        }
    }
}

void build_a5_sqe_ccore_notify_record(
    uint32_t streamId, uint32_t taskId, uint64_t writeAddr, uint64_t valueAddr, uint8_t* sqeIn)
{
    (void)streamId;
    auto* sqe = reinterpret_cast<Rt91095StarsCCoreSqeNotifyRecord*>(sqeIn);
    sqe->header.type = static_cast<uint8_t>(Rt91095StarsSqeType::RT_91095_SQE_TYPE_COND);
    sqe->header.rtStreamId = static_cast<uint16_t>(taskId);
    sqe->header.taskId = static_cast<uint16_t>(taskId >> LOW_BITS);

    sqe->kernelCredit = RT_STARS_DEFAULT_KERNEL_CREDIT;
    sqe->csc = 1U;

    constexpr Rt91095StarsCondIsaRegister_t r1 = Rt91095StarsCondIsaRegister_t::RT_91095_STARS_COND_ISA_REGISTER_R1;
    constexpr Rt91095StarsCondIsaRegister_t r2 = Rt91095StarsCondIsaRegister_t::RT_91095_STARS_COND_ISA_REGISTER_R2;

    construct_load_imm(
        r1, valueAddr, Rt91095StarsCondIsaLoadImmFunc3_t::RT_91095_STARS_COND_ISA_LOAD_IMM_FUNC3_LHU, sqe->ldrImm);
    construct_llwi(r2, writeAddr, sqe->llwi1);
    construct_lhwi(r2, writeAddr, sqe->lhwi1);

    construct_store(r2, r1, 0U, Rt91095StarsCondIsaStoreFunc3_t::RT_91095_STARS_COND_ISA_STORE_FUNC3_SH, sqe->sw);
    for (Rt91095StarsCondOpNop_t& nop : sqe->nop) {
        construct_nop(nop);
    }
}

} // namespace Asc
