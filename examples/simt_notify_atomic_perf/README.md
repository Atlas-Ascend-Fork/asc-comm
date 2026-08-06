# SIMT Notify/FAA/CAS 下发性能测试

本用例参考 `aclshmem/examples/shmem_perftest/udma_perftest` 的计时方式，统计设备侧连续执行“填写 WQE + `commit=true` 敲 DB”的平均耗时。

本目录只迁移性能验证框架，不包含AIN性能分支中的Hcomm实现优化；因此可以用于测量和比较当前asc-comm实现优化前后的下发性能。

## 计时口径

每个测试进程使用独立通信上下文，设备侧流程为：

1. 连续预热若干次并执行一次 `Drain`；
2. 读取开始周期；
3. 连续执行 `iterations` 次接口调用，每次均为 `commit=true`；
4. 读取结束周期；
5. 在计时区间外执行 `Drain`，用于回收 SQ/CQ 并检查执行状态。

因此结果只包含 WQE 组装和敲 DB，不包含 `Drain/PollCq`。这里的“下发带宽”按 `数据长度 / 平均下发耗时` 计算，表示软件下发能力，不表示链路完成带宽。

- Notify：扫描数据长度，对比 SIMT Notify、AIV-UB Notify 和 AIV-UB WriteNbi。
- FAA/CAS：操作数固定为 8B，主要比较平均下发耗时。

Ascend 950 的系统计数器频率为 1GHz，本用例按 1 cycle = 1ns 输出。

## 编译

先在asc-comm仓库根目录生成并安装开发验证run包：

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh --pkg
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --full
```

然后编译性能样例：

```bash
cd examples/simt_notify_atomic_perf
bash build.sh
```

## 执行

```bash
# 2^3B 到 2^20B，每个长度正式下发1000次、预热100次
bash examples/simt_notify_atomic_perf/run.sh 2 notify 1000 100 3 20

# FAA/CAS固定8B，长度指数参数会被忽略
bash examples/simt_notify_atomic_perf/run.sh 2 faa 1000 100
bash examples/simt_notify_atomic_perf/run.sh 2 cas 1000 100
```

完整参数：

```text
run.sh <卡数> <notify|faa|cas> <次数> [预热次数] [最小长度指数] [最大长度指数] [日志文件]
```

输出示例：

```text
RESULT | 路径=SIMT | 接口=notify | 数据(B)=4096 | 次数=1000 | 平均下发(ns/次)=... | 下发带宽(GB/s)=...
```

## 绘制带宽曲线

`run.sh` 默认将日志保存到 `examples/simt_notify_atomic_perf/results/`。根据日志生成对比折线图：

```bash
python3 examples/simt_notify_atomic_perf/plot.py \
    examples/simt_notify_atomic_perf/results/notify_YYYYmmdd_HHMMSS.log
```

横轴为数据长度（B，对数刻度），纵轴为下发带宽（GB/s）。同一日志中相同长度存在多次结果时，脚本会取平均值。

## 资源约束

样例通过`HcclCommMemReg`和`HcclChannelAcquire`创建通信资源。Channel本地缓冲区表下标0由CCL buffer预留，因此第一个显式注册的发送buffer使用下标1；远端接收buffer下标通过tag查询。SIMT和AIV对照使用相同的Channel和缓冲区映射。
