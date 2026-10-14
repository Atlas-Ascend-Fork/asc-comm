# CCU AllToAllV 直接执行样例

## 样例介绍

本样例使用 HCCL 通信域、CCU full-mesh channel 和 `ccu::write` 实现 AllToAllV。
每个 rank 通过 `sendCounts/sendDispls` 和 `recvCounts/recvDispls` 指定各 peer 的可变长度数据块。

CCU kernel 向每个 peer 发布已经加上接收偏移的 output 地址，远端数据使用 `ccu::write`，本地数据使用一次 `ccu::local_copy`。

本样例当前是 direct 调用最小验证原型：动态的 `sendBytes/sendOffsets/recvOffsets` 由 `ccu_kernel_arg` 指向的结构体携带，kernel 内通过 `ccu::variable = uint64_t` 初始化。该写法是否与 `ccu::load_arg` 等价，需要通过本样例的实际运行结果确认。

样例仅支持 FP32、非原地操作、最多 16 个 rank，单个 peer 的数据块不超过 256 MiB。

## 构建和运行

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
bash run.sh
```

测试使用 `count(src, dst) = src + dst + 1` 构造非等长数据，并在每个 rank 检查输出后打印 `PASS` 或 `FAIL`。
