# 多 Jetty SIMT 验证

根目录只运行当前并行发布 + CompletionV1。历史 SIMD、SIMT-Serial、scope 对照和分项探针独立放在 [diagnostics/](diagnostics/README.md)，不进入正常构建。

## 执行

```bash
# 已加载 CANN 环境；每次只编一个 batch 的 VF。
bash build.sh --batch-size 1
# 默认运行 Write/Read/Notify × J1/2/4/8，共12项。
bash run.sh --batch-size 1
# 可选其中一项；不需要设置 parallel-control 或 trace 环境变量。
bash run.sh --batch-size 1 --api write --jetty-counts 4
# 对 8、32、64、128、256 分别重复上述两条，六组共72项。
```

需要显式使用 `-O0` 调测时，构建和运行都加同一个选项：

```bash
bash build.sh --batch-size 64 -O0
bash run.sh --batch-size 64 -O0
```

产物放在 `build/simt-matrix-bs64-o0`，默认构建不受影响。结果标记 `Optimization=O0`，不能作为优化版本的性能基线。构建前移除旧可执行文件，编译失败后不会误跑旧版本。


固定64B、每Jetty1024计时请求、warmup96按batch向上对齐、两设备。batch决定每Jetty的lane数；用户只指定batch，不再另配scope/control/lane参数。

每组一个独立构建目录 `build/simt-matrix-bs<N>`；不编SIMD和串行VF。构建超时5分钟，失败直接退出；`--clean`仅清理选中的产物。运行前只要求对应可执行文件存在，不依赖资源报告、栈阈值、SHA或构建标记。

`--matrix-batch`和以前矩阵命令的固定参数仍兼容；历史`--scope/--fast`改用diagnostics入口。用例中的完成计数、数据和Notify校验保持。


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

Comm算法、三个发布边界和SQ扫描删减保持；没有原始CQ采样或分项计时探针。J8/BS256仍为同stream两次各4Jetty的kernel，第二次接续同一CompletionSet，不清零CQ游标。输出明确报告KernelLaunches及MaxConcurrentJettys。

## 结果

每项同时要求双方进程成功、完成计数PASS和对应端数据校验通过；Notify还要求所有通知值通过。Read检查发起端，Write/Notify检查接收端，包含预热数据。

每组产生12条RESULT及一条MATRIX_SUMMARY。完整rank日志保留在汇总日志旁的`.ranks.*`目录。普通校验失败收集完本组；超时、信号、初始化错误停止后续项。没有执行的组合不能计为PASS。

性能单位为原始device ticks；多kernel累加设备计时区间，不包含host launch间隙。未对齐时钟口径前，不与归档SIMD的ns/GB/s直接计算加速比。

本地检查在tests/ut/aicore/hcomm：完成管理/72项几何回归、真实build/run脚本模拟、完整主机/设备源文件语法检查。SDK声明替代不等于目标CANN编译或NPU运行通过。

## Group 入参

`Hcomm<>` 不保存 Group；创建 handle、每次 Batch Append 和 `BatchCommit` 显式传入同一个 `group`。Group 的成员、rank 和 size 在 handle 使用期间必须保持一致。无 Group 重载用于单 lane；`Drain` 和 CompletionSet 接口不变。
