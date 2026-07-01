# 三方依赖与兼容性

## 适用范围

asc-comm 当前 AICore Hcomm 接口为 header-only 形态，仓内构建主要用于环境检查和 UT 验证。完整算子工程还需要 devkit/CANN 算子工程能力和 hcomm control-plane 资源创建能力，相关依赖应以对应外部仓文档为准。

## 基础环境

| 依赖 | 要求 | 说明 |
| --- | --- | --- |
| CANN Toolkit | 与当前分支或 Tag 配套 | 执行 `build.sh` 前必须先 `source ${install_path}/cann/set_env.sh`，脚本会检查 `ASCEND_HOME_PATH`。 |
| CMake | >= 3.16 | UT CMake 入口为 `tests/ut/CMakeLists.txt`。 |
| C++ 编译器 | 支持 C++17 | UT 目标使用 `CMAKE_CXX_STANDARD 17`。参考 devkit，建议 `gcc/g++ >= 7.3.0` 且版本一致。 |
| Python | Python 3 | UT 可用于生成 tiling 头文件；OAT 钩子要求 Python 3.7+。参考 devkit，源码和 examples 环境建议 Python >= 3.9.0。 |

## 本仓直接三方依赖

| 场景 | 依赖 | 版本 | 获取或配置方式 |
| --- | --- | --- | --- |
| UT | googletest | 1.14.0 | 优先使用系统 GTest；没有系统 GTest 时，通过 `CANN_3RD_LIB_PATH` 指向 CANN third_party 目录。 |
| 代码格式化 | clang-format | v18.1.8 | `pre-commit-config.yaml` 从 `pre-commit-clang/mirrors-clang-format` 拉取。 |
| 开源合规检查 | oat-py | >= 1.0.1 | `scripts/oat_check.sh` 会尝试自动安装；失败时手动执行 `pip install oat-py>=1.0.1`。 |

仓根目录的 `Third_Party_Open_Source_Software_List.yaml` 当前只登记本仓测试直接使用的 `googletest`。如果后续新增直接链接或打包的三方库，需要同步更新该清单和 Notice。

## GTest 安装与配置

若系统没有安装 GTest，可准备如下目录结构：

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

`build.sh -t` 会调用 UT 构建，但当前脚本未暴露 `CANN_3RD_LIB_PATH` 参数；需要指定离线 GTest 路径时，建议直接使用上面的 CMake 命令。

## 外部集成依赖边界

devkit quick start 中列出的 UT 三方包包含 `googletest 1.14.0`、`boost 1.87.0`、`mockcpp 2.7` 及 patch；hcomm 构建文档还包含 `json`、`makeself`、`openssl`、`hcomm_utils`、`abseil-cpp`、`protobuf`、`rdma-core`、`cann-cmake` 等。这些是 devkit/hcomm 自身构建或完整通信算子集成所需依赖，不是 asc-comm 当前 header-only 构建的直接依赖。
