# AICore Hcomm点对点通信Kernel样例

## 样例说明

本样例展示`AscendC::Hcomm`在AICore Kernel侧的基本使用流程。代码参考devkit Hcomm API用例和本仓`tests/ut/aicore/hcomm/`中的接口验证逻辑。

该样例是Kernel侧代码片段，不是完整可直接运行的自定义算子工程。真实算子中，`ChannelHandle`需要由外部HCOMM/HCCL control-plane创建并传入，输入输出GM地址和临时UB工作区也需要由算子工程和运行时流程正确组织。完整集成方式请参考[完整算子集成说明](./INTEGRATION.md)。

## 文件说明

| 文件 | 说明 |
| --- | --- |
| `hcomm_p2p_kernel.cpp` | UBC_CTP/URMA路径下的`WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`调用示例。 |
| `INTEGRATION.md` | 将本样例接入hcomm/devkit/CANN完整算子工程的步骤和依赖边界。 |

## 基本流程

1. 包含`hcomm/hcomm.h`。
2. 创建`AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP>`对象。
3. 使用UB临时缓冲区调用`Init`。
4. 使用`commit = false`批量提交`WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`。
5. 调用`Commit`提交通道任务。
6. 调用`Drain`等待任务完成。

## 协议约束

- `COMM_PROTOCOL_UBC_CTP`支持`WriteWithNotifyNbi`。
- `COMM_PROTOCOL_ROCE`支持`ReadNbi`、`WriteNbi`、`Commit`和`Drain`，但不支持`WriteWithNotifyNbi`。
- `ChannelHandle`必须指向与模板协议匹配且已初始化的通道实体。

## 验证参考

可执行验证参考UT：

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh -t
```

相关用例位于`tests/ut/aicore/hcomm/`。
