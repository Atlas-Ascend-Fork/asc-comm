# CCU AllToAllV 直接执行样例

## 概述

本样例使用 HCCL 通信域、CCU full-mesh channel 和 `ccu::write` 实现 AllToAllV。
每个 rank 通过 `sendCounts/sendDispls` 和 `recvCounts/recvDispls` 指定各 peer 的可变长度数据块。

CCU kernel 向每个 peer 发布已经加上接收偏移的 output 地址，远端数据使用 `ccu::write`，本地数据使用一次 `ccu::local_copy`。

本样例当前是 direct 调用最小验证原型：动态的 `sendBytes/sendOffsets/recvOffsets` 由 `ccu_kernel_arg` 指向的结构体携带，kernel 内通过 `ccu::variable = uint64_t` 初始化。该写法是否与 `ccu::load_arg` 等价，需要通过本样例的实际运行结果确认。

样例仅支持 FP32、非原地操作、最多 16 个 rank，单个 peer 的数据块不超过 256 MiB。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## 目录结构介绍

```text
06_all_to_allv/
├── CMakeLists.txt
├── README.md
├── README_en.md
├── run.sh
└── main.asc
```

## 样例描述

本样例基于HCCL通信域申请CCU资源，并通过ASC `<<<>>>` 直调CCU Kernel完成数据通信或搬运。

## 编译运行

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
bash run.sh
```

测试使用 `count(src, dst) = src + dst + 1` 构造非等长数据，并在每个 rank 检查输出后打印 `PASS` 或 `FAIL`。

## 执行结果

样例执行成功时，各rank输出`PASS`。若任意rank输出`FAIL`，表示执行结果与预期不一致。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
