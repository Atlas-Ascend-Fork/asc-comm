/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AICPU_COMMON_KERNEL_LOADER_H
#define AICPU_COMMON_KERNEL_LOADER_H

#include <climits>
#include <string>

thread_local aclrtBinHandle g_binKernelHandle = nullptr;

static HcclResult GetKernelFilePath(std::string& binaryPath)
{
    std::string libPath;
    char* getPath = std::getenv("ASCEND_HOME_PATH");
    if (getPath != nullptr) {
        libPath = getPath;
    } else {
        libPath = "/usr/local/Ascend/cann/";
        HCCL_WARNING("[GetKernelFilePath]ENV:ASCEND_HOME_PATH is not set");
    }

    libPath += "/opp/vendors/cust/aicpu/config/";
    binaryPath = libPath;
    HCCL_DEBUG("[GetKernelFilePath]kernel folder path[%s]", binaryPath.c_str());
    return HCCL_SUCCESS;
}

static HcclResult LoadBinaryFromFile(
    const char* binPath, aclrtBinaryLoadOptionType optionType, uint32_t cpuKernelMode, aclrtBinHandle& binHandle)
{
    CHK_PRT_RET(binPath == nullptr, HCCL_ERROR("[Load][Binary]binary path is nullptr"), HCCL_E_PTR);

    char realPath[PATH_MAX] = {0};
    CHK_PRT_RET(
        realpath(binPath, realPath) == nullptr,
        HCCL_ERROR("LoadBinaryFromFile: %s is not a valid real path, err[%d]", binPath, errno), HCCL_E_INTERNAL);
    HCCL_INFO("[LoadBinaryFromFile] realPath: %s", realPath);

    aclrtBinaryLoadOptions loadOptions = {0};
    aclrtBinaryLoadOption option;
    loadOptions.numOpt = 1;
    loadOptions.options = &option;
    option.type = optionType;
    option.value.cpuKernelMode = cpuKernelMode;
    aclError aclRet =
        aclrtBinaryLoadFromFile(realPath, &loadOptions, &binHandle); // ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE
    CHK_PRT_RET(
        aclRet != ACL_SUCCESS, HCCL_ERROR("[LoadBinaryFromFile] load binary from file error, ret[%d]", aclRet),
        HCCL_E_OPEN_FILE_FAILURE);
    return HCCL_SUCCESS;
}

inline HcclResult LoadAICPUKernel(const char* kernelConfigName, aclrtBinHandle& binHandle)
{
    if (binHandle != nullptr) {
        return HCCL_SUCCESS;
    }
    std::string jsonPath;
    CHK_RET(GetKernelFilePath(jsonPath));
    jsonPath += kernelConfigName;
    constexpr uint32_t CPU_KERNEL_MODE_JSON_ONLY = 0;
    HcclResult ret = LoadBinaryFromFile(
        jsonPath.c_str(), ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE, CPU_KERNEL_MODE_JSON_ONLY, binHandle);
    CHK_PRT_RET(
        ret != HCCL_SUCCESS,
        HCCL_ERROR(
            "[LoadAICPUKernel] load aicpu file fail, ret[%d], path[%s], optionType[%u], "
            "cpuKernelMode[%u].",
            ret, jsonPath.c_str(), ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE, CPU_KERNEL_MODE_JSON_ONLY),
        ret);
    return HCCL_SUCCESS;
}

#endif // AICPU_COMMON_KERNEL_LOADER_H
