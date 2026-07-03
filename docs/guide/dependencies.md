# 三方依赖与兼容性

## 适用范围

asc-comm当前仓内构建主要用于环境检查和AICore Hcomm接口UT验证。本文档仅说明当前已纳入仓内构建与验证流程的直接依赖；后续新增模块、样例或端到端流程时，应同步补充对应依赖说明。

## 基础环境

| 依赖 | 要求 | 说明 |
| --- | --- | --- |
| CANN Toolkit | 与当前分支或Tag配套 | 执行`build.sh`前必须先`source ${install_path}/cann/set_env.sh`，脚本会检查`ASCEND_HOME_PATH`。 |
| CMake | >= 3.16 | UT CMake入口为`tests/ut/CMakeLists.txt`。 |
| C++ 编译器 | 支持C++17 | UT目标使用`CMAKE_CXX_STANDARD 17`，建议`gcc/g++ >= 7.3.0`且版本一致。 |
| Python | Python 3 | UT可用于生成tiling头文件；OAT钩子要求Python 3.7+。源码和examples环境建议Python >= 3.9.0。 |

## 本仓直接三方依赖

| 场景 | 依赖 | 版本 | 获取或配置方式 |
| --- | --- | --- | --- |
| UT | googletest | 1.14.0 | 优先使用系统GTest；没有系统GTest时，通过`CANN_3RD_LIB_PATH`指向CANN third_party目录。 |
| 代码格式化 | clang-format | v18.1.8 | `pre-commit-config.yaml`从`pre-commit-clang/mirrors-clang-format`拉取。 |
| 开源合规检查 | oat-py | >= 1.0.1 | `scripts/oat_check.sh`会尝试自动安装；失败时手动执行`pip install oat-py>=1.0.1`。 |

仓根目录的`Third_Party_Open_Source_Software_List.yaml`当前只登记本仓测试直接使用的`googletest`。如果后续新增直接链接或打包的三方库，需要同步更新该清单和Notice。

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

`build.sh -t`会调用UT构建，但当前脚本未暴露`CANN_3RD_LIB_PATH`参数；需要指定离线GTest路径时，建议直接使用上面的CMake命令。

## 集成依赖边界

后续新增模块、样例或端到端流程引入新的三方组件时，需要同步更新本节、本仓三方开源软件清单和Notice。
