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

## 构建开发验证run包

本地修改Hcomm头文件后，可生成轻量开发验证包，将当前工作树中的修改安装到已有CANN环境，无需重新生成完整toolkit包：

```bash
bash build.sh --pkg
```

生成文件默认位于`build_out/`：

```text
Ascend-asccomm_1.0.0_linux-<arch>.run
```

安装到当前`ASCEND_HOME_PATH`：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --install
```

也可以明确指定CANN根目录。`--install-path`应指向包含`asc/`和`share/info/asc-devkit/`的实际CANN目录：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run \
    --install --install-path=/path/to/cann
```

安装器只替换与CANN中asc-devkit布局一致的Hcomm头文件：公开头文件安装到`asc/include/adv_api/hcomm/`，detail头文件安装到`asc/impl/adv_api/detail/hcomm/`。原文件在首次安装时保存到目标CANN的`var/asc-comm-dev-patch/`，重复安装不会覆盖这份原始基线。

恢复安装前的文件：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --uninstall
```

制包时会记录asc-devkit版本和系统架构，安装时默认要求目标环境一致。仅在明确了解兼容性风险时使用`--force`跳过版本或已安装文件一致性检查。安装完成后需要重新编译待验证的Ascend C Kernel，已有`.o`或`.bin`不会自动更新。

可以直接调用制包脚本指定版本、输出目录和制包所用CANN：

```bash
bash scripts/package/build_package.sh \
    --cann-path=/path/to/cann \
    --package-version=1.0.0 \
    --output-dir=/path/to/output
```

在临时构造的CANN目录中验证安装、重复安装、文件保护和卸载恢复：

```bash
bash tests/package/test_run_package.sh \
    build_out/Ascend-asccomm_1.0.0_linux-<arch>.run
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

也可以通过构建脚本传入CANN third_party目录：

```bash
bash build.sh -t --cann_3rd_lib_path=<path-to-third-party>
```

## 样例构建与运行

`examples/hcomm_write_read_nbi`提供AIV直驱URMA `WriteNbi`和`ReadNbi`点对点通信样例。该样例使用独立CMake工程构建：

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
