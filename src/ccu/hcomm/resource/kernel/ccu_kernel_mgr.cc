/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel_mgr.h"
#include "hcomm/resource/kernel/ccu_kernel_res_allocator.h"

#include "string_util.h"
#include "exception_handler.h"
#include "adapter_rts.h"
#include "hcomm/resource/microcode/ccu_assist_v1.h"
#include "dev_buffer.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_type_v1.h"

#include "hcomm_adapter_hccp.h"

#include "hcomm/common/ccu_log.h"
#include "hcomm/resource/common/ccu_kernel_func.h"

namespace hcomm {

CcuKernelMgr::~CcuKernelMgr()
{
    if (!initializedFlag_) {
        return;
    }

    if (instructionLoadDevMem_) {
        HCCL_RUN_INFO(
            "[CcuKernelMgr][~CcuKernelMgr]: deviceLogicId[%d], free addr[%p]", devLogicId_, instructionLoadDevMem_);
        (void)hrtFree(instructionLoadDevMem_);
        instructionLoadDevMem_ = nullptr;
        instructionLoadDevMemSize_ = 0;
    }

    (void)Deinit();
}

CcuKernelMgr& CcuKernelMgr::GetInstance(const int32_t deviceLogicId)
{
    static CcuKernelMgr kernelManager[MAX_MODULE_DEVICE_NUM + 1];

    int32_t devLogicId = deviceLogicId;
    if (devLogicId < 0 || static_cast<uint32_t>(devLogicId) >= MAX_MODULE_DEVICE_NUM) {
        HCCL_WARNING(
            "[CcuKernelMgr][%s] use the backup device, devLogicId[%d] should be "
            "less than %u.",
            __func__, devLogicId, MAX_MODULE_DEVICE_NUM);
        devLogicId = MAX_MODULE_DEVICE_NUM; // 使用备份设备
    }

    kernelManager[devLogicId].devLogicId_ = devLogicId;
    return kernelManager[devLogicId];
}

HcclResult CcuKernelMgr::Init()
{
    std::unique_lock<std::mutex> lock(kernelMapMutex_);
    initializedFlag_ = true;
    kernelMap_.clear();
    return HcclResult::HCCL_SUCCESS;
}

CcuResult CcuKernelMgr::Configure(CcuResPack &resPack)
{
    std::unique_lock<std::mutex> lock(kernelMapMutex_);
    devLogicId_ = resPack.GetDeviceLogicId();
    if (activeGeneration_ == resPack.GetGeneration()) {
        return CcuResult::CCU_SUCCESS;
    }

    translators.clear();
    referenceMgrs.clear();
    ccuVersion_ = resPack.GetCcuVersion() == 0 ? CcuVersion::CCU_V1 : CcuVersion::CCU_V2;
    CcuRep::CcuRepTranslator::SetCcuVersion(ccuVersion_);
    for (uint32_t dieId = 0; dieId < HCOMM_CCU_MAX_DIE_NUM; ++dieId) {
        if ((resPack.GetValidDieMask() & (1U << dieId)) == 0) {
            continue;
        }
        const HcommCcuDieMetadataPod *metadata = resPack.GetDieMetadata(dieId);
        CCU_CHK_PTR_NULL(metadata);
        missionKeys_[dieId] = metadata->missionKey;
        CCU_CHK_RET(static_cast<CcuResult>(InstantiationTranslator(static_cast<uint16_t>(dieId), resPack)));
    }
    insGenePtr = std::make_shared<CcuRep::CcuInsGeneraterV1>();
    initializedFlag_ = true;
    activeGeneration_ = resPack.GetGeneration();
    return CcuResult::CCU_SUCCESS;
}

HcclResult CcuKernelMgr::Deinit()
{
    // 不需要主动释放CCU指令空间等资源，因为设备管理与kernelMgr都为静态，生命周期一致
    std::unique_lock<std::mutex> lock(kernelMapMutex_);
    initializedFlag_ = false;
    activeGeneration_ = 0;
    kernelMap_.clear();
    translators.clear();
    referenceMgrs.clear();
    return HcclResult::HCCL_SUCCESS;
}

CcuResult CcuKernelMgr::Register(
    CcuResPack& resPack, const uint32_t dieId, const char* kernelFuncName, const void* kernelFunc,
    const void** kernelArgs, const uint32_t argNum, CcuKernelHandle& kernelHandle)
{
    // 允许kernelFuncName未空，此时传递默认名称
    (void)kernelFuncName;
    CCU_CHK_PTR_NULL(kernelFunc);

    // 当前argNum仅允许 0 或 1
    if (argNum > 1) {
        HCCL_ERROR("[%s] failed, argNum[%u] now only support 0 or 1.", __func__, argNum);
        return CcuResult::CCU_E_PARA;
    }

    // 注意处理时序，需要先重置后处理rep
    std::unique_lock<std::mutex> lock(kernelMapMutex_);
    currKernel_ = std::make_unique<CcuKernel>(); // 重置待注册kernel
    currKernel_->SetDieId(0);                    // 资源先记在 die 0，确定实际 die 后再迁移
    CCU_CHK_RET(currKernel_->SetupProfilingInfo(kernelFuncName));

    // 初始化翻译器（需在执行kernel func前设置，因为func执行时会创建rep对象）
    currKernel_->SetInsGenerater(insGenePtr.get());
    currKernel_->SetCcuVersion(ccuVersion_);

    if (argNum == 0) {
        auto ccuKernelFunc = reinterpret_cast<CcuKernelFuncNoArg>(kernelFunc);
        CCU_CHK_RET(ccuKernelFunc()); // 执行算法流程，生成rep和计算资源占用
    } else {
        CCU_CHK_PTR_NULL(kernelArgs);
        const void* kernelArg = kernelArgs[0];
        CCU_CHK_PTR_NULL(kernelArg);
        const auto ccuKernelArg = const_cast<CcuKernelArg>(kernelArg);
        auto ccuKernelFunc = reinterpret_cast<CcuKernelFuncOneArg>(kernelFunc);
        CCU_CHK_RET(ccuKernelFunc(ccuKernelArg)); // 执行算法流程，生成rep和计算资源占用
    }

    currKernel_->FlushClosablePendingIfs(); // 处理未闭合的if
    CCU_CHK_RET(currKernel_->ValidateAndApplyDie(dieId));
    CCU_CHK_RET(PrepareConstValueResources()); // 记录翻译过程所需常量并申请对应资源

    CcuResult ret = AllocRes(resPack);
    if (ret != CcuResult::CCU_SUCCESS) {
        HCCL_WARNING("[%s] AllocRes failed, maybe resource not enough, please check ret[%d]", __func__, ret);
        return ret;
    }

    kernelId_++;
    kernelMap_[kernelId_] = std::move(currKernel_);

    kernelHandle = kernelId_;
    return CcuResult::CCU_SUCCESS;
}

static void DumpResReqInfo(const HcommCcuResRequestPod& totalRes)
{
    for (uint32_t i = 0; i < CCU_MAX_IODIE_NUM; i++) {
        if (totalRes.count[HCOMM_CCU_BATCH_RES_MS][i] != 0 || totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_CKE][i] != 0 || totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_LOOP][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i] != 0 || totalRes.count[HCOMM_CCU_BATCH_RES_GSA][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i] != 0 || totalRes.count[HCOMM_CCU_BATCH_RES_XN][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_MISSION][i] != 0) {
            HCCL_INFO(
                "DumpResReqInfo: dieId[%u], msReq[%u], blockMsReq[%u], ckeReq[%u], blockCkeReq[%u], "
                "loopEngineReq[%u], blockLoopEngineReq[%u], gsaReq[%u], blockGsaReq[%u], xnReq[%u], "
                "blockXnReq[%u], missionReq[%u]",
                i, totalRes.count[HCOMM_CCU_BATCH_RES_MS][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_CKE][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_LOOP][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_GSA][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_XN][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_MISSION][i]);
        }
    }
}

inline int32_t GetResTotalNum(const std::vector<HcommCcuResRangePod>& resInfos)
{
    int32_t resNum = 0;
    for (HcommCcuResRangePod resInfo : resInfos) {
        resNum += static_cast<int32_t>(resInfo.count);
    }
    return resNum;
}

static void GetResNumFromResPack(CcuResPack& resPack, HcommCcuResRequestPod& totalRes)
{
    // 获取通信域当前所持有的资源
    const auto& tmpResRepository = resPack.GetCcuResRepo();

    // repository 保存的是若干离散区间，这里按“资源类型 + DIE”求和，转换成定长 POD 计数矩阵。
    for (u32 i = 0; i < CCU_MAX_IODIE_NUM; i++) {
        totalRes.count[HCOMM_CCU_BATCH_RES_MS][i] += GetResTotalNum(tmpResRepository.ms[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i] += GetResTotalNum(tmpResRepository.blockMs[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_CKE][i] += GetResTotalNum(tmpResRepository.cke[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i] += GetResTotalNum(tmpResRepository.blockCke[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_LOOP][i] += GetResTotalNum(tmpResRepository.loopEngine[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i] += GetResTotalNum(tmpResRepository.blockLoopEngine[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_GSA][i] += GetResTotalNum(tmpResRepository.gsa[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i] += GetResTotalNum(tmpResRepository.blockGsa[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_XN][i] += GetResTotalNum(tmpResRepository.xn[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i] += GetResTotalNum(tmpResRepository.blockXn[i]);
        totalRes.count[HCOMM_CCU_BATCH_RES_MISSION][i] += GetResTotalNum(tmpResRepository.mission[i]);
    }

    DumpResReqInfo(totalRes);
    HCCL_INFO("GetResPackTotalResNum:dumpInfos success.");
}

inline uint32_t GetReqResNum(const uint32_t reqRes, const uint32_t totalRes)
{
    return ((reqRes > totalRes) ? (reqRes - totalRes) : 0);
}

static bool CheckResIfAvailable(const HcommCcuResRequestPod& totalRes, const HcommCcuResRequestPod& resReq)
{
    DumpResReqInfo(resReq);

    HcommCcuResRequestPod needResReq{};
    // todo: 优化为遍历数组
    for (u32 i = 0; i < CCU_MAX_IODIE_NUM; i++) {
        needResReq.count[HCOMM_CCU_BATCH_RES_MS][i] =
            GetReqResNum(resReq.count[HCOMM_CCU_BATCH_RES_MS][i], totalRes.count[HCOMM_CCU_BATCH_RES_MS][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i] = GetReqResNum(
            resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_CKE][i] =
            GetReqResNum(resReq.count[HCOMM_CCU_BATCH_RES_CKE][i], totalRes.count[HCOMM_CCU_BATCH_RES_CKE][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i] = GetReqResNum(
            resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_LOOP][i] =
            GetReqResNum(resReq.count[HCOMM_CCU_BATCH_RES_LOOP][i], totalRes.count[HCOMM_CCU_BATCH_RES_LOOP][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i] = GetReqResNum(
            resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_GSA][i] =
            GetReqResNum(resReq.count[HCOMM_CCU_BATCH_RES_GSA][i], totalRes.count[HCOMM_CCU_BATCH_RES_GSA][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i] = GetReqResNum(
            resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_XN][i] =
            GetReqResNum(resReq.count[HCOMM_CCU_BATCH_RES_XN][i], totalRes.count[HCOMM_CCU_BATCH_RES_XN][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i] = GetReqResNum(
            resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i]);
        needResReq.count[HCOMM_CCU_BATCH_RES_MISSION][i] =
            GetReqResNum(resReq.count[HCOMM_CCU_BATCH_RES_MISSION][i], totalRes.count[HCOMM_CCU_BATCH_RES_MISSION][i]);

        if (needResReq.count[HCOMM_CCU_BATCH_RES_MISSION][i] > 0) {
            needResReq.missionReqType = resReq.missionReqType;
        }

        if (needResReq.count[HCOMM_CCU_BATCH_RES_MS][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_CKE][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_LOOP][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_GSA][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_XN][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i] != 0 ||
            needResReq.count[HCOMM_CCU_BATCH_RES_MISSION][i] != 0) {
            HCCL_WARNING(
                "[CcuKernelMgr][%s] dieId[%u] not enough, msReq[%u] blockMsReq[%u] ckeReq[%u]"
                "blockCkeReq[%u] loopEngineReq[%u] blockLoopEngineReq[%u] gsaReq[%u] blockGsaReq[%u]"
                "xnReq[%u] blockXnReq[%u] missionReq[%u].",
                __func__, i, needResReq.count[HCOMM_CCU_BATCH_RES_MS][i],
                needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i], needResReq.count[HCOMM_CCU_BATCH_RES_CKE][i],
                needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i], needResReq.count[HCOMM_CCU_BATCH_RES_LOOP][i],
                needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i], needResReq.count[HCOMM_CCU_BATCH_RES_GSA][i],
                needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i], needResReq.count[HCOMM_CCU_BATCH_RES_XN][i],
                needResReq.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i], needResReq.count[HCOMM_CCU_BATCH_RES_MISSION][i]);
            return false;
        }
    }

    return true;
}

static void MoveResInfo(
    std::vector<HcommCcuResRangePod>& dest, std::vector<HcommCcuResRangePod>& source, const uint32_t resNum)
{
    // Register 前序流程已检查资源不足场景
    if (resNum == 0) {
        return;
    }

    dest.clear();
    auto iter = source.begin();
    uint32_t remain = resNum;
    while (remain > 0 && iter != source.end()) {
        auto& srcBlock = *iter;
        const uint32_t take = std::min(remain, srcBlock.count);
        dest.push_back({srcBlock.resourceType, srcBlock.dieId, srcBlock.startId, take});

        if (take == srcBlock.count) {
            // 完全用掉这个资源，source中移除
            iter = source.erase(iter);
        } else {
            // 只用了部分，更新source中的资源
            srcBlock.startId += take;
            srcBlock.count -= take;
        }

        remain -= take; // 更新剩余需要的资源数量
    }
}

static void LoadRes(std::unique_ptr<CcuKernel>& kernel, CcuResPack& resPack)
{
    const HcommCcuResRequestPod& resReq = kernel->GetResourceRequest();
    AscCcuResRepository& totalResRepo = resPack.GetCcuResRepo();
    AscCcuResRepository kernelResRepo{};

    // 按 kernel 请求数量从通信域资源池切分区间；切出的区间归 kernel，剩余区间留在 resPack。
    for (uint8_t i = 0; i < CCU_MAX_IODIE_NUM; i++) { // todo: 建议改成dieId
        MoveResInfo(kernelResRepo.loopEngine[i], totalResRepo.loopEngine[i], resReq.count[HCOMM_CCU_BATCH_RES_LOOP][i]);
        MoveResInfo(
            kernelResRepo.blockLoopEngine[i], totalResRepo.blockLoopEngine[i],
            resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i]);
        MoveResInfo(kernelResRepo.ms[i], totalResRepo.ms[i], resReq.count[HCOMM_CCU_BATCH_RES_MS][i]);
        MoveResInfo(kernelResRepo.blockMs[i], totalResRepo.blockMs[i], resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i]);
        MoveResInfo(kernelResRepo.cke[i], totalResRepo.cke[i], resReq.count[HCOMM_CCU_BATCH_RES_CKE][i]);
        MoveResInfo(
            kernelResRepo.blockCke[i], totalResRepo.blockCke[i], resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i]);
        MoveResInfo(kernelResRepo.xn[i], totalResRepo.xn[i], resReq.count[HCOMM_CCU_BATCH_RES_XN][i]);
        MoveResInfo(kernelResRepo.blockXn[i], totalResRepo.blockXn[i], resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i]);
        MoveResInfo(kernelResRepo.gsa[i], totalResRepo.gsa[i], resReq.count[HCOMM_CCU_BATCH_RES_GSA][i]);
        MoveResInfo(
            kernelResRepo.blockGsa[i], totalResRepo.blockGsa[i], resReq.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i]);
        MoveResInfo(kernelResRepo.mission[i], totalResRepo.mission[i], resReq.count[HCOMM_CCU_BATCH_RES_MISSION][i]);
    }

    kernel->SetResRepository(kernelResRepo);
}

static CcuResult AllocInstrRes(std::unique_ptr<CcuKernel>& kernel, CcuResPack &resPack)
{
    const uint32_t instrCount =
        kernel->GetInstrCount() + CcuRep::CcuRepTranslator::GetInstrNum() + kernel->GetConstValue2VarMap().size();
    const uint32_t dieId = kernel->GetDieId();
    HcommCcuResRangePod insInfo{};
    CCU_CHK_RET(resPack.AllocateInstruction(dieId, instrCount, insInfo));
    HCCL_INFO(
        "[CcuKernelMgr][%s]: devLogicId[%d], dieId[%u], startId[%u], count[%u]", __func__,
        resPack.GetDeviceLogicId(), dieId,
        insInfo.startId, insInfo.count);
    kernel->SetInstrId(insInfo.startId);

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuKernelMgr::PrepareConstValueResources()
{
    // insGenerator统计rep中常量，并填写当前kernel的常量表，当前只有A6有对应处理，A5没有常量处理需求
    CCU_CHK_PTR_NULL(currKernel_);
    const auto& repVec = currKernel_->GetRepSequence();

    const auto& translator = translators[currKernel_->GetDieId()][0];
    CCU_CHK_PTR_NULL(translator);
    const auto& transDep = translator->GetTransDep(); // 此时未分配missionid，取0对应的transDep读取常量
    CCU_CHK_PTR_NULL(insGenePtr);
    for (uint32_t index = 0; index < repVec.size(); index++) {
        const auto& curRepType = repVec[index]->Type();
        CcuRep::CcuRepBase* curRepPtr = repVec[index].get();
        CCU_CHK_PTR_NULL(curRepPtr);

        // 遍历每个rep，包括repBlock中的每个rep，将常量资源需求记录在currkernel中
        insGenePtr->PrepareConstValue(curRepPtr, transDep, currKernel_.get());
        if (curRepType == CcuRep::CcuRepType::BLOCK || curRepType == CcuRep::CcuRepType::FUNC_BLOCK ||
            curRepType == CcuRep::CcuRepType::LOOP_BLOCK) {
            CcuRep::CcuRepBlock* curRepBlockPtr = static_cast<CcuRep::CcuRepBlock*>(curRepPtr);
            CCU_CHK_PTR_NULL(curRepBlockPtr);
            for (const auto& repInBlock : curRepBlockPtr->GetReps()) {
                insGenePtr->PrepareConstValue(repInBlock.get(), transDep, currKernel_.get());
            }
        }
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuKernelMgr::AllocRes(CcuResPack& resPack)
{
    const HcommCcuResRequestPod& resReq = currKernel_->GetResourceRequest();
    AscCcuResRepository remaining{};
    AscCcuResRepository allocated{};
    CcuResult planRet = PlanKernelResources(resPack.GetCcuResRepo(), resReq, remaining, allocated);
    if (planRet != CcuResult::CCU_SUCCESS) {
        DumpResReqInfo(resReq);
        HCCL_WARNING("[CcuKernelMgr][%s] resource is not enough.", __func__);
        return planRet;
    }

    CCU_CHK_RET(AllocInstrRes(currKernel_, resPack));

    resPack.GetCcuResRepo() = std::move(remaining);
    currKernel_->SetResRepository(std::move(allocated));
    const HcommCcuDieMetadataPod *metadata = resPack.GetDieMetadata(currKernel_->GetDieId());
    CCU_CHK_PTR_NULL(metadata);
    currKernel_->SetMissionKey(metadata->missionKey);

    return CcuResult::CCU_SUCCESS;
}

template <typename T1, typename T2>
HcclResult ResetRepResourceTemplate(
    std::vector<T1>& resource, const std::vector<T2>& repository, const uint32_t startIndex = 0)
{
    if (resource.size() > repository.size() - startIndex) {
        HCCL_ERROR(
            "[CcuKernelMgr][ResetRepResourceTemplate]resource size[%u] bigger "
            "repository size[%u] typeid[%s]",
            resource.size(), repository.size(), typeid(T1).name());
        return HcclResult::HCCL_E_INTERNAL;
    }

    for (uint32_t j = 0; j < resource.size(); j++) {
        resource[j].Reset(repository[j + startIndex].startId);
    }

    return HcclResult::HCCL_SUCCESS;
}

static HcclResult ResetRepResourceToResRepository(
    CcuRepResource& totalRepRes, const AscCcuResRepository& totalResRepository)
{
    // 遍历translatorRepRes, 将每个rep的虚拟资源翻译到实际物理资源上
    for (u32 i = 0; i < CCU_MAX_IODIE_NUM; i++) {
        CHK_RET(ResetRepResourceTemplate(totalRepRes.ccubufs[i], totalResRepository.ms[i]));
        CHK_RET(ResetRepResourceTemplate(totalRepRes.blockCcubufs[i], totalResRepository.blockMs[i]));
        CHK_RET(ResetRepResourceTemplate(totalRepRes.executor[i], totalResRepository.loopEngine[i]));
        CHK_RET(ResetRepResourceTemplate(totalRepRes.blockExecutor[i], totalResRepository.blockLoopEngine[i]));
        CHK_RET(ResetRepResourceTemplate(totalRepRes.completedEvent[i], totalResRepository.cke[i]));
        CHK_RET(ResetRepResourceTemplate(totalRepRes.blockCompletedEvent[i], totalResRepository.blockCke[i]));
        CHK_RET(ResetRepResourceTemplate(
            totalRepRes.localNotify[i], totalResRepository.blockCke[i],
            totalRepRes.blockCompletedEvent[i].size())); // 两类资源都使用block cke，需要调整起始分配位置
        CHK_RET(ResetRepResourceTemplate(totalRepRes.address[i], totalResRepository.gsa[i]));
        CHK_RET(ResetRepResourceTemplate(totalRepRes.blockAddress[i], totalResRepository.blockGsa[i]));
        CHK_RET(ResetRepResourceTemplate(totalRepRes.variable[i], totalResRepository.xn[i]));
        CHK_RET(ResetRepResourceTemplate(totalRepRes.continuousVariable[i], totalResRepository.blockXn[i]));
    }
    return HcclResult::HCCL_SUCCESS;
}

using DieResInfos = std::array<std::vector<HcommCcuResRangePod>, CCU_MAX_IODIE_NUM>;
static HcclResult SaveKernelMissionInfo(CcuKernel* kernel, const DieResInfos& missionId)
{
    const uint32_t dieId = kernel->GetDieId();
    // 从missionId中获取一个元素并从missionId中删除，当前应只有一个元素，且无需删除
    if (missionId[dieId].empty()) {
        HCCL_ERROR("[%s] failed, dieId[%u] does not have missions.", __func__, dieId);
        return HcclResult::HCCL_E_INTERNAL;
    }

    kernel->SetMissionId(missionId[dieId].back().startId);
    return HcclResult::HCCL_SUCCESS;
}

static void DumpResRepositoryInfo(const AscCcuResRepository& resRepo)
{
    for (uint32_t i = 0; i < CCU_MAX_IODIE_NUM; i++) {
        if (resRepo.ms[i].size() != 0 || resRepo.blockMs[i].size() != 0 || resRepo.cke[i].size() != 0 ||
            resRepo.blockCke[i].size() != 0 || resRepo.loopEngine[i].size() != 0 ||
            resRepo.blockLoopEngine[i].size() != 0 || resRepo.gsa[i].size() != 0 || resRepo.blockGsa[i].size() != 0 ||
            resRepo.xn[i].size() != 0 || resRepo.blockXn[i].size() != 0 || resRepo.mission[i].size() != 0) {
            HCCL_INFO(
                "DumpResRepository: dieId[%u], ms size[%u], blockMs size[%u], cke size[%u], blockCke size[%u], "
                "loopEngine size[%u], blockLoopEngine size[%u], gsa size[%u], blockGsa size[%u], xn size[%u], "
                "blockXn size[%u], mission size[%u]",
                i, resRepo.ms[i].size(), resRepo.blockMs[i].size(), resRepo.cke[i].size(), resRepo.blockCke[i].size(),
                resRepo.loopEngine[i].size(), resRepo.blockLoopEngine[i].size(), resRepo.gsa[i].size(),
                resRepo.blockGsa[i].size(), resRepo.xn[i].size(), resRepo.blockXn[i].size(), resRepo.mission[i].size());
        }
    }
}

inline void ExpandResInfo(
    std::vector<HcommCcuResRangePod>& expendResInfos, const std::vector<HcommCcuResRangePod>& resInfos)
{
    // 将resInfo中的资源信息还原为单个资源粒度
    for (auto& resInfo : resInfos) {
        for (uint32_t id = 0; id < resInfo.count; id++) {
            expendResInfos.push_back({resInfo.resourceType, resInfo.dieId, resInfo.startId + id, 1});
        }
    }
}

static CcuResult ExpandResRepo(AscCcuResRepository& totalRes, const AscCcuResRepository& tmpResRepository)
{
    // 合并获取的所持有的资源信息, 按照类型合并资源总和到totalRes中
    for (u32 i = 0; i < CCU_MAX_IODIE_NUM; i++) {
        ExpandResInfo(totalRes.ms[i], tmpResRepository.ms[i]);
        ExpandResInfo(totalRes.blockMs[i], tmpResRepository.blockMs[i]);
        ExpandResInfo(totalRes.loopEngine[i], tmpResRepository.loopEngine[i]);
        ExpandResInfo(totalRes.blockLoopEngine[i], tmpResRepository.blockLoopEngine[i]);
        ExpandResInfo(totalRes.cke[i], tmpResRepository.cke[i]);
        ExpandResInfo(totalRes.blockCke[i], tmpResRepository.blockCke[i]);
        ExpandResInfo(totalRes.gsa[i], tmpResRepository.gsa[i]);
        ExpandResInfo(totalRes.blockGsa[i], tmpResRepository.blockGsa[i]);
        ExpandResInfo(totalRes.xn[i], tmpResRepository.xn[i]);
        ExpandResInfo(totalRes.blockXn[i], tmpResRepository.blockXn[i]);
        ExpandResInfo(totalRes.mission[i], tmpResRepository.mission[i]);
    }
    DumpResRepositoryInfo(totalRes);
    return CcuResult::CCU_SUCCESS;
}

template <typename T>
static HcclResult MergeExportedResources(
    const std::unordered_map<std::string, T>& inputRes, std::unordered_map<std::string, T>& outputRes)
{
    for (const auto& item : inputRes) {
        const auto& resTag = item.first;
        if (outputRes.find(resTag) != outputRes.end()) {
            HCCL_ERROR(
                "[CcuKernelMgr][%s] failed, exported resource tag[%s] is already existed, "
                "please check.",
                __func__, resTag);
            return HcclResult::HCCL_E_PARA;
        }

        outputRes.insert(item);
    }

    return HcclResult::HCCL_SUCCESS;
}

template <typename T>
static HcclResult ResetImportedResources(
    std::unordered_map<std::string, T>& importedRes, const std::unordered_map<std::string, T>& exportedRes)
{
    for (auto& item : importedRes) {
        const auto& resTag = item.first;
        const auto& iter = exportedRes.find(resTag);
        if (iter == exportedRes.end()) {
            HCCL_ERROR("[CcuKernelMgr][%s] failed to find exported resources by tag[%s].", __func__, resTag.c_str());
            return HcclResult::HCCL_E_NOT_FOUND;
        }

        item.second.Reset(iter->second.Id(), iter->second.DieId());
    }

    return HcclResult::HCCL_SUCCESS;
}

static HcclResult ProcessInterCtxRes(const std::vector<CcuKernel*>& kernels)
{
    std::unordered_map<std::string, CcuRep::LocalNotify> totalExportedNotifies;

    for (const auto kernel : kernels) {
        const auto& exportedRes = kernel->GetExportedRes();
        CHK_RET(MergeExportedResources(exportedRes.sharedNotifies, totalExportedNotifies));
    }

    for (auto kernel : kernels) {
        auto& importedRes = kernel->GetImportedRes();
        CHK_RET(ResetImportedResources(importedRes.sharedNotifies, totalExportedNotifies));
    }

    return HcclResult::HCCL_SUCCESS;
}

static HcclResult TransRepResToPhyRes(const std::vector<CcuKernel*>& kernels)
{
    for (auto kernel : kernels) {
        const auto& totalResRepository = kernel->GetResRepository();
        auto& totalRepRes = kernel->GetResource();

        // 将ccu kernel持有的物理资源赋给资源对象
        AscCcuResRepository expandedResRepo{};
        ExpandResRepo(expandedResRepo, totalResRepository);
        CHK_RET(ResetRepResourceToResRepository(totalRepRes, expandedResRepo));

        CHK_RET(SaveKernelMissionInfo(kernel, totalResRepository.mission));
    }

    CHK_RET(ProcessInterCtxRes(kernels));

    return HcclResult::HCCL_SUCCESS;
}

CcuResult CcuKernelMgr::Translate(const std::vector<CcuKernelHandle>& kernelHandles)
{
    if (kernelHandles.empty()) {
        HCCL_INFO("[CcuKernelMgr][%s] passed, kernelHandles are empty.", __func__);
        return CcuResult::CCU_SUCCESS;
    }

    std::vector<CcuKernel*> kernels{};
    std::unique_lock<std::mutex> mapLock(kernelMapMutex_);
    for (const auto kernelHandle : kernelHandles) {
        const auto& iter = kernelMap_.find(kernelHandle);
        if (iter == kernelMap_.end()) {
            HCCL_ERROR(
                "[CcuKernelMgr][%s] failed to find kernel by ccu kernel handle[0x%llx].", __func__, kernelHandle);
            return CcuResult::CCU_E_NOT_FOUND;
        }

        kernels.push_back(iter->second.get());
    }
    mapLock.unlock();

    constexpr bool isFuncBlock = false; // 当前不支持MC2

    std::unique_lock<std::mutex> translateLock(translateMutex_);
    CCU_CHK_RET(TransRepResToPhyRes(kernels));
    CCU_CHK_RET(TransRepSequenceToMicrocode(kernels, isFuncBlock));

    for (auto& referenceMgrMap : referenceMgrs) {
        for (auto& referenceMgr : referenceMgrMap.second) {
            referenceMgr.second->ClearRepReference();
        }
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuKernelMgr::UnRegister(const CcuKernelHandle kernelHandle)
{
    std::unique_lock<std::mutex> lock(kernelMapMutex_);

    // 校验kernelMap_中是否存在executorId对应的kernel
    auto it = kernelMap_.find(kernelHandle);
    CHK_PRT_RET(
        it == kernelMap_.end(),
        HCCL_ERROR("[CcuKernelMgr][%s] kernelHandle [%llu] does not exist", __func__, kernelHandle),
        CcuResult::CCU_E_NOT_FOUND);

    kernelMap_.erase(kernelHandle);
    return CcuResult::CCU_SUCCESS;
}

static void MergeCcuResReq(HcommCcuResRequestPod& resReqA, const HcommCcuResRequestPod& resReqB)
{
    // 多个 translator/reference manager 共用一次批量申请，按“资源类型 + DIE”累加请求数量。
    // 仅当本次确实申请 mission 时才传播其模式，避免空请求覆盖已有模式。
    for (uint32_t i = 0; i < CCU_MAX_IODIE_NUM; i++) {
        resReqA.count[HCOMM_CCU_BATCH_RES_MS][i] += resReqB.count[HCOMM_CCU_BATCH_RES_MS][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i] += resReqB.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_CKE][i] += resReqB.count[HCOMM_CCU_BATCH_RES_CKE][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i] += resReqB.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_LOOP][i] += resReqB.count[HCOMM_CCU_BATCH_RES_LOOP][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i] += resReqB.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_GSA][i] += resReqB.count[HCOMM_CCU_BATCH_RES_GSA][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i] += resReqB.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_XN][i] += resReqB.count[HCOMM_CCU_BATCH_RES_XN][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i] += resReqB.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i];
        resReqA.count[HCOMM_CCU_BATCH_RES_MISSION][i] += resReqB.count[HCOMM_CCU_BATCH_RES_MISSION][i];

        if (resReqB.count[HCOMM_CCU_BATCH_RES_MISSION][i] > 0) {
            resReqA.missionReqType = resReqB.missionReqType;
        }
    }
}

HcclResult CcuKernelMgr::InstantiationTranslator(const uint16_t dieId, const CcuResPack &resPack)
{
    const HcommCcuDieMetadataPod *metadata = resPack.GetDieMetadata(dieId);
    CHK_PTR_NULL(metadata);
    Hccl::DevBuffer tmpDevMem{1}; // 临时申请device hbm内存用于查询token信息
    auto hbmTokenInfo = hcomm::CcuRep::GetTokenInfo(tmpDevMem.GetAddr(), 1);
    CcuRep::TransDep transDep{};
    transDep.logicalId = resPack.GetDeviceLogicId();
    transDep.dieId = dieId;
    transDep.reserveChannalId[0] = static_cast<uint16_t>(metadata->innerDieLoopChannelId);
    transDep.reserveChannalId[1] = static_cast<uint16_t>(metadata->interDieLoopChannelId);
    for (uint32_t metadataDie = 0; metadataDie < HCOMM_CCU_MAX_DIE_NUM; ++metadataDie) {
        const HcommCcuDieMetadataPod *dieMetadata = resPack.GetDieMetadata(metadataDie);
        if (dieMetadata != nullptr) {
            transDep.xnBaseAddr[metadataDie] = dieMetadata->xnBaseAddr;
        }
    }
    transDep.ccuResSpaceTokenInfo = CcuRep::GetToken(
        metadata->resourceSpaceTokenId, metadata->resourceSpaceTokenValue, 1);
    transDep.memTokenInfo = hbmTokenInfo;

    // 实例化CcuRepReferenceManager和CcuRepTranslator，并为CcuRepReferenceManager绑定物理资源
    for (uint32_t i = 0; i < 16; i++) { // mgr有16个
        referenceMgrs[dieId][i] = std::make_shared<hcomm::CcuRep::CcuRepReferenceManager>(dieId);
        translators[dieId][i] =
            std::make_shared<hcomm::CcuRep::CcuRepTranslator>(referenceMgrs[dieId][i], transDep);
    }

    CcuRepResource translatorRepRes;
    for (uint32_t i = 0; i < 16; i++) { // mgr有16个
        referenceMgrs[dieId][i]->GetRes(translatorRepRes);
        translators[dieId][i]->GetRes(translatorRepRes);
    }

    AscCcuResRepository totalResRepository;
    CHK_RET(ExpandResRepo(totalResRepository, resPack.GetTranslatorResRepo()));
    // 将kernel中的rep虚拟资源按类型进行和CCU物理资源映射
    CHK_RET(ResetRepResourceToResRepository(translatorRepRes, totalResRepository));
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuKernelMgr::LoadInstruction(const CcuRep::CcuInstrInfo& instrInfo, const uint32_t dieId)
{
    const uint64_t instrInfoSize = instrInfo.instrVec.size() * sizeof(hcomm::CcuRep::CcuInstr);

    if (instrInfoSize == 0) {
        return HcclResult::HCCL_E_PARA;
    }
    if (instructionLoadDevMemSize_ < instrInfoSize) {
        if (instructionLoadDevMem_ != nullptr) {
            CHK_RET(hrtFree(instructionLoadDevMem_));
            instructionLoadDevMem_ = nullptr;
            instructionLoadDevMemSize_ = 0;
        }
        CHK_RET(hrtMalloc(&instructionLoadDevMem_, instrInfoSize));
        instructionLoadDevMemSize_ = instrInfoSize;
    }

    CHK_RET(hrtMemcpy(
        instructionLoadDevMem_, instrInfoSize, instrInfo.instrVec.data(), instrInfoSize,
        HcclRtMemcpyKind::HCCL_RT_MEMCPY_KIND_HOST_TO_DEVICE));

    uint32_t devPhyId = 0;
    CHK_RET(hrtGetDevicePhyIdByIndex(static_cast<uint32_t>(devLogicId_), devPhyId));

    CustomChannelInfoIn inBuff{};
    CustomChannelInfoOut outBuff{};

    // 设置操作码和通道数据
    inBuff.op = CCU_U_OP_SET_INSTRUCTION;
    inBuff.offsetStartIdx = instrInfo.startInstrId;
    inBuff.data.dataInfo.udieIdx = dieId;
    inBuff.data.dataInfo.dataArraySize = 1;
    inBuff.data.dataInfo.dataLen = instrInfoSize;

    CcuDataTypeUnion tmp{};
    tmp.insinfo.resourceAddr = reinterpret_cast<uint64_t>(instructionLoadDevMem_);
    (void)memcpy_s(inBuff.data.dataInfo.dataArray, sizeof(CcuDataTypeUnion), &tmp, sizeof(CcuDataTypeUnion));

    auto ret = HccpRaTlvCcuCustomChannel(devLogicId_, static_cast<void*>(&inBuff), static_cast<void*>(&outBuff));
    if (ret != HCCL_SUCCESS) {
        HCCL_ERROR(
            "[CcuResSpecifications][%s] failed to call ccu driver, "
            "devLogicId[%d] devPhyId[%u] dieId[%d] op[%s] ret[%d].",
            __func__, devLogicId_, devPhyId, dieId, "SET_INSTRUCTION", ret);
        return ret;
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuKernelMgr::TransRepSequenceToMicrocode(const std::vector<CcuKernel*>& kernels, bool isFuncBlock)
{
    for (auto kernel : kernels) {
        const uint32_t dieId = kernel->GetDieId();
        const uint32_t missionId = kernel->GetMissionId();

        EXCEPTION_HANDLE_BEGIN
        const auto& instrInfo = translators[dieId][missionId]->Translate(
            kernel, kernel->GetRepSequence(), kernel->GetInstrId(), isFuncBlock);

        CHK_RET(LoadInstruction(instrInfo, dieId));

        kernel->SetCcuInstrInfo(instrInfo); // 指令下发成功后可以对kernel进行launch
        EXCEPTION_HANDLE_END
    }

    return HcclResult::HCCL_SUCCESS;
}

CcuKernel* CcuKernelMgr::GetKernel(const CcuKernelHandle kernelHandle)
{
    std::unique_lock<std::mutex> lock(kernelMapMutex_);
    auto it = kernelMap_.find(kernelHandle);
    if (it == kernelMap_.end()) {
        HCCL_ERROR("[CcuKernelMgr][%s] handle[%llx] is not existed.", __func__, kernelHandle);
        return nullptr;
    }

    return it->second.get();
}

CcuResult CcuKernelMgr::GetCcuKernelInfo(const CcuKernelHandle kernelHandle, CcuKernelInfo& info)
{
    std::unique_lock<std::mutex> lock(kernelMapMutex_);
    auto it = kernelMap_.find(kernelHandle);
    if (it == kernelMap_.end()) {
        HCCL_ERROR("[CcuKernelMgr][%s] handle[%llx] is not existed.", __func__, kernelHandle);
        return CcuResult::CCU_E_NOT_FOUND;
    }
    CCU_CHK_RET(it->second->GetCcuKernelInfo(info));
    return CcuResult::CCU_SUCCESS;
}

CcuKernel* CcuKernelMgr::GetCurrentKernel() { return currKernel_.get(); }

} // namespace hcomm
