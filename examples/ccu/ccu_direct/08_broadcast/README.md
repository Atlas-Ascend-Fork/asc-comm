# CCU Broadcast 直接执行样例

## 概述

本样例使用 HCCL 通信域、CCU full-mesh channel 和 `ccu::write` 实现 Broadcast。
所有 rank 先交换本地 buffer 地址和 token，然后由 root rank 将完整数据直接写到其他 rank。
Kernel 通过 ASC `<<<>>>` 直调方式下发，不再使用注册和 `asccomm_ccu_kernel_launch` 接口。

该实现面向流程验证，没有使用生产 HCCL 中针对大数据优化的 scatter+allgather。

样例仅支持 FP32、最多 16 个 rank，单次数据不超过 256 MiB。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## 目录结构介绍

```text
08_broadcast/
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

测试使用 root rank 0，消息元素数等于 `rankSize`，并在所有 rank 打印 `PASS` 或 `FAIL`。

## 执行结果

样例执行成功时，各rank输出`PASS`。若任意rank输出`FAIL`，表示执行结果与预期不一致。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
