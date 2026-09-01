# Hcomm Batch Write 样例

## 功能说明

本样例演示如何使用 Hcomm 批量写接口，在单机多卡环境中由 Rank 0 向其他 Rank 写入数据。程序会准备待发送数据，提交一批写操作并等待完成；接收 Rank 随后校验收到的数据，主进程最后汇总所有 Rank 的执行结果。

## 支持环境

- Ascend 950PR 或 Ascend 950DT；
- 至少两张 NPU，仅支持单机多卡；
- Rank 数量为 2 至 16，且不能超过本机可用 NPU 数量。

运行时使用两个 Rank 进行最小验证，也可以传入更多 Rank 验证一次批量写入多个目标。

## 编译运行

先配置 CANN 环境：

```bash
source ${install_path}/cann/set_env.sh
```

使用脚本编译并运行，默认使用两张 NPU：

```bash
bash examples/hcomm_batch_write/run.sh
bash examples/hcomm_batch_write/run.sh 4
```

脚本参数为 Rank 数量，取值范围为 `2-16`。也可以手动执行：

```bash
cmake -S examples/hcomm_batch_write -B build/examples/hcomm_batch_write \
    -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build build/examples/hcomm_batch_write -j
./build/examples/hcomm_batch_write/hcomm_batch_write 4
```

其中最后一条命令中的 `4` 表示启动 4 个 Rank。运行前请确认对应数量的 NPU 可用，并确保当前用户具备设备访问权限。

## 结果判断

成功时每个接收 Rank 会输出校验通过信息，主进程最终输出：

```text
hcomm 4-rank grouped batch write test passed
```

主进程输出 `failed` 或任一 Rank 报错时，表示本次批量写入或数据校验未通过。
