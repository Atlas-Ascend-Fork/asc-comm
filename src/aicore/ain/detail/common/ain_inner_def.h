/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message("This is an internal Ain header file and must not be used directly. " \
                "Please use public interface headers.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_INNER_DEF_H__
#endif

#ifndef SRC_AIN_DETAIL_COMMON_AIN_INNER_DEF_H
#define SRC_AIN_DETAIL_COMMON_AIN_INNER_DEF_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct {
    CommAbiHeader header;
    uint32_t memsNum;
    CommMem* mems;
    HcommTeamHandle worldTeam;
    uint32_t reserved[8];
} HcommWindow;

typedef struct {
    CommMem* remoteMems;
    uint32_t remoteMemsNum;
    CommMem shadowMem;
    HcommTeamSyncMemRequirement syncMemReq;
    uint64_t syncMemSize;
    uint32_t reserved[5];
} HcommTeamSyncMem;

typedef struct {
    CommAbiHeader header;
    CommEngine engine;
    uint32_t memberNum;
    uint32_t selfMemberId;
    uint64_t channelsBaseAddr;
    uint32_t* channelNumPerMember;
    uint32_t netLayer;
    uint32_t* worldTeamIds;
    HcommTeamSyncMem syncMem;
    uint32_t reserved[8];
} HcommTeam;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // SRC_AIN_DETAIL_COMMON_AIN_INNER_DEF_H

#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_INNER_DEF_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_INNER_DEF_H__
#endif
