# 构建与测试

## 环境准备

执行`build.sh`前，需要先source CANN环境脚本设置环境变量（`build.sh`会检查`ASCEND_HOME_PATH`，无论是否构建UT均需配置）。

```bash
# 默认路径安装，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/cann/set_env.sh
# 指定路径安装
# source ${install_path}/cann/set_env.sh
```

基础环境和三方依赖清单见[三方依赖与兼容性](./dependencies.md)。

## 构建说明

直接执行构建脚本时，脚本会完成基础环境检查。UT需要通过测试构建参数单独触发。

```bash
bash build.sh
```

## 构建UT

使用`-t`或`--test`构建Hcomm UT。

```bash
bash build.sh -t
```

构建目录默认为：

```text
build/ut-hcomm
```

## CMake入口

UT的CMake入口为：

```text
tests/ut/CMakeLists.txt
```

常用CMake变量：

| 变量 | 说明 |
| --- | --- |
| `ASCEND_CANN_PACKAGE_PATH` | CANN包路径。未显式指定时，优先从环境变量推导。 |
| `PRODUCT_TYPE_LIST` | 要构建的产品类型列表，默认包含`ascend950pr_9599_AIV`和`ascend910B1_AIC`。 |
| `TEST_MOD` | 要运行的UT目标过滤项，默认`all`。 |
| `ASCCOMM_UT_RUN_AFTER_BUILD` | 是否在构建后运行UT，默认`ON`。如只需构建不运行，可设置为`OFF`。 |

## GTest依赖

UT会优先查找系统GTest。若系统中没有GTest，可以通过`CANN_3RD_LIB_PATH`指向CANN third_party目录。

```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<path-to-third-party>
```

`build.sh -t`当前未暴露`CANN_3RD_LIB_PATH`参数；需要指定离线GTest路径时，建议直接使用上述CMake命令构建UT。

## 样例构建与运行

`examples/hcomm_write_read_nbi`提供Hcomm `WriteNbi`和`ReadNbi`点对点通信样例。该样例使用独立CMake工程构建：

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/hcomm_write_read_nbi
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

样例默认可直接启动两个rank：

```bash
./demo
```

也可以手动指定rank运行：

```bash
# 终端1：rank 0
./demo 0 2 tcp://127.0.0.1:29621

# 终端2：rank 1
./demo 1 2 tcp://127.0.0.1:29621
```

样例支持Ascend 950PR/Ascend 950DT，要求CANN 9.1.0或以上版本。运行样例需要至少2张NPU；单卡环境仅支持编译验证。
