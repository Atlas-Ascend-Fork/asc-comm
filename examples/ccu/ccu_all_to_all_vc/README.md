# CCU AllToAllVC 直接执行样例

## 样例介绍

本样例使用 HCCL 通信域、CCU full-mesh channel 和 `ccu::Write` 实现 AllToAllVC。
所有 rank 使用同一个 `rankSize * rankSize` count matrix；元素 `(src, dst)` 指定 src 发给 dst 的元素数。

CCU kernel 向每个 peer 发布已经加上接收偏移的 output 地址，远端数据使用 `ccu::Write`，本地数据使用一次 `ccu::LocalCopy`。

样例仅支持 FP32、非原地操作、最多 16 个 rank，单个 peer 的数据块不超过 256 MiB。

## 构建和运行

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
bash run.sh
```

Host 从 count matrix 的本 rank 行计算发送布局，从本 rank 列计算接收布局。测试使用
`count(src, dst) = src + dst + 1` 构造非等长数据，并打印 `PASS` 或 `FAIL`。
