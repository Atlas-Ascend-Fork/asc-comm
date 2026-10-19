# CCU数据搬运样例

## 样例介绍

本样例演示A5/V1 CCU数据面三类基础搬运接口：本地拷贝、远端写和远端归约写。样例固定使用2个rank，每个rank输入4个FP32元素，通过编译参数选择教学场景。
Kernel 通过 ASC `<<<>>>` 直调方式下发，不再使用注册和 `asccomm_ccu_kernel_launch` 接口。

- 场景1调用`ccu::local_copy`的三个重载：`local_addr`到`local_addr`、`local_addr`到`ccu_buffer`、`ccu_buffer`到`local_addr`。直接拷贝用于本rank内存搬运；`ccu_buffer`可作为暂存资源，适合先把数据写入CCU缓冲，再从缓冲写出。
- 场景2调用`ccu::write`的两个重载：`local_addr`到远端`remote_addr`、`ccu_buffer`到远端`remote_addr`。远端写由本rank发起，直接写入对端内存。
- 场景3调用`ccu::write_reduce`，远端写入时按元素执行SUM归约。该接口与`ccu::write`的区别是：目标数据不是被覆盖，而是与源数据逐元素累加。

所有场景都使用event跟踪搬运完成状态，rank之间通过channel交换输出地址和token，并在kernel结束前同步远端写入完成。

## 预期结果

rank0输入为`[1, 2, 3, 4]`，rank1输入为`[5, 6, 7, 8]`。

| 场景 | 接口 | rank0输出 | rank1输出 |
| --- | --- | --- | --- |
| 1 | `local_copy`三个重载 | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |
| 2 | `write`两个重载 | `[5, 6, 7, 8]` | `[1, 2, 3, 4]` |
| 3 | `write_reduce` | `[6, 8, 10, 12]` | `[6, 8, 10, 12]` |

每个rank都会输出`PASS`。若任意rank输出`FAIL`，说明搬运结果与预期不一致。

## 构建和运行

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
SCENARIO_NUM=1 bash run.sh
```

将`SCENARIO_NUM`分别设置为`1`、`2`、`3`可以编译并运行三个教学场景。样例要求当前环境至少可见2个NPU。
