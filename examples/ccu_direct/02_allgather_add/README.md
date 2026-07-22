# 自定义通信算子 - AllGather（直接编译版）

## 样例介绍

本样例展示如何基于 HCCL 通信编程接口开发 AllGather 通信算子，并将 host 侧接口、资源申请、CCU kernel 注册/编排代码直接编译进测试程序。包含以下功能点：

1. 基于 CCU_SCHED 通信引擎实现 AllGather 集合通信算子
2. 不生成 `libhccl_custom_allgather.so`
3. `main.asc` 直接包含自定义接口实现、资源申请、taskArgs 组织与 CCU kernel launch 代码

## 目录结构

```text
├── CMakeLists.txt                      # CMake 直接编译样例配置文件
├── op_kernel_ccu/
│   ├── ccu_kernel.cc                   # CCU Kernel 实现逻辑
│   └── ccu_kernel.h                    # CCU Kernel 头文件
├── inc/
│   ├── common.h                        # 公共类型头文件
│   └── log.h                           # 日志宏定义
└── testcase/
    └── main.asc                        # 样例实现源文件
```

## 一、环境准备

### 1. 环境要求

本样例支持以下昇腾产品：

- <term>Ascend 950PR</term> / <term>Ascend 950DT</term>

### 2. 安装 CANN Toolkit 开发套件包

参考 [昇腾文档中心-CANN软件安装指南](https://www.hiascend.com/document/redirect/CannCommunityInstWizard)，安装最新版本 CANN Toolkit 开发套件包。

### 3. 配置环境变量

按需选择合适的命令使环境变量生效。
    
```bash
# 默认路径安装，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/cann/set_env.sh
# 指定路径安装，${install_path}表示CANN-Toolkit包实际安装路径
# source ${install_path}/cann/set_env.sh
```

## 二、编译执行样例

### 1. 编译样例

在 `examples/ccu_direct` 代码目录下执行如下命令：

```bash
# 编译样例
mkdir build
cd build
cmake ..
make
```

### 2. 执行样例

在 `examples/ccu_direct` 代码目录下执行如下命令：
 	 
```bash
./build/demo
```

### 3. 样例结果示例

所有节点的输入数据初始化为该节点的 DeviceId。运行成功后，终端将输出类似以下的日志信息（以 2 卡运行为例）：

```text
Found 2 NPU device(s) available
rankId: 1, input: [ 2 2 2 2 2 ... ]
rankId: 0, input: [ 1 1 1 1 1 ... ]
rankId: 0, output: [ 9 9 9... 10 10 10... ]
rankId: 1, output: [ 9 9 9... 10 10 10... ]
```
