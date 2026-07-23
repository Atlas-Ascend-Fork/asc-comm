/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_DATAPLANE_ABI_H
#define ASCCOMM_CCU_DATAPLANE_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASCCOMM_CCU_DATAPLANE_ABI_VERSION 2U
#define ASCCOMM_CCU_MAX_DIE_NUM 2U
#define ASCCOMM_CCU_TASK_ARG_NUM 13U
#define ASCCOMM_CCU_MAX_LAUNCH_TASKS ASCCOMM_CCU_MAX_DIE_NUM

typedef uint64_t AscCommCcuChannelHandle;
typedef uint64_t AscCommCcuThreadHandle;
typedef uint64_t AscCommCcuInsHandle;
typedef uint64_t AscCommCcuKernelHandle;
typedef uint32_t AscCommCcuResult;

typedef struct {
    uint64_t tokenId;
    uint64_t tokenValue;
} AscCommCcuTokenInfo;

typedef struct {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t dieId;
    uint32_t channelId;
    uint32_t entryCount;
    uint32_t reserved;
    const void *entries;
    uint64_t remoteAddress;
    uint32_t remoteSize;
    uint32_t remoteTokenId;
    uint32_t remoteTokenValue;
} AscCommCcuChannelSnapshot;

typedef struct {
    uint32_t localCkeId;
    uint32_t localXnId;
    uint32_t remoteCkeId;
    uint32_t remoteXnId;
} AscCommCcuChannelEntry;

typedef struct {
    uint32_t dieId;
    uint32_t missionId;
    uint32_t instructionStartId;
    uint32_t instructionCount;
    uint32_t missionKey;
    uint32_t argumentSize;
    uint32_t timeoutSeconds;
    uint64_t arguments[ASCCOMM_CCU_TASK_ARG_NUM];
} AscCommCcuTask;

typedef struct {
    void *stream;
    uint32_t deviceLogicId;
    uint32_t timeoutSeconds;
    uint8_t isMaster;
    uint8_t profilingLevel0;
    uint8_t profilingLevel1;
    uint8_t isOpbase;
} AscCommCcuLaunchContext;

typedef struct {
    AscCommCcuTask task;
    uint32_t streamId;
    uint32_t taskId;
} AscCommCcuTaskInfo;

typedef struct {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t taskCount;
    uint32_t reserved;
    AscCommCcuTaskInfo tasks[ASCCOMM_CCU_MAX_LAUNCH_TASKS];
} AscCommCcuLaunchResult;

typedef struct AscCommCcuHostServicesV1 {
    uint32_t abiVersion;
    uint32_t structSize;
    void *userData;
    AscCommCcuResult (*queryMemoryToken)(
        void *userData, uint64_t address, uint64_t size, uint64_t *tokenId, uint64_t *tokenValue);
    AscCommCcuResult (*getLaunchContext)(
        void *userData, AscCommCcuThreadHandle thread, AscCommCcuLaunchContext *context);
    AscCommCcuResult (*reportTask)(void *userData, AscCommCcuThreadHandle thread, uint32_t streamId, uint32_t taskId,
        AscCommCcuKernelHandle kernelHandle, const AscCommCcuTask *task);
} AscCommCcuHostServicesV1;

typedef struct {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t deviceLogicId;
    uint32_t instanceType;
    uint8_t dieEnabled[ASCCOMM_CCU_MAX_DIE_NUM];
    uint8_t reserved[6];
    uint64_t xnBaseAddress[ASCCOMM_CCU_MAX_DIE_NUM];
    uint32_t missionKey[ASCCOMM_CCU_MAX_DIE_NUM];
    uint32_t loopChannelId[ASCCOMM_CCU_MAX_DIE_NUM][ASCCOMM_CCU_MAX_DIE_NUM];
    AscCommCcuTokenInfo resourceSpaceToken[ASCCOMM_CCU_MAX_DIE_NUM];
    AscCommCcuTokenInfo deviceMemoryToken;
    const AscCommCcuHostServicesV1 *hostServices;
} AscCommCcuInstanceCreateParam;

typedef struct {
    uint32_t abiVersion;
    uint32_t structSize;
    AscCommCcuResult (*instanceCreate)(const void *param, void *insHandle);
    AscCommCcuResult (*instanceDestroy)(AscCommCcuInsHandle insHandle, uint32_t deviceLogicId);
    AscCommCcuResult (*upsertChannel)(AscCommCcuChannelHandle channel, const void *snapshot);
    AscCommCcuResult (*removeChannel)(AscCommCcuChannelHandle channel);
} AscCommCcuDataPlaneV1;

#pragma GCC visibility push(default)
const AscCommCcuDataPlaneV1 *AscCommCcuGetDataPlaneV1(void);
#pragma GCC visibility pop

#ifdef __cplusplus
}

static_assert(sizeof(AscCommCcuResult) == sizeof(uint32_t), "AscCommCcuResult ABI changed");
static_assert(sizeof(AscCommCcuInsHandle) == sizeof(uint64_t), "AscCommCcuInsHandle ABI changed");
static_assert(sizeof(AscCommCcuKernelHandle) == sizeof(uint64_t), "AscCommCcuKernelHandle ABI changed");
static_assert(offsetof(AscCommCcuDataPlaneV1, instanceDestroy) > offsetof(AscCommCcuDataPlaneV1, instanceCreate),
    "AscCommCcuDataPlaneV1 ABI changed");
static_assert(offsetof(AscCommCcuHostServicesV1, getLaunchContext)
        > offsetof(AscCommCcuHostServicesV1, queryMemoryToken),
    "AscCommCcuHostServicesV1 ABI changed");
#endif

#endif
