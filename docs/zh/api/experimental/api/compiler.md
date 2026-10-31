# ccuc编译接口

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 编译CCU kernel

最小命令如下：

```bash
ccuc --ccu-arch=a5 -std=c++17 -c kernel.cc -o kernel.o
```

如果需要直接检查CCU容器，可生成`.ccuobj`：

```bash
ccuc -x ccu --ccu-arch=a5 -std=c++17 --emit-ccuobj \
  kernel.cc -o kernel.ccuobj
```

## Host与kernel分文件

推荐将控制面和数据面拆开：

```text
allgather_host.cc    # communicator、stream、context、buffer、launch、同步、cleanup
allgather_kernel.cc  # __global__ __ccu__ kernel 和 Host wrapper
```

kernel源文件由`ccuc`编译，Host文件使用普通C/C++ 编译器编译；最后链接
`libccu_runtime.so.1`、部署环境的HCOMM public library和平台runtime。

```bash
ccuc --ccu-arch=a5 -std=c++17 -c allgather_kernel.cc -o allgather_kernel.o
g++ -std=c++17 -c allgather_host.cc -o allgather_host.o
g++ allgather_host.o allgather_kernel.o \
  -lccu_runtime -lhcomm -lascendcl -lc_sec -pthread -o allgather
```

实际部署还需要设置SDK和CANN/HCOMM的include、library和运行时搜索路径；这些路径应由
部署脚本或环境变量提供，不要写入源码。

## 产物

| 产物 | 用途 |
| --- | --- |
| `.o` | Host与CCU kernel的可链接对象 |
| `.ccuobj` | 可审计、可加载的CCU kernel容器 |
| `.ccuobj.mlir` | 便于检查编译结果和资源绑定信息的文本产物 |

`.ccuobj`由compiler生成。每次运行使用任务私有副本，不会原地修改编译产物。

## 编译约束

- `--ccu-arch`必须与部署设备匹配。
- kernel必须使用`__global__ __ccu__` specialization。
- task参数顺序和类型必须与生成的schema一致。
- rank specialization由`__ccu_rank_sizes__`声明，并由launch调度信息中的`rankSize`选择。
- CCU kernel不使用STL、异常、RTTI、动态分配或普通Host指针。

## 调用示例

```bash
ccuc --ccu-arch=a5 -std=c++17 -c allgather_kernel.cc -o allgather_kernel.o
g++ -std=c++17 -c allgather_host.cc -o allgather_host.o
g++ allgather_host.o allgather_kernel.o \
  -lccu_runtime -lhcomm -lascendcl -lc_sec -pthread -o allgather
```
