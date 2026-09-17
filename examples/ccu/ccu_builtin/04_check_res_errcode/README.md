# CCU Builtin CheckOpResSufficient错误码验证样例

## 概述

本样例用于验证`CheckOpResSufficient(comm, ccType, ccArgs)`
（ccArgs版）各错误码的返回行为。

样例在每个rank上依次执行一组错误码用例，rank 0汇总打印用例名、期望值、实际值和判定结果。

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
| SetReduceType=INVALID | `Mc2SetCcReduceType(args, 0xFF)` | 5 (HCCL_E_NOT_SUPPORT) | `HcomCheckReductionOp`（白名单仅SUM/PROD/MAX/MIN） |
| SetAlgConfig=128chars | `Mc2SetCcAlgConfig(args, 128字符长串)` | 1 (HCCL_E_PARA) | Set接口algConfig长度校验（超长在设置阶段即拒绝） |
| UnsupportedOpType=GATHER | `ccType=HCCL_CMD_GATHER`，algConfig留空 | 5 (HCCL_E_NOT_SUPPORT) | opType白名单（仅AG/AR/RS/A2A/A2AV） |
| UnregisteredAlg | `algConfig="NoSuchAlgName"` | 1042 (HCCL_E_ALG_NOT_SUPPORTED) | `CheckCcuAlgorithmsRegistered` |
| AicpuTsEngine | `commEngine=AICPU_TS(2)` | 0 (HCCL_SUCCESS) | checkOnly提前返回（非CCU_SCHED不探测） |
| LegacyAlgConfig | `algConfig="level_graph=0"`（含'='） | 1042 (HCCL_E_ALG_NOT_SUPPORTED) | `CheckCcuAlgorithmsRegistered`（要求纯算法名） |
| ResourceSufficient | 2卡+ALLGATHER+FP32+注册算法 | 0 (HCCL_SUCCESS) | 空载资源充足基线 |
| SingleRankGuard | 单卡环境运行（devCount==1） | 0 (HCCL_SUCCESS) | rankSize==1守卫 |
| ReuseInsInsufficient | `--stress-reuse`启用 | 1043 (观察到时PASS，否则仅观察) | 资源链UNAVAIL翻译 |

1043（HCCL_E_RES_NOT_SUFFICIENT）用例设计：每个 comm 首次真实 `Mc2AcquireCcResCtx` 会创建一个
CcuIns 实例并绑定到该 comm（约占设备默认资源池的 1/5），实例随通信域持有、`HcclCommDestroy` 才释放；
check 为 checkOnly 探测，不占资源。因此用例循环创建独立 comm 并各 Acquire 一次逐个占住设备资源，
当某次 Acquire 返回 `HCCL_E_UNAVAIL(7)`（设备全局剩余不足）时，在同 comm 上调用 check——
check 走与 Acquire 相同的新建实例路径，必然复现 UNAVAIL 并被翻译为 1043。循环上限 8 个 comm
（理论 5 个即耗尽），结束后逆序销毁全部 comm 归还资源。若循环上限内资源仍未耗尽（环境资源异常充裕）
则该用例仅观察、不判失败。

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
  SetReduceType=INVALID (expect 5)       5          5          PASS
  SetAlgConfig=128chars (expect PARA=1)  1          1          PASS
  UnregisteredAlg (expect 1042)          1042       1042       PASS
  AicpuTsEngine (expect SUCCESS=0)       0          0          PASS
  UnsupportedOpType=GATHER (expect 5)    5          5          PASS
  LegacyAlgConfig (expect 1042)          1042       1042       PASS
  ResourceSufficient (expect 0)          0          0          PASS
  =====================================================================
  total=11 pass=11 fail=0
  ```

## 注意事项

- 运行多卡用例需要至少2张NPU；单卡环境仅执行单rank守卫和参数类用例。
- 1043观察用例受设备全局资源状态影响，建议在空载环境运行，且用例前确保历史comm已
  `HcclCommDestroy`释放。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`由通信域管理，随`HcclCommDestroy`释放，不需要单独释放。
