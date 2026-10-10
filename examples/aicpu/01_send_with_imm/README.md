# AICPU TS `asccomm_send_with_imm_on_thread` 带立即数发送真机样例

## 概述

本样例展示如何在**AICPU TS 直驱**路径上验证 asc-comm AICPU 数据面新增接口
`asccomm_send_with_imm_on_thread`。样例采用双卡（或多卡）环形拓扑：每个 rank 与下一个 rank
建立 `COMM_PROTOCOL_UB_CTP` 通道，Host 侧完成建域、注册内存、申请通道与线程资源后，把用例表
下发到设备侧，由 AICPU Kernel 逐个调用被测接口，最后 Host 侧回读结果并逐字段断言。

与只校验返回码的用例不同，本样例会从真实 UB SQ 内存回读接口写入的 WQE，直接断言硬件描述符内容，
因此可以在真机上确认：

- 接口走的是 `UDMA_OPC_SEND_WITH_IMM` 操作码，而不是普通 `WRITE`；
- 32 位立即数被正确填入 `UdmaSqeCommon` 中与 `rmtAddr` 共用的 `immData` union 字段；
- 本端 SGE 的地址、长度、token 与切片数符合 `process_slices` 的分片规则；
- `pi` 按切片数推进，且 RTSQ doorbell 被敲响；
- 参数与协议拒绝路径不产生任何 WQE。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
01_send_with_imm
├── CMakeLists.txt              // 编译工程文件
├── send_with_imm.asc           // Host侧资源准备、Kernel下发与结果断言
├── send_with_imm_kernel.aicpu  // AICPU Kernel侧接口调用与WQE回读
├── send_with_imm_def.h         // Host与Kernel共享定义（用例表、WQE解码）
├── README.md                   // 中文样例说明
└── README_en.md                // 英文样例说明
```

## 样例描述

### 功能说明

被测接口原型（见 `include/aicpu/hcomm/asccomm_primitives.h`）：

```c
int32_t asccomm_send_with_imm_on_thread(
    ThreadHandle thread, ChannelHandle channel, const void* src, uint64_t len, uint32_t immData);
```

| 项目 | 说明 |
|------|------|
| 语义 | 带 32 位立即数的双边发送，对端通过特殊 CQE 接收立即数 |
| 支持协议 | 仅 UB 类协议（`UB_CTP`/`UBC_TP`/`UB_MEM` 等），其余协议返回 `ASCCOMM_E_NOT_SUPPORT` |
| 底层操作码 | `UDMA_OPC_SEND_WITH_IMM` |
| 远端地址 | 不需要，SQE 中 `rmtAddr` 位置改填 `immData` |

### 用例矩阵

Kernel 侧按顺序执行以下 8 个用例，Host 侧逐个断言：

| # | 用例名 | 类型 | len | immData | 期望返回码 | 看护点 |
|---|--------|------|-----|---------|-----------|--------|
| 0 | `positive/typical` | 正向 | 256 | `0xA5A5A5A5` | `SUCCESS` | 典型值单切片，WQE 全字段 |
| 1 | `positive/min-len-imm-low` | 正向 | 1 | `0x00000000` | `SUCCESS` | 最小长度 + immData 下边界 |
| 2 | `positive/imm-high` | 正向 | 256 | `0xFFFFFFFF` | `SUCCESS` | immData 上边界 |
| 3 | `positive/multi-slice` | 正向 | 1 MB | `0x5A5A5A5A` | `SUCCESS` | 超过 `maxWriteSize` 时的多切片分片与 `pi` 累加 |
| 4 | `zero-length` | 边界 | 0 | `0x12345678` | `SUCCESS` | 不产生 WQE，`pi` 不变 |
| 5 | `null-src` | 负向 | 256 | 0 | `E_PTR` | `src` 空指针拒绝 |
| 6 | `bad-thread-magic` | 负向 | 256 | 0 | `E_PTR` | `ThreadEntity` ABI 头魔术字校验 |
| 7 | `non-ub-protocol` | 负向 | 256 | 0 | `E_NOT_SUPPORT` | 非 UB 协议拒绝 |

负向用例在设备侧复制真实实体后只破坏单个字段（魔术字 / 协议），不污染真实资源。

### 断言项

正向用例（`len > 0`）在真机上断言以下全部字段：

| 断言 | 期望值 |
|------|--------|
| `apiRet` | `ASCCOMM_SUCCESS` |
| `opcode` | `UDMA_OPC_SEND_WITH_IMM`（1） |
| `immDataLow` | 用例给定的 `immData` |
| `immDataHigh` | 0 |
| `inlineEn` | 0 |
| `sgeNum` | 1 |
| `sgeAddr` | 本端发送源地址 |
| `sgeLength` | `min(len, maxWriteSize)`，即首片长度 |
| `cqe` | 单切片为 1，多切片首片为 0 |
| `piAfter` | `(piBefore + sliceNum) & 0xFFFF` |

负向用例断言 `apiRet` 等于期望错误码，且 `piAfter == piBefore`（未产生 WQE）。

### 实现流程

1. 可执行文件内部 fork 出全部 rank 进程（`examples/aicore/utils/process_manager.h`），rank 间通过
   命令行指定的 TCP 控制通道（`examples/aicore/utils/rank_sync.h`）交换 root info 并创建通信域。
2. 每个 rank 申请 4 MB 设备通信缓冲区并 `HcclCommMemReg` 注册；前半段为发送源，后半段预留给对端
   数据落地观测。
3. 通过 `HcclRankGraphGetLayers`/`HcclRankGraphGetLinks` 找到通往 `(rank + 1) % nranks` 的
   `COMM_PROTOCOL_UB_CTP` 链路，调用 `HcclChannelAcquire` 以 `COMM_ENGINE_AICPU_TS` 引擎申请通道。
4. 调用 `HcclThreadAcquireWithStream` 以 `COMM_ENGINE_AICPU_TS` 引擎申请绑定 AICPU 任务流的线程资源。
5. Host 侧回读句柄指向的 `CommAbiHeader`，校验其为 POD v2 的 `ThreadEntity`/`ChannelEntity`；
   控制面版本不匹配时在此提前失败并给出明确提示。
6. Host 侧填充用例表并拷贝到设备侧 `KernelContext`，通过 `<<<1, 0, stream>>>` 下发 AICPU Kernel。
7. Kernel 逐个用例调用 `asccomm_send_with_imm_on_thread`，记录返回码，并从
   `ChannelEntity.sqContextAddr[0].contextInfo.ubJfs.sqVa` 回读 `pi` 指向的 64 字节 WQE。
8. Host 侧带超时同步任务流，回读 `KernelContext` 并逐字段断言，最后打印每个用例的 PASS/FAIL。
9. 控制通道 barrier 后，各 rank 观测本卡接收区并输出 `PEER_PAYLOAD` 诊断行。

## 编译运行

在本样例根目录下执行如下步骤。本样例仅支持 NPU 运行模式，**不能在无 NPU 的纯 CPU 环境运行**。

- 配置环境变量

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 前置依赖

  | 依赖 | 说明 |
  |------|------|
  | ASC / AICPU CMake 包 | 提供 `find_package(ASC)`、`find_package(AICPU)` 与 `<<<>>>` AICPU 直调能力 |
  | `libhcomm.so` / `libhccl.so` | 提供通信域、内存注册、通道与线程资源等控制面接口 |
  | `libasccomm_aicpu.so` | asc-comm AICPU 数据面，提供被测接口 `asccomm_send_with_imm_on_thread` |
  | POD v2 控制面 | 控制面必须产出 `ThreadEntity`/`ChannelEntity` POD 句柄，契约见 `include/aicpu/hcomm/hcomm_res_entity_defs.h` |

- 样例执行

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && timeout --signal=TERM --kill-after=10s 300s \
      ./build/asccomm_send_with_imm tcp://127.0.0.1:29631 <nranks>
  ```

- 启动参数说明

  | 参数 | 默认值 | 说明 |
  |------|--------|------|
  | `<ip:port>` | 无默认 | rank 0 监听的控制通道地址（`tcp://ip:port`格式）；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 启动的 rank 数量，必须 >= 2，且不超过当前可用 NPU 数量 |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  |------|--------|------|
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510`（默认） | NPU架构，对应Ascend 950PR/Ascend 950DT |

- 执行结果

  两卡执行成功后，终端将输出类似以下信息：

  ```text
  [rank 0] case[positive/typical] apiRet=0 expectRet=0 pi 0->1 sliceNum=1 | PASS
  [rank 0] case[positive/min-len-imm-low] apiRet=0 expectRet=0 pi 1->2 sliceNum=1 | PASS
  [rank 0] case[positive/imm-high] apiRet=0 expectRet=0 pi 2->3 sliceNum=1 | PASS
  [rank 0] case[positive/multi-slice] apiRet=0 expectRet=0 pi 3->4 sliceNum=1 | PASS
  [rank 0] case[zero-length] apiRet=0 expectRet=0 pi 4->4 sliceNum=0 | PASS
  [rank 0] case[null-src] apiRet=2 expectRet=2 pi 4->4 sliceNum=0 | PASS
  [rank 0] case[bad-thread-magic] apiRet=2 expectRet=2 pi 4->4 sliceNum=0 | PASS
  [rank 0] case[non-ub-protocol] apiRet=5 expectRet=5 pi 4->4 sliceNum=0 | PASS
  [rank 0] PEER_PAYLOAD | from rank 1 | Status=NOT_LANDED
  [rank 0] asccomm_send_with_imm_on_thread | peer rank 1 | PASS
  RESULT | Example=asccomm_send_with_imm | Status=PASS
  ```

  门禁判据为：进程退出码 0，且 `RESULT` 行 `Status=PASS`。

## 注意事项

- 运行样例需要至少 2 张 NPU；单卡环境仅支持编译验证。
- 被测接口只能在 AICPU 上执行：RTSQ 敲门铃依赖 `halSqCqQuery`/`halSqCqConfig` 与设备运行环境提供的
  `GetSqeId`，纯 CPU 环境无法运行。
- `PEER_PAYLOAD` 是**非门禁诊断项**。`SEND_WITH_IMM` 是双边语义，数据与立即数由对端接收 CQE 承载，
  而 asc-comm 数据面当前没有提供 recv/poll CQE 接口，因此接收侧无法校验立即数；输出
  `NOT_LANDED` 属预期，不影响 `RESULT` 判定。待数据面补齐接收接口后，可把该项升级为门禁断言。
- `positive/multi-slice` 的实际切片数取决于控制面下发的 `maxWriteSize`；样例按设备侧回读到的
  `maxWriteSize` 动态计算期望值，因此不同环境上 `sliceNum` 可能为 1 或更大，均属正常。
- Kernel 同步使用 `aclrtSynchronizeStreamWithTimeout`（默认 60 秒）。若底层链路异常导致挂死，
  样例会以 FAIL 退出而不是永久阻塞；外层仍建议保留 `timeout --kill-after` 强杀兜底，并在强杀后
  用 `npu-smi info` 探测设备健康。
- 样例默认使用环境中可见的全部 NPU 拉起 `nranks` 个 rank，rank `r` 向 rank `(r + 1) % nranks` 发送。
- 8 个用例中有 4 个会真实上线发送。若对端接收资源紧张，可减少 `CASE_TABLE` 中的正向用例数量后重新编译。
