# SIMT Notify/FAA/CAS 下发性能测试

本用例参考 `aclshmem/examples/shmem_perftest/udma_perftest` 的计时方式，统计设备侧连续执行“填写 WQE + `commit=true` 敲 DB”的平均耗时。

本目录只迁移性能验证框架，不包含AIN性能分支中的Hcomm实现优化；因此可以用于测量和比较当前asc-comm实现优化前后的下发性能。

## 计时口径

每个测试进程使用独立通信上下文，设备侧流程为：

1. 从Channel读取SQ的BB深度；SIMT Notify/FAA/CAS及AIV Notify/FAA/CAS按每个WQE占2BB、AIV WriteNbi按1BB计算，得到单个burst的最大WQE数；
2. 在SQ容量范围内完成预热，每个burst结束后执行一次`Drain`；
3. 正式测量按相同burst大小执行，每次接口调用均为`commit=true`；
4. 每个burst只累计WQE组装和敲DB区间的周期；
5. 在burst之间执行`Drain`，用于回收SQ/CQ并检查执行状态，但不计入累计周期。

该测试口径是“容量内burst下发”，不是无间断持续吞吐。结果只包含WQE组装和敲DB，不包含`Drain/PollCq`；输出同时记录实际burst大小。这里的“平均WQE下发耗时”由所有burst的下发周期除以WQE总数得到，“Burst下发带宽”按`数据长度 / 平均WQE下发耗时`计算，表示软件在一个SQ容量窗口内的下发能力，不表示链路完成带宽。

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
RESULT | 路径=SIMT | 接口=notify | 数据(B)=4096 | 次数=1000 | SQDepth(BB)=... | Burst(WQE)=... | 平均WQE下发(ns/次)=... | Burst下发带宽(GB/s)=...
```

## 绘制带宽曲线

`run.sh` 默认将日志保存到 `examples/simt_notify_atomic_perf/results/`。根据日志生成对比折线图：

```bash
python3 examples/simt_notify_atomic_perf/plot.py \
    examples/simt_notify_atomic_perf/results/notify_YYYYmmdd_HHMMSS.log
```

横轴为数据长度（B，对数刻度），纵轴为Burst下发带宽（GB/s）。同一日志中相同长度存在多次结果时，脚本会取平均值。

## 资源约束

样例通过`HcclCommMemReg`和`HcclChannelAcquire`创建通信资源。Channel本地缓冲区表下标0由CCL buffer预留，因此第一个显式注册的发送buffer使用下标1；远端接收buffer下标通过tag查询。SIMT和AIV对照使用相同的Channel和缓冲区映射。
