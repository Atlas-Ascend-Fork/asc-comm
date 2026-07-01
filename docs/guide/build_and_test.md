# 构建与测试

## 环境准备

执行 `build.sh` 前，需要先 source CANN 环境脚本设置环境变量（`build.sh` 会检查 `ASCEND_HOME_PATH`，无论是否构建 UT 均需配置）。

```bash
# 默认路径安装，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/cann/set_env.sh
# 指定路径安装
# source ${install_path}/cann/set_env.sh
```

## 构建说明

asc-comm 当前 AICore Hcomm 接口为 header-only 形态。直接执行构建脚本时，脚本会完成环境检查，但不会构建 UT。

```bash
bash build.sh
```

## 构建 UT

使用 `-t` 或 `--test` 构建 Hcomm UT。

```bash
bash build.sh -t
```

构建目录默认为：

```text
build/ut-hcomm
```

## CMake 入口

UT 的 CMake 入口为：

```text
tests/ut/CMakeLists.txt
```

常用 CMake 变量：

| 变量 | 说明 |
| --- | --- |
| `ASCEND_CANN_PACKAGE_PATH` | CANN 包路径。未显式指定时，优先从环境变量推导。 |
| `PRODUCT_TYPE_LIST` | 要构建的产品类型列表，默认包含 `ascend950pr_9599_AIV` 和 `ascend910B1_AIC`。 |
| `TEST_MOD` | 要运行的 UT 目标过滤项，默认 `all`。 |
| `ASCCOMM_UT_RUN_AFTER_BUILD` | 是否在构建后运行 UT，默认 `OFF`。 |

## GTest 依赖

UT 会优先查找系统 GTest。若系统中没有 GTest，可以通过 `CANN_3RD_LIB_PATH` 指向 CANN third_party 目录。

```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<path-to-third-party>
```
