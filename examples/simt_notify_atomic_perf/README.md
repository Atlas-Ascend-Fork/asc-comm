# SIMT Notify/FAA/CAS 下发性能测试

本用例参考 `aclshmem/examples/shmem_perftest/udma_perftest` 的计时方式，统计设备侧连续执行“填写 WQE + `commit=true` 敲 DB”的平均耗时。通信初始化、Channel/Buffer映射、收发同步和结果校验沿用已验证的`simt_notify_atomic`功能样例。

本目录只迁移性能验证框架，不包含AIN性能分支中的Hcomm实现优化；因此可以用于测量和比较当前asc-comm实现优化前后的下发性能。

## 计时口径

每个测试进程使用独立通信上下文，设备侧流程为：

1. 预热阶段按`batch`次提交为一组，每组结束后执行一次`Drain`；
2. 正式阶段在计时窗口内连续调用接口，每次均为`commit=true`；
3. 每组计时结束后再执行`Drain`，因此`Drain/PollCq`不计入下发耗时；
4. 累加所有组的下发周期并除以正式提交次数，得到平均单次WQE下发时延；
5. Host端在完成同步后校验远端数据/信号，以及FAA/CAS取回的旧值。

注册通信缓冲区至少保留与已验证功能样例相同的48B布局；较大payload按`对齐后payload + 8B signal`扩展。

默认`batch=0`，运行时会转换为`batch=iterations`：全部正式请求处于同一个计时窗口，末尾只在窗口外执行一次`Drain`。也可显式设置较小的`batch`，观察不同批量提交规模；样例不会根据SQ/CQ深度自动修改batch。

“下发带宽”按`数据长度 / 平均WQE下发耗时`计算，表示WQE组装与敲DB的软件下发能力，不是包含远端完成等待的链路带宽。

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
# 先验证单次提交框架：SIMT Notify、AIV Notify和AIV Write均会校验结果
bash examples/simt_notify_atomic_perf/run.sh 2 /path/to/rank_table.json notify 1 0 1 3 3

# 2^3B 到 2^20B，每个长度正式下发1000次、预热100次；默认单个batch
bash examples/simt_notify_atomic_perf/run.sh 2 /path/to/rank_table.json notify 1000 100 0 3 20

# FAA/CAS固定8B，长度指数参数会被忽略
bash examples/simt_notify_atomic_perf/run.sh 2 /path/to/rank_table.json faa 1000 100
bash examples/simt_notify_atomic_perf/run.sh 2 /path/to/rank_table.json cas 1000 100

# 每16次提交后在计时区间外Drain一次
bash examples/simt_notify_atomic_perf/run.sh 2 /path/to/rank_table.json notify 1000 100 16 3 20
```

完整参数：

```text
run.sh <卡数> <rank_table.json> <notify|faa|cas> <次数> [预热次数] [batch] [最小长度指数] [最大长度指数] [日志文件]
```

输出示例：

```text
RESULT | 路径=SIMT | 接口=notify | 数据(B)=4096 | 次数=1000 | Batch(WQE)=1000 | SQDepth(BB)=... | CQDepth(CQE)=... | 平均WQE下发(ns/次)=... | 下发带宽(GB/s)=...
```

## 绘制带宽曲线

`run.sh` 默认将日志保存到 `examples/simt_notify_atomic_perf/results/`。根据日志生成对比折线图：

```bash
python3 examples/simt_notify_atomic_perf/plot.py \
    examples/simt_notify_atomic_perf/results/notify_YYYYmmdd_HHMMSS.log
```

横轴为数据长度（B，对数刻度），纵轴为下发带宽（GB/s）。Notify日志包含SIMT Notify、AIV-UB Notify和AIV-UB WriteNbi三条基线对比曲线；同一日志中相同长度存在多次结果时，脚本会取平均值。

## 资源约束

样例通过`HcclCommMemReg`和`HcclChannelAcquire`创建通信资源。Channel本地缓冲区表下标0由CCL buffer预留，因此第一个显式注册的发送buffer使用下标1；远端接收buffer下标通过tag查询。SIMT和AIV对照使用相同的Channel和缓冲区映射。
