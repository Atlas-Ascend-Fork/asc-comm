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
ccuc -std=c++17 -c kernel.cc -o kernel.o
```

## Host与kernel分文件

推荐将控制面和数据面拆开：

```text
allgather_host.cc
allgather_kernel.cc
```

kernel源文件由`ccuc`编译，Host文件使用g++编译；最后链接
`libccu_runtime.so`、环境中CANN包的HCOMM库和运行时库。

```bash
ccuc -std=c++17 -c allgather_kernel.cc -o allgather_kernel.o
g++ -std=c++17 -c allgather_host.cc -o allgather_host.o
g++ allgather_host.o allgather_kernel.o \
  -lccu_runtime -lhcomm -lascendcl -lc_sec -pthread -o allgather
```

编译CCU kernel时，通过`-I`指定头文件路径，例如：

```bash
ccuc -std=c++17 -I"${CCUC_HOME}/include" -c kernel.cc -o kernel.o
```

## 声明rankSize特例化

在kernel定义前使用`__ccu_rank_sizes__`声明当前kernel支持的rankSize。编译器为每个
rankSize生成一份特例化kernel，运行时根据`HcomCcuSchd.rankSize`选择对应的kernel。

```cpp
__ccu_rank_sizes__(2, 4, 8, 16)
__global__ __ccu__ void allgather_kernel(...) {
  // kernel实现
}
```

`__ccu_rank_sizes__(2, 4, 8, 16)`表示当前kernel支持rankSize为2、4、8、16的通信域；
Host启动时传入的`schd.rankSize`必须等于其中一个值。`rankSize`当前仅支持2、4、8、16。

## 编译约束

- kernel必须使用`__global__ __ccu__`定义。
- 任务参数顺序和类型必须与kernel形参一致。
- rankSize特例化由`__ccu_rank_sizes__`声明，并由launch调度信息中的`rankSize`选择。
- CCU kernel不支持STL、异常、RTTI、动态内存分配和Host指针。
