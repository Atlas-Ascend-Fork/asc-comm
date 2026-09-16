# 多 Jetty SIMT 验证

用例演示多个 Jetty 并行发布，以及由一个消费者统一处理共享 CQ、按 JFS 归属推进各 Jetty 的完成状态。支持 Write、Read 和 WriteWithNotify。

## 执行

```bash
# 已加载 CANN 环境；每次只编一个 batch 的 VF。
bash build.sh --batch-size 1
# 默认运行 Write/Read/Notify × J1/2/4/8，共12项。
bash run.sh --batch-size 1
# 可选其中一项。
bash run.sh --batch-size 1 --api write --jetty-counts 4
# 对 8、32、64、128、256 分别重复上述两条，六组共72项。
```

固定64B、每Jetty1024计时请求、warmup96按batch向上对齐、两设备。`--batch-size` 同时指定每批请求数和每 Jetty 的 lane 数，每 lane 构造一个请求。`--jetty-counts` 指定 Jetty 数；当前不支持独立指定 lane 数或每 lane 构造多个请求。

每组一个独立构建目录 `build/simt-matrix-bs<N>`，仅编译一个并行VF。构建超时5分钟，检查单VF身份与stack≤1152B，寄存器按实际几何记录。`--clean`仅清理选中的产物。

运行前核对 `Layout=matrix-v3`、batch及构建生成的二进制SHA。增量构建只有对象SHA相同时才复用资源记录。构建和执行参数以各脚本 `--help` 为准。

## 文件职责

| 文件 | 职责 |
|---|---|
| main.cpp | 分配缓冲、发起kernel、收集结果与释放资源 |
| host_setup.h | 两rank初始化、Jetty创建、主机阶段同步 |
| simt_kernel.cpp | 单一并行VF与warmup/计时/Drain流程 |
| simt_submit.h | 当前三屏障发布顺序与每lane SQ维护 |
| simt_completion.h | CompletionSet初始化/接续、窗口登记、完成快照 |
| validation.h | 完成计数、全部payload字节、Notify值校验 |
| multi_jetty_batch_perf_common.h | 共享常量及主机/设备数据布局 |

每 lane 维护自己构造的 SQ，多个 Jetty 的 publisher 并行发布。三个 block 屏障协调发布前、发布后和下一批开始，仍会使组间等待；共享 CQ 的单消费者规则只约束完成管理，并不要求 Jetty 轮流提交。J8/BS256仍为同stream两次各4Jetty的kernel，第二次接续同一CompletionSet，不清零CQ游标。输出明确报告KernelLaunches及MaxConcurrentJettys。

## 结果

每项同时要求双方进程成功、完成计数PASS和对应端数据校验通过；Notify还要求所有通知值通过。Read检查发起端，Write/Notify检查接收端，包含预热数据。

每组产生12条RESULT及一条MATRIX_SUMMARY。完整rank日志保留在汇总日志旁的`.ranks.*`目录。普通校验失败收集完本组；超时、信号、初始化错误停止后续项。没有执行的组合不能计为PASS。

性能单位为原始device ticks；多kernel累加设备计时区间，不包含host launch间隙。未对齐时钟口径前，不与归档SIMD的ns/GB/s直接计算加速比。

本地检查在tests/ut/aicore/hcomm：完成管理/72项几何回归、真实build/run脚本模拟、完整主机/设备源文件语法检查。SDK声明替代不等于目标CANN编译或NPU运行通过。
