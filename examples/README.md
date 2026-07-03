# asc-comm样例

本目录提供asc-comm API的使用样例。

## 样例列表

| 样例 | 说明 | 支持产品 |
| --- | --- | --- |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README.md) | 演示两卡场景下使用`Hcomm::WriteNbi`和`Hcomm::ReadNbi`完成点对点通信，并校验通信结果。 | Ascend 950PR/Ascend 950DT |

## hcomm_write_read_nbi

`hcomm_write_read_nbi`展示完整的Hcomm点对点通信流程，包括Host侧通信域创建、通信内存注册、P2P通道创建、Kernel侧`Init`、`WriteNbi`、`ReadNbi`和`Drain`调用。

样例采用两卡对称执行方式：

1. Host侧为每个rank创建通信域并注册通信buffer。
2. 通过`HcclChannelAcquire`创建到对端rank的P2P通道。
3. Kernel侧将本卡数据通过`WriteNbi`写入对端buffer。
4. Kernel侧通过`ReadNbi`从对端buffer读回数据。
5. Host侧回读校验结果，两个rank均通过时输出`test pass!`。

## 编译运行

进入样例目录后执行：

```bash
source /usr/local/Ascend/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
./demo
```

也可以手动启动两个rank：

```bash
# 终端1：rank 0
./demo 0 2 tcp://127.0.0.1:29621

# 终端2：rank 1
./demo 1 2 tcp://127.0.0.1:29621
```

## 运行约束

- 样例支持Ascend 950PR/Ascend 950DT，CANN软件版本要求为9.1.0或以上。
- 样例运行需要至少2张NPU；单卡环境仅支持编译验证。
- 样例编译依赖CANN ASC CMake能力，并在链接阶段依赖`hcomm`库。
