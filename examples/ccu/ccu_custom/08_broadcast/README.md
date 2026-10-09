# CCU Broadcast 直接执行样例

## 样例介绍

本样例使用 HCCL 通信域、CCU full-mesh channel 和 `ccu::Write` 实现 Broadcast。
所有 rank 先交换本地 buffer 地址和 token，然后由 root rank 将完整数据直接写到其他 rank。

该实现面向流程验证，没有使用生产 HCCL 中针对大数据优化的 scatter+allgather。

样例仅支持 FP32、最多 16 个 rank，单次数据不超过 256 MiB。

## 构建和运行

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
bash run.sh
```

测试使用 root rank 0，消息元素数等于 `rankSize`，并在所有 rank 打印 `PASS` 或 `FAIL`。
