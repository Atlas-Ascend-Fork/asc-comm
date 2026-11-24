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

## 源文件和编译结果

Host与kernel可以写在一个C++源文件中，也可以分别写在Host源文件和kernel源文件中。
`ccuc`在融合编译时提供CCU接口声明，源码不必显式包含`ccu_runtime.h`和
`ccu_intrinsics.h`。

| 输入和命令 | 结果 |
| --- | --- |
| `ccuc source.cc -o app` | 编译单文件中的Host和kernel，生成可执行文件 |
| `ccuc -c kernel.cc -o kernel.o` | 生成包含CCU kernel的对象文件 |
| `ccuc host.cc kernel.o -o app` | 将Host与kernel对象组合，生成可执行文件 |

## 编译CCU kernel

单独编译kernel源文件：

```bash
ccuc -std=c++17 -c kernel.cc -o kernel.o
```

`ccuc`支持混合Host/CCU源文件，也可以显式选择编译侧：

```bash
# 仅生成Host对象
ccuc --host-only -c mixed.cc -o host.o

# 仅生成CCU设备对象
ccuc --ccu-only -c mixed.cc -o kernel.o
```

默认模式会根据源文件内容编译Host和CCU代码；`--host-only`和`--ccu-only`
分别只处理Host侧或CCU侧。通常不需要为分文件构建的Host链接命令添加`--host-only`。

## Host与kernel分文件

推荐将控制面和数据面拆开：

```text
allgather_host.cc
allgather_kernel.cc
```

先编译kernel，再由`ccuc`编译Host并链接。下面的命令在包含样例源文件的目录执行：

```bash
ccuc -std=c++17 -c allgather_kernel.cc -o allgather_kernel.o
ccuc -std=c++17 allgather_host.cc allgather_kernel.o -o allgather
```

也可以让`ccuc`在一条命令中处理两个源文件；编译器先编译kernel，再处理Host和链接：

```bash
ccuc -std=c++17 allgather_host.cc allgather_kernel.cc -o allgather
```

上述命令由`ccuc`查找已安装CANN包中的头文件和链接库；输入若还依赖其他库，可用
`-I`、`-L`和`-l`补充路径或库。`-c`只生成对象文件，不生成可执行文件。

常用选项如下；具体支持情况以当前安装包的`ccuc --help`为准。

| 选项 | 用途 |
| --- | --- |
| `-c` | 只编译，不执行链接；生成对象文件 |
| `-o <文件名>` | 指定输出文件名，可用于对象文件或可执行文件 |
| `-std=c++17` | 指定C++17语言标准 |
| `-x ccu` | 将输入文件按CCU源文件解析 |
| `--ccu-only`、`--host-only` | 分别只编译CCU侧、Host侧 |

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
