# CCU Builtin CheckOpResSufficient错误码验证样例

## 概述

本样例基于[01_allgather](../01_allgather)复制改造，专门用于验证`CheckOpResSufficient(comm, ccType, ccArgs)`
（ccArgs版）各错误码的返回行为，便于与mc2Tiling版接口做功能对比。

样例在每个rank上依次执行一组错误码用例，rank 0汇总打印用例名、期望值、实际值和判定结果。
用例按ccArgs版调用链的触发条件设计（见`hccl_mc2.cc`的`HcclAllocCcResByArgsImpl`）。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
04_check_res_errcode
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧错误码用例驱动
```

## 用例清单

| 用例 | 构造方法 | 期望错误码 | 触发点 |
| --- | --- | --- | --- |
| NullComm | `comm`传nullptr，其余合法 | 2 (HCCL_E_PTR) | check入口`CHK_PTR_NULL` |
| NullCcArgs | `ccArgs`传nullptr，其余合法 | 2 (HCCL_E_PTR) | check入口`CHK_PTR_NULL` |
| SetEngine=AIV | `Mc2SetCcCommEngine(args, 3)` | 5 (HCCL_E_NOT_SUPPORT) | Set接口引擎白名单（非法引擎构造不出合法参数） |
| SetSrcDataType=RESERVED | `Mc2SetCcSrcDataType(args, 255)` | 5 (HCCL_E_NOT_SUPPORT) | `HcomCheckDataType` |
| UnsupportedOpType=GATHER | `ccType=HCCL_CMD_GATHER`，algConfig留空 | 5 (HCCL_E_NOT_SUPPORT) | opType白名单（仅AG/AR/RS/A2A/A2AV） |
| UnregisteredAlg | `algConfig="NoSuchAlgName"` | 1042 (HCCL_E_ALG_NOT_SUPPORTED) | `CheckCcuAlgorithmsRegistered` |
| AicpuTsEngine | `commEngine=AICPU_TS(2)` | 0 (HCCL_SUCCESS) | checkOnly提前返回（非CCU_SCHED不探测） |
| LegacyAlgConfig | `algConfig="level_graph=0"`（含'='） | 1042 (HCCL_E_ALG_NOT_SUPPORTED) | `CheckCcuAlgorithmsRegistered`（要求纯算法名） |
| ResourceSufficient | 2卡+ALLGATHER+FP32+注册算法 | 0 (HCCL_SUCCESS) | 空载资源充足基线 |
| SingleRankGuard | 单卡环境运行（devCount==1） | 0 (HCCL_SUCCESS) | rankSize==1守卫 |
| ReuseInsInsufficient | `--stress-reuse`启用 | 观察（不判定） | 资源链UNAVAIL翻译 |

说明：

- **单卡环境**自动跳过多卡用例，仅执行单rank守卫与参数类用例。
- **1042/5的边界**：ccArgs版由`Mc2SetCc*`前置校验拦截大部分非法参数（引擎/数据类型），与
  mc2Tiling版（tiling字段可自由篡改）的拦截位置不同——这正是两版本功能对比的关注点之一。
- **1043 (HCCL_E_RES_NOT_SUFFICIENT)** 需要在设备上已有其它comm/实例占用CCU资源时才可能出现：
  checkOnly不注册kernel、不占用资源，同一进程内反复check是幂等的，无法主动构造该场景；
  因此`./demo --stress-reuse`只做一次观察、不对结果做断言。

## 与mc2Tiling版的差异关注点

| 对比项 | mc2Tiling版 | ccArgs版（本样例验证） |
| --- | --- | --- |
| 参数载体 | host构造的tiling结构体（字段可篡改） | `Mc2GetCcArgs`创建的C++对象 |
| 非法引擎 | check内部`ObtainCommEngine`拦截 | `Mc2SetCcCommEngine`前置拦截 |
| 非法数据类型 | check内部`BuildTagsAndValidate`拦截 | `Mc2SetCcSrcDataType`前置拦截 |
| tiling结构校验（version/cnt/offset） | check入口`HcclGetTilingList`（PARA=1） | 不存在（无tiling载体） |
| 算法选择/资源探测 | 同一条checkOnly预检链 | 同一条checkOnly预检链 |

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

- 样例执行

  在本样例目录下执行如下命令。

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  ./demo                 # 基础错误码用例
  ./demo --stress-reuse  # 追加1043观察用例（需空载950真机）
  ```

- 执行结果

  2卡场景下运行成功会输出类似以下信息（用例全部通过时进程退出码为0，否则为非0）：

  ```text
  Found 2 NPU device(s) available

  ================ CheckOpResSufficient errcode cases ================
  case                                   expect     actual     result
  NullComm (expect PTR=2)                2          2          PASS
  NullCcArgs (expect PTR=2)              2          2          PASS
  SetEngine=AIV (expect NOT_SUPPORT=5)   5          5          PASS
  SetSrcDataType=RESERVED (expect 5)     5          5          PASS
  UnregisteredAlg (expect 1042)          1042       1042       PASS
  AicpuTsEngine (expect SUCCESS=0)       0          0          PASS
  UnsupportedOpType=GATHER (expect 5)    5          5          PASS
  LegacyAlgConfig (expect 1042)          1042       1042       PASS
  ResourceSufficient (expect 0)          0          0          PASS
  =====================================================================
  total=9 pass=9 fail=0
  ```

## 注意事项

- 运行多卡用例需要至少2张NPU；单卡环境仅执行单rank守卫和参数类用例。
- 1043观察用例受设备全局资源状态影响，建议在空载环境运行，且用例前确保历史comm已
  `HcclCommDestroy`释放。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`由通信域管理，随`HcclCommDestroy`释放，不需要单独释放。
