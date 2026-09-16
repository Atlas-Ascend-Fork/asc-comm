# Hcomm Batch Write样例

## 功能说明

本样例演示如何使用Hcomm批量写接口，在单机多卡环境中由Rank 0向其他Rank写入数据。程序会准备待发送数据，提交一批写操作并等待完成；接收Rank随后校验收到的数据，主进程最后汇总所有Rank的执行结果。

## 支持环境

- Ascend 950PR或Ascend 950DT；
- 至少两张NPU，仅支持单机多卡；
- Rank数量为2至16，且不能超过本机可用NPU数量。

运行时使用两个Rank进行最小验证，也可以传入更多Rank验证一次批量写入多个目标。

## 编译运行

先配置CANN环境：

```bash
source ${install_path}/cann/set_env.sh
```

使用脚本编译并运行，默认使用两张NPU：

```bash
bash examples/hcomm_batch_write/run.sh
bash examples/hcomm_batch_write/run.sh 4
```

脚本参数为Rank数量，取值范围为`2-16`。也可以手动执行：

```bash
cmake -S examples/hcomm_batch_write -B build/examples/hcomm_batch_write \
    -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build build/examples/hcomm_batch_write -j
./build/examples/hcomm_batch_write/hcomm_batch_write 4
```

其中最后一条命令中的`4`表示启动4个Rank。运行前请确认对应数量的NPU可用，并确保当前用户具备设备访问权限。

## 结果判断

成功时每个接收Rank会输出校验通过信息，主进程最终输出：

```text
hcomm 4-rank grouped batch write test passed
```

主进程输出`failed`或任一Rank报错时，表示本次批量写入或数据校验未通过。
