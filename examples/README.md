# asc-comm样例

本目录提供asc-comm API的使用样例。

## 样例列表

| 样例 | 说明 | 支持产品 |
| --- | --- | --- |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README.md) | 演示AIV Kernel通过URMA路径在环形拓扑中调用`Hcomm::WriteNbi`和`Hcomm::ReadNbi`，并校验通信结果。 | Ascend 950PR/Ascend 950DT |

## hcomm_write_read_nbi

`hcomm_write_read_nbi`展示AIV直驱URMA点对点通信流程，包括Host侧通信域创建、通信内存注册、AIV P2P通道创建，以及Kernel侧`Init`、`WriteNbi`、`ReadNbi`和`Drain`调用。该样例不覆盖RoCE路径。

样例采用自动Fork多进程方式启动，默认双卡，也可以通过`./demo [nranks]`指定rank数量。运行时每个rank与环形拓扑中的前后相邻rank建立通信关系：

1. Host侧为每个rank创建通信域并注册通信buffer。
2. Host侧建立TCP环形拓扑：`prev = (rank - 1 + nranks) % nranks`，`next = (rank + 1) % nranks`。
3. 通过`HcclChannelAcquire`创建到相邻rank的AIV P2P通道。
4. Kernel侧将本卡数据通过`WriteNbi`写入`next`，并通过`ReadNbi`从`prev`读回数据。
5. Host侧回读校验结果，所有rank均通过时输出`test pass!`。

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

也可以指定rank数量：

```bash
# 显式指定双卡
./demo 2

# 指定4卡环形拓扑
./demo 4
```

## 运行约束

- 样例支持Ascend 950PR/Ascend 950DT，CANN软件版本要求为9.1.0或以上。
- 样例运行需要至少2张NPU；单卡环境仅支持编译验证。
- 样例编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
