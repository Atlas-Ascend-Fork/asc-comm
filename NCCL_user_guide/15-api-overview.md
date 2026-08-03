# 15 · NCCL API 总览

> 原文：NCCL API

本章是 API 参考的目录页，列出 NCCL 提供的方法和操作。以下为分类清单，各类的详细说明见后续文件。

## Communicator 创建与管理（→ `16`）

错误与版本查询：`ncclGetLastError`、`ncclGetErrorString`、`ncclGetVersion`

创建：`ncclGetUniqueId`、`ncclCommInitRank`、`ncclCommInitAll`、`ncclCommInitRankConfig`、`ncclCommInitRankScalable`

派生：`ncclCommSplit`、`ncclCommShrink`、`ncclCommGetUniqueId`、`ncclCommGrow`、`ncclCommRevoke`

销毁：`ncclCommFinalize`、`ncclCommDestroy`、`ncclCommAbort`

状态查询：`ncclCommGetAsyncError`、`ncclCommCount`、`ncclCommCuDevice`、`ncclCommUserRank`

内存注册：`ncclCommRegister`、`ncclCommDeregister`、`ncclCommWindowRegister`、`ncclCommWindowDeregister`

内存分配：`ncclMemAlloc`、`ncclMemFree`

挂起/恢复与统计：`ncclCommSuspend`、`ncclCommResume`、`ncclCommMemStats`

## 集合通信函数（→ `17`）

`ncclAllReduce`、`ncclBroadcast`、`ncclReduce`、`ncclAllGather`、`ncclReduceScatter`、`ncclAlltoAll`、`ncclGather`、`ncclScatter`

## Group Calls（→ `18`）

`ncclGroupStart`、`ncclGroupEnd`、`ncclGroupSimulateEnd`

## 点对点通信函数（→ `19`）

- 双边点对点操作
- 单边点对点操作（RMA）

## Types（→ `20`）

`ncclComm_t`、`ncclResult_t`、`ncclDataType_t`、`ncclRedOp_t`、`ncclScalarResidence_t`、`ncclConfig_t`、`ncclSimInfo_t`、`ncclCommMemStat_t`、`ncclWindow_t`

## 用户自定义归约算子（→ `21`）

`ncclRedOpCreatePreMulSum`、`ncclRedOpDestroy`

## API 支持的 Flags（→ `22`）

- Window Registration Flags
- NCCL Communicator CTA Policy Flags
- Communicator Shrink Flags

## Device API（→ `23`–`27`）

- Device API – Host-Side Setup（`24`）
- Device API – Memory and LSA（`25`）
- Device API – GIN（`26`）
- Device API – Remote Reduce and Copy：自定义通信 kernel 的构件（`27`）

## NCCL Parameter API（→ `28`）

- Types
- Handle-Based API
- Key-Based API
