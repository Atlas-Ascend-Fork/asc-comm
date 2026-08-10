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

#include <cstddef>
#include <limits>

#include "string_util.h"
#include "adapter_rts.h"
#include "hcomm/resource/microcode/ccu_assist_v1.h"
#include "hcomm/common/ccu_dev_mem.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_type_v1.h"

#include "hcomm/hcomm_ccu_instruction_api.h"

#include "hcomm/common/ccu_log.h"
#include "hcomm/resource/common/ccu_kernel_func.h"

namespace hcomm {

namespace {

CcuResult SetRequestCount(uint32_t &target, size_t value)
{
    if (value > std::numeric_limits<uint32_t>::max()) {
        return CcuResult::CCU_E_INTERNAL;
    }
    target = static_cast<uint32_t>(value);
    return CcuResult::CCU_SUCCESS;
}

CcuResult BuildTranslatorResourceRequest(uint32_t dieId, CcuVersion ccuVersion, AscCcuResRequest &request)
{
    request = {};
    if (dieId >= HCOMM_CCU_MAX_DIE_NUM) {
        return CcuResult::CCU_E_PARA;
    }

    constexpr size_t slotNum = CcuRep::CCU_TRANSLATOR_MISSION_SLOT_NUM;
    constexpr size_t xnPerSlot = CcuRep::CCU_REFERENCE_MANAGER_XN_NUM + CcuRep::CCU_TRANSLATOR_XN_NUM;
    constexpr size_t ckePerSlot = CcuRep::CCU_TRANSLATOR_CKE_NUM;
    constexpr size_t gsaPerSlot = CcuRep::CCU_TRANSLATOR_GSA_NUM;

    CCU_CHK_RET(SetRequestCount(request.count[HCOMM_CCU_BATCH_RES_XN][dieId], slotNum * xnPerSlot));
    CCU_CHK_RET(SetRequestCount(request.count[HCOMM_CCU_BATCH_RES_CKE][dieId], slotNum * ckePerSlot));
    if (ccuVersion == CcuVersion::CCU_V1) {
        CCU_CHK_RET(SetRequestCount(request.count[HCOMM_CCU_BATCH_RES_GSA][dieId], slotNum * gsaPerSlot));
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult ClearKernelInstructionResource(CcuKernel &kernel)
{
    HcommCcuResRangePod range{};
    if (!kernel.GetInstructionResource(range)) {
        return CcuResult::CCU_SUCCESS;
    }
    kernel.ClearInstructionResource();
    return CcuResult::CCU_SUCCESS;
}

} // namespace

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

void EnsureCcuKernelMgrConstructed(int32_t deviceLogicId)
{
    (void)CcuKernelMgr::GetInstance(deviceLogicId);
}

HcclResult CcuKernelMgr::Init()
{
    std::unique_lock<std::mutex> lock(kernelMapMutex_);
    initializedFlag_ = true;
    kernelMap_.clear();
    return HcclResult::HCCL_SUCCESS;
}

CcuResult CcuKernelMgr::Configure(AscCcuResSnapshot &resPack)
{
    std::unique_lock<std::mutex> lock(kernelMapMutex_);
    devLogicId_ = resPack.GetDeviceLogicId();
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
        HcclResult transRet = InstantiationTranslator(static_cast<uint16_t>(dieId), resPack);
        if (transRet != HcclResult::HCCL_SUCCESS) {
            translators.clear();
            referenceMgrs.clear();
            return static_cast<CcuResult>(transRet);
        }
    }
    insGenePtr = std::make_shared<CcuRep::CcuInsGeneraterV1>();
    initializedFlag_ = true;
    return CcuResult::CCU_SUCCESS;
}

HcclResult CcuKernelMgr::Deinit()
{
    std::unique_lock<std::mutex> lock(kernelMapMutex_);
    HcclResult firstError = HcclResult::HCCL_SUCCESS;
    for (auto &item : kernelMap_) {
        if (item.second == nullptr) {
            continue;
        }
        const CcuResult ret = ClearKernelInstructionResource(*item.second);
        if (firstError == HcclResult::HCCL_SUCCESS && ret != CcuResult::CCU_SUCCESS) {
            firstError = static_cast<HcclResult>(ret);
        }
    }
    initializedFlag_ = false;
    kernelMap_.clear();
    translators.clear();
    referenceMgrs.clear();
    return firstError;
}

CcuResult CcuKernelMgr::Register(
    AscCcuResSnapshot& resPack, const uint32_t dieId, const char* kernelFuncName, const void* kernelFunc,
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

static void DumpResReqInfo(const AscCcuResRequest& totalRes)
{
    for (uint32_t i = 0; i < CCU_MAX_IODIE_NUM; i++) {
        if (totalRes.count[HCOMM_CCU_BATCH_RES_MS][i] != 0 || totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_CKE][i] != 0 || totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_LOOP][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i] != 0 || totalRes.count[HCOMM_CCU_BATCH_RES_GSA][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i] != 0 || totalRes.count[HCOMM_CCU_BATCH_RES_XN][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i] != 0 ||
            totalRes.count[HCOMM_CCU_BATCH_RES_MISSION][i] != 0 || totalRes.instruction[i] != 0) {
            HCCL_INFO(
                "DumpResReqInfo: dieId[%u], msReq[%u], blockMsReq[%u], ckeReq[%u], blockCkeReq[%u], "
                "loopEngineReq[%u], blockLoopEngineReq[%u], gsaReq[%u], blockGsaReq[%u], xnReq[%u], "
                "blockXnReq[%u], missionReq[%u], instructionReq[%u]",
                i, totalRes.count[HCOMM_CCU_BATCH_RES_MS][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_CKE][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_LOOP][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_GSA][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_XN][i], totalRes.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i],
                totalRes.count[HCOMM_CCU_BATCH_RES_MISSION][i], totalRes.instruction[i]);
        }
    }
}

static CcuResult SetInstrRequest(std::unique_ptr<CcuKernel>& kernel, AscCcuResRequest &request)
{
    const size_t instrCount = static_cast<size_t>(kernel->GetInstrCount()) +
        CcuRep::CcuRepTranslator::GetInstrNum() + kernel->GetConstValue2VarMap().size();
    const uint32_t dieId = kernel->GetDieId();
    return SetRequestCount(request.instruction[dieId], instrCount);
}

static CcuResult SetKernelInstructionResource(std::unique_ptr<CcuKernel>& kernel, const AscCcuResRepository &allocated)
{
    const uint32_t dieId = kernel->GetDieId();
    const auto &instructionRanges = allocated.instruction[dieId];
    if (instructionRanges.empty()) {
        HCCL_ERROR("[CcuKernelMgr][%s] failed, dieId[%u] does not have instruction resource.", __func__, dieId);
        return CcuResult::CCU_E_UNAVAIL;
    }
    const HcommCcuResRangePod &insInfo = instructionRanges.front();
    HCCL_INFO(
        "[CcuKernelMgr][%s]: dieId[%u], startId[%u], count[%u]", __func__, dieId, insInfo.startId, insInfo.count);
    kernel->SetInstrId(insInfo.startId);
    kernel->SetInstructionResource(insInfo);

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

CcuResult CcuKernelMgr::AllocRes(AscCcuResSnapshot& resPack)
{
    AscCcuResRequest resReq = currKernel_->GetResourceRequest();
    CCU_CHK_RET(SetInstrRequest(currKernel_, resReq));
    AscCcuResRepository remaining{};
    AscCcuResRepository allocated{};
    CcuResult planRet = PlanKernelResources(resPack.GetCcuResRepo(), resReq, remaining, allocated);
    if (planRet != CcuResult::CCU_SUCCESS) {
        DumpResReqInfo(resReq);
        HCCL_WARNING("[CcuKernelMgr][%s] resource is not enough.", __func__);
        return planRet;
    }

    CCU_CHK_RET(SetKernelInstructionResource(currKernel_, allocated));

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
            resRepo.xn[i].size() != 0 || resRepo.blockXn[i].size() != 0 || resRepo.mission[i].size() != 0 ||
            resRepo.instruction[i].size() != 0) {
            HCCL_INFO(
                "DumpResRepository: dieId[%u], ms size[%u], blockMs size[%u], cke size[%u], blockCke size[%u], "
                "loopEngine size[%u], blockLoopEngine size[%u], gsa size[%u], blockGsa size[%u], xn size[%u], "
                "blockXn size[%u], mission size[%u], instruction size[%u]",
                i, resRepo.ms[i].size(), resRepo.blockMs[i].size(), resRepo.cke[i].size(), resRepo.blockCke[i].size(),
                resRepo.loopEngine[i].size(), resRepo.blockLoopEngine[i].size(), resRepo.gsa[i].size(),
                resRepo.blockGsa[i].size(), resRepo.xn[i].size(), resRepo.blockXn[i].size(), resRepo.mission[i].size(),
                resRepo.instruction[i].size());
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
        ExpandResInfo(totalRes.instruction[i], tmpResRepository.instruction[i]);
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

    CCU_CHK_RET(ClearKernelInstructionResource(*it->second));
    kernelMap_.erase(kernelHandle);
    return CcuResult::CCU_SUCCESS;
}

HcclResult CcuKernelMgr::InstantiationTranslator(const uint16_t dieId, AscCcuResSnapshot &resPack)
{
    const HcommCcuDieMetadataPod *metadata = resPack.GetDieMetadata(dieId);
    CHK_PTR_NULL(metadata);
    // 临时申请device hbm内存用于查询token信息，由本地 RAII 对象管理生命周期
    CcuDevMem tmpDevMem{1};
    if (!tmpDevMem.Valid()) {
        HCCL_ERROR("[InstantiationTranslator] alloc temp device memory for token query failed.");
        return HcclResult::HCCL_E_MEMORY;
    }
    auto hbmTokenInfo = hcomm::CcuRep::GetTokenInfo(tmpDevMem.GetAddr(), tmpDevMem.GetSize());
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
    for (uint32_t i = 0; i < CcuRep::CCU_TRANSLATOR_MISSION_SLOT_NUM; i++) {
        referenceMgrs[dieId][i] = std::make_shared<hcomm::CcuRep::CcuRepReferenceManager>(dieId);
        translators[dieId][i] =
            std::make_shared<hcomm::CcuRep::CcuRepTranslator>(referenceMgrs[dieId][i], transDep);
    }

    CcuRepResource translatorRepRes;
    for (uint32_t i = 0; i < CcuRep::CCU_TRANSLATOR_MISSION_SLOT_NUM; i++) {
        referenceMgrs[dieId][i]->GetRes(translatorRepRes);
        translators[dieId][i]->GetRes(translatorRepRes);
    }

    AscCcuResRequest request{};
    CcuResult ccuRet = BuildTranslatorResourceRequest(dieId, ccuVersion_, request);
    if (ccuRet != CcuResult::CCU_SUCCESS) {
        return static_cast<HcclResult>(ccuRet);
    }

    AscCcuResRepository remainingRepository;
    AscCcuResRepository translatorResRepository;
    ccuRet = PlanKernelResources(resPack.GetCcuResRepo(), request, remainingRepository, translatorResRepository);
    if (ccuRet != CcuResult::CCU_SUCCESS) {
        DumpResReqInfo(request);
        HCCL_WARNING("[CcuKernelMgr][%s] translator resource is not enough, dieId[%u].", __func__, dieId);
        return static_cast<HcclResult>(ccuRet);
    }
    AscCcuResRepository expandedTranslatorResRepository;
    ccuRet = ExpandResRepo(expandedTranslatorResRepository, translatorResRepository);
    if (ccuRet != CcuResult::CCU_SUCCESS) {
        return static_cast<HcclResult>(ccuRet);
    }
    // 将translator中的rep虚拟资源按类型进行和CCU物理资源映射
    HcclResult hcclRet = ResetRepResourceToResRepository(translatorRepRes, expandedTranslatorResRepository);
    if (hcclRet != HcclResult::HCCL_SUCCESS) {
        return hcclRet;
    }
    resPack.GetCcuResRepo() = std::move(remainingRepository);
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

    // 只传语义参数；opcode 选择、TLV 报文组装与驱动交互均由 hcomm 在其 SO 内完成
    HcommCcuInstructionLoadPod request{};
    request.header.version = HCOMM_CCU_INSTRUCTION_ABI_VERSION;
    request.header.magicWord = HCOMM_CCU_INSTRUCTION_LOAD_MAGIC_WORD;
    request.header.size = sizeof(request);
    request.deviceLogicId = devLogicId_;
    request.dieId = dieId;
    request.startInstructionId = instrInfo.startInstrId;
    request.deviceAddress = reinterpret_cast<uint64_t>(instructionLoadDevMem_);
    request.byteSize = instrInfoSize;

    auto ret = static_cast<HcclResult>(HcommCcuLoadInstruction(&request));
    if (ret != HcclResult::HCCL_SUCCESS) {
        HCCL_ERROR(
            "[CcuKernelMgr][%s] failed to load instruction, "
            "devLogicId[%d] dieId[%u] startInstrId[%u] byteSize[%llu] ret[%d].",
            __func__, devLogicId_, dieId, instrInfo.startInstrId,
            static_cast<unsigned long long>(instrInfoSize), ret);
        return ret;
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuKernelMgr::TransRepSequenceToMicrocode(const std::vector<CcuKernel*>& kernels, bool isFuncBlock)
{
    for (auto kernel : kernels) {
        const uint32_t dieId = kernel->GetDieId();
        const uint32_t missionId = kernel->GetMissionId();

        try {
            const auto& instrInfo = translators[dieId][missionId]->Translate(
                kernel, kernel->GetRepSequence(), kernel->GetInstrId(), isFuncBlock);
            CHK_RET(LoadInstruction(instrInfo, dieId));
            kernel->SetCcuInstrInfo(instrInfo); // 指令下发成功后可以对kernel进行launch
        } catch (const ::AscendC::ccu::detail::CcuException& exception) {
            HCCL_ERROR("[%s] exception: %s", __func__, exception.what());
            return static_cast<HcclResult>(exception.code());
        } catch (const std::exception& exception) {
            HCCL_ERROR("[%s] exception: %s", __func__, exception.what());
            return HcclResult::HCCL_E_INTERNAL;
        } catch (...) {
            HCCL_ERROR("[%s] unknown exception", __func__);
            return HcclResult::HCCL_E_INTERNAL;
        }
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
