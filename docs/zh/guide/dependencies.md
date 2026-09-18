# 三方依赖与兼容性

## 适用范围

asc-comm当前仓内构建主要用于环境检查、AICore Hcomm接口UT验证和Hcomm样例验证。本文档说明当前已纳入仓内构建、验证和样例流程的直接依赖。

## 基础环境

| 依赖 | 要求 | 说明 |
| --- | --- | --- |
| CANN Toolkit | 与当前分支或Tag配套 | 执行`build.sh`前必须先`source ${install_path}/cann/set_env.sh`，脚本会检查`ASCEND_HOME_PATH`。 |
| CANN Runtime/HCCL/Hcomm | CANN 9.1.0或以上 | `hcomm_write_read_nbi`样例需要通信域创建、内存注册和AIV P2P通道创建能力，并在链接阶段依赖Host侧`hcomm`库。 |
| CMake | >= 3.16 | UT CMake入口为`tests/ut/CMakeLists.txt`。 |
| C++ 编译器 | 支持C++17 | UT目标使用`CMAKE_CXX_STANDARD 17`，建议`gcc/g++ >= 7.3.0`且版本一致。 |
| Python | Python 3 | UT可用于生成tiling头文件；OAT钩子要求Python 3.7+。源码和examples环境建议Python >= 3.9.0。 |

## 本仓直接三方依赖

| 场景 | 依赖 | 版本 | 获取或配置方式 |
| --- | --- | --- | --- |
| UT | googletest | 1.14.0 | 优先使用系统GTest；没有系统GTest时，通过`CANN_3RD_LIB_PATH`指向CANN third_party目录。 |
| 样例 | CANN ASC CMake能力和hcomm库 | CANN 9.1.0或以上 | 执行`source ${install_path}/cann/set_env.sh`后，在`examples/aicore/hcomm/01_hcomm_write_read_nbi`目录下使用CMake构建。 |
| 代码格式化 | clang-format | v18.1.8 | `pre-commit-config.yaml`从`pre-commit-clang/mirrors-clang-format`拉取。 |
| 开源合规检查 | oat-py | >= 1.0.1 | `scripts/oat_check.sh`会尝试自动安装；失败时手动执行`pip install oat-py>=1.0.1`。 |

仓根目录的`Third_Party_Open_Source_Software_List.yaml`当前只登记本仓测试直接使用的`googletest`。如果后续新增直接链接或打包的三方库，需要同步更新该清单和Notice。

## 样例运行依赖

`examples/aicore/hcomm/01_hcomm_write_read_nbi`样例支持Ascend 950PR/Ascend 950DT，运行时需要至少2张NPU。单卡环境可完成编译验证，但无法完成两卡点对点通信运行验证。该样例固定使用`COMM_ENGINE_AIV`和`COMM_PROTOCOL_UBC_CTP`，不覆盖RoCE路径。

样例编译命令如下：

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/aicore/hcomm/01_hcomm_write_read_nbi
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

## GTest安装与配置

若系统没有安装GTest，可准备如下目录结构：

```text
<third_party>/gtest/include/gtest/gtest.h
<third_party>/gtest/lib64/libgtest.a
```

然后执行：

```bash
source /usr/local/Ascend/cann/set_env.sh
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<third_party>
cmake --build build/ut-hcomm
```

`build.sh -t`会调用UT构建。需要指定离线GTest路径时，可以直接使用上面的CMake命令，也可以通过构建脚本传入：

```bash
bash build.sh -t --cann_3rd_lib_path=<third_party>
```

## 集成依赖边界

后续新增模块、样例或端到端流程引入新的三方组件时，需要同步更新本节、本仓三方开源软件清单和Notice。
