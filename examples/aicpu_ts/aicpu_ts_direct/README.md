# AICPU_TS自定义通信算子样例

简体中文 | [English](./README_en.md)

本目录提供AICPU_TS自定义集合通信算子样例。Host侧通过HCCL通信编程接口申请和复用通信资源，AICPU Kernel使用asc-comm POD数据面原语完成集合通信。

## 样例列表

| 目录名称 | 内容说明 |
| --- | --- |
| [allgather](./allgather/README.md) | 自定义AllGather算子，将各rank输入按rank顺序聚合到每个rank输出。 |
| [allreduce](./allreduce/README.md) | 自定义AllReduce算子，对各rank输入执行SUM归约并输出完整结果。 |
| [alltoall](./alltoall/README.md) | 自定义AlltoAll算子，在rank间交换等长数据块。 |
| [alltoallv](./alltoallv/README.md) | 自定义AlltoAllV算子，在rank间交换可变长度数据块。 |
| [reduce_scatter](./reduce_scatter/README.md) | 自定义ReduceScatter算子，先执行SUM归约再按rank切分输出。 |

## 运行约束

- 样例支持Ascend 950PR&950DT系列产品，组网为单机N卡（N >= 2）。
- 样例基于控制面预解析POD实体架构，需配套版本的asc-comm与hcomm源码环境；CANN安装包内旧版数据面不兼容时，CMake配置阶段会拦截并提示。
- 编译前可通过`ASC_COMM_SOURCE_DIR`、`HCOMM_SOURCE_DIR`、`ASCCOMM_DEVICE_LIBRARY`和`HCOMM_DEVICE_LIBRARY`显式指定源码与设备侧库路径。
- AICPU_TS自定义算子包需关闭驱动验签并加入`ascend_package_load.ini`白名单后才能运行；具体步骤见各算子README。
- 若环境中未安装hccl包，`${ASCEND_HOME_PATH}/share/info/hccl/version.info`可能不存在。hcomm初始化通信域时读取该文件，缺失会导致`HcclCommInitRootInfo`失败。可先安装hccl包；仅验证本样例时，也可创建内容为`Version=9.2.0`的占位文件。

各样例的目录结构、编译安装命令和预期输出参见对应目录下的README。

## 构建入口

本目录保留了源工程的自定义算子编译配置和`build.sh`入口。编译某个算子时，在本目录执行对应算子README中的命令，例如：

```bash
bash build.sh --vendor=cust --ops=allgather_aicpu --custom_ops_path=./allgather
```

