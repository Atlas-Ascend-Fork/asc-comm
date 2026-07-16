/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_PRIMITIVES_IMPL_H
#define CCU_PRIMITIVES_IMPL_H

#ifdef __cplusplus
#include <cstdbool>
#else
#include <stdbool.h>
#endif // __cplusplus

#include "hccl_types.h"
#include "ccu_types.h"
#include "hcomm_primitives.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

//Alloc 相关接口
extern CcuResult CcuVariableAlloc(CcuVariableHandle *varHandle);
extern CcuResult CcuAddressAlloc(CcuAddressHandle *addrHandle);
extern CcuResult CcuEventAlloc(CcuEventHandle *eventHandle);
extern CcuResult CcuBufferAlloc(CcuBufferHandle *bufHandle);
extern CcuResult CcuLocalAddrAlloc(CcuLocalAddrHandle *localAddrHandle, CcuAddressHandle *addrHandle, CcuVariableHandle *tokenHandle);
extern CcuResult CcuRemoteAddrAlloc(CcuRemoteAddrHandle *remoteAddrHandle, CcuAddressHandle *addrHandle, CcuVariableHandle *tokenHandle);

//BlockAlloc 相关接口
extern CcuResult CcuBlockVariableAlloc(CcuVariableHandle *varHandles, uint32_t count);
extern CcuResult CcuBlockEventAlloc(CcuEventHandle *eventHandles, uint32_t count);
extern CcuResult CcuBlockBufferAlloc(CcuBufferHandle *bufHandles, uint32_t count);

extern CcuResult CcuVariableCreateByChannel(ChannelHandle channel,
    uint32_t varIndex, CcuVariableHandle *varHandle);

//Variable操作类 相关接口
extern CcuResult CcuVariableAssignImm(CcuVariableHandle resVar, uint64_t immediate);
extern CcuResult CcuVariableAssignVar(CcuVariableHandle dstVarHandle, CcuVariableHandle srcVarHandle);
extern CcuResult CcuVariableAddVarToVar(CcuVariableHandle resVar, CcuVariableHandle varA, CcuVariableHandle varB);

//Address操作类 相关接口
extern CcuResult CcuAddressAssignImm(CcuAddressHandle addr, uint64_t immediate);
extern CcuResult CcuAddressAssignAddr(CcuAddressHandle dstAddrHandle, CcuAddressHandle srcAddrHandle);
extern CcuResult CcuAddressAssignVar(CcuAddressHandle addr, CcuVariableHandle var);
extern CcuResult CcuAddressAddVarToAddr(CcuAddressHandle resAddr, CcuAddressHandle lhsAddr, CcuVariableHandle rhsVar);
extern CcuResult CcuAddressAddAddrToAddr(CcuAddressHandle resAddr, CcuAddressHandle addrA, CcuAddressHandle addrB);
extern CcuResult CcuAddressAddAssignVar(CcuAddressHandle addr, CcuVariableHandle var);

//参数加载类 相关接口
extern CcuResult CcuLoadArg(CcuVariableHandle varHandle, uint32_t argId);
extern CcuResult CcuLoadVar(uint64_t addr, CcuVariableHandle varHandle, uint32_t num);
extern CcuResult CcuLoadVarFromVarAddr(CcuVariableHandle addrHandle, CcuVariableHandle varHandle, uint32_t num);
extern CcuResult CcuStoreVar(uint64_t addr, CcuVariableHandle varHandle, uint32_t num);
extern CcuResult CcuStoreVarToVarAddr(CcuVariableHandle addrHandle, CcuVariableHandle varHandle, uint32_t num);

//Event信号同步类 相关接口
// mask 由调用方独立传入（与 Event 句柄解耦）；CcuSetMask 已废弃删除。
extern CcuResult CcuEventRecord(CcuEventHandle eventHandle, uint16_t mask);
extern CcuResult CcuEventWait(CcuEventHandle eventHandle, uint16_t mask);
extern CcuResult CcuNotifyRecord(ChannelHandle channel, uint32_t remoteNotifyIdx, uint16_t mask);
extern CcuResult CcuNotifyWait(ChannelHandle channel, uint32_t localNotifyIdx, uint16_t mask);
extern CcuResult CcuWriteVariableWithNotify(ChannelHandle channel, CcuVariableHandle varHandle,uint32_t remoteVarIdx, uint32_t remoteNotifyIdx, uint16_t mask);
// 本地（同 device 内跨 core）通知同步：与 CcuNotifyRecord/Wait 的区别在于
// 通知对端用 coreId 标识（同卡内某个 core），而不是 ChannelHandle（跨 rank 通道）。
extern CcuResult CcuLocalNotifyRecord(const char *notifyTag, uint16_t mask);
extern CcuResult CcuLocalNotifyWait(const char *notifyTag, uint16_t mask);

//本地数据拷贝 相关接口
extern CcuResult CcuLocalCopyMemToMem(CcuLocalAddrHandle dst, CcuLocalAddrHandle src, CcuVariableHandle len, CcuEventHandle event, uint16_t mask);
extern CcuResult CcuLocalCopyMemToBuffer(CcuBufferHandle dst, CcuLocalAddrHandle src,CcuVariableHandle len, CcuEventHandle event, uint16_t mask);
extern CcuResult CcuLocalCopyBufferToMem(CcuLocalAddrHandle dst, CcuBufferHandle src, CcuVariableHandle len, CcuEventHandle event, uint16_t mask);
//本地reduce 相关接口
extern CcuResult CcuLocalMemReduce(CcuLocalAddrHandle dst, CcuLocalAddrHandle src, CcuVariableHandle len, HcclDataType dataType, HcclReduceOp opType, CcuEventHandle event, uint16_t mask);
extern CcuResult CcuLocalBufferReduce(CcuBufferHandle* buffers, uint32_t count, HcclDataType dataType, HcclDataType outputDataType, HcclReduceOp opType, CcuVariableHandle len, CcuEventHandle event, uint16_t mask);

/*========== 远端数据传输操作 ==========*/
extern CcuResult CcuReadMemToMem(ChannelHandle channel, CcuLocalAddrHandle localHandle, CcuRemoteAddrHandle remoteHandle, CcuVariableHandle len, CcuEventHandle event, uint16_t mask);
extern CcuResult CcuReadMemToBuffer(ChannelHandle channel, CcuBufferHandle localHandle, CcuRemoteAddrHandle remoteHandle, CcuVariableHandle len, CcuEventHandle event, uint16_t mask);
extern CcuResult CcuReadMemToMemReduce(ChannelHandle channel, CcuLocalAddrHandle localHandle, CcuRemoteAddrHandle remoteHandle, CcuVariableHandle len, HcclDataType dataType, HcclReduceOp opType, CcuEventHandle event, uint16_t mask);
extern CcuResult CcuWriteMemToMem(ChannelHandle channel, CcuRemoteAddrHandle remoteHandle, CcuLocalAddrHandle localHandle, CcuVariableHandle len, CcuEventHandle event, uint16_t mask);
extern CcuResult CcuWriteBufferToMem(ChannelHandle channel, CcuRemoteAddrHandle remoteHandle, CcuBufferHandle localHandle, CcuVariableHandle len, CcuEventHandle event, uint16_t mask);
extern CcuResult CcuWriteMemToMemReduce(ChannelHandle channel, CcuRemoteAddrHandle remoteHandle, CcuLocalAddrHandle localHandle, CcuVariableHandle len, HcclDataType dataType, HcclReduceOp opType, CcuEventHandle event, uint16_t mask);

/*========== 控制流操作 ==========*/
extern CcuResult CcuIfBegin(CcuVariableHandle var, uint64_t immediate, CcuConditionType condType, const char *label);
extern CcuResult CcuIfElse(const char *label);
extern CcuResult CcuIfEnd(const char *label);
extern CcuResult CcuFlushPendingIfs();
extern CcuResult CcuWhileBegin(CcuVariableHandle var, uint64_t immediate, CcuConditionType condType, const char *label);
extern CcuResult CcuWhileEnd(const char *label);
extern CcuResult CcuDoWhileBegin(const char *label);
extern CcuResult CcuDoWhileEnd(CcuVariableHandle var, uint64_t immediate, CcuConditionType condType, const char *label);

/*========== 函数调用操作 ==========*/
extern CcuResult CcuFuncBlockLookup(const void *funcPtr, uint64_t *outHandle);
extern CcuResult CcuFuncBlockBegin(const void *funcPtr, uint64_t *outHandle);
extern CcuResult CcuFuncBlockEnd(uint64_t handle);
extern CcuResult CcuFuncDefineInArg(uint64_t handle, CcuVariableHandle formal);
extern CcuResult CcuFuncCall(uint64_t handle,
    const CcuVariableHandle *inArgs, uint32_t numIn);

/*
 * 控制流宏内部使用的标签栈接口（以 _ 前缀标识为内部 API，
 * 仅供 ccu_control_flow_macro.h 中的 CCU_IF / CCU_ELSE / CCU_DO / CCU_WHILE
 * 等宏在调用现场展开时使用）。
 */
extern void _CcuIfStackPush(const char *label);
extern void _CcuIfStackMarkBodyDone();
extern const char *_CcuIfStackPopForElse();

extern void _CcuDoWhileStackPush(const char *label);
extern const char *_CcuDoWhileStackPopForWhile();

/*========== 循环操作 ==========*/
extern CcuResult CcuLoopCreate(CcuLoop *loop);
extern CcuResult _CcuLoopBodyEnter(CcuLoop loop);
extern CcuResult _CcuLoopBodyExit(CcuLoop loop);
// LoopGroup 创建时按需扩容 LoopEngine 池：传入本 group 实际要 AddLoop 的次数
// （含展开复用）。各 LoopGroup 之间通过 local loopIdx 复用低位 executorId，
// 因此池子按"取最大值"被动扩容，不会按组累加。
extern CcuResult CcuLoopGroupCreate(CcuLoopGroup *group, uint32_t maxLoopNum,
    const CcuLoopGroupConfig *config);
extern CcuResult CcuLoopGroupCreateFromVar(CcuLoopGroup *group, uint32_t maxLoopNum,
    CcuVariableHandle parallelVar, CcuVariableHandle offsetVar);
extern CcuResult CcuLoopGroupAddLoop(CcuLoopGroup group,
    CcuLoop loop, const CcuLoopConfig *config);
extern CcuResult CcuLoopGroupAddLoopFromVar(CcuLoopGroup group,
    CcuLoop loop, CcuVariableHandle loopParamVar);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CCU_PRIMITIVES_IMPL_H