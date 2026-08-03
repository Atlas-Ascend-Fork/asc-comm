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

本地修改AICore头文件后，可生成轻量开发验证包，将当前工作树中的公开头文件和detail头文件安装到已有CANN环境，无需重新生成完整toolkit包：

```bash
bash build.sh --pkg
```

生成文件默认位于`build_out/`：

```text
Ascend-asccomm_1.0.0_linux-<arch>.run
```

其中`<arch>`为`aarch64`或`x86_64`。制包时会递归扫描`include/aicore/`下的全部`.h`文件，以及`src/aicore/`下各模块`detail/`目录中的全部`.h`文件，不指定固定模块、文件名或文件数量：

| 仓库扫描目录 | CANN安装目录 |
| --- | --- |
| `include/aicore/<module>/` | `asc/include/adv_api/<module>/` |
| `src/aicore/<module>/detail/` | `asc/impl/adv_api/detail/<module>/` |

例如，Hcomm头文件按原有规则安装到`asc/include/adv_api/hcomm/`和`asc/impl/adv_api/detail/hcomm/`。新增`include/aicore/ain/ain.h`与`src/aicore/ain/detail/impl/ain_impl.h`后，会自动安装为`asc/include/adv_api/ain/ain.h`与`asc/impl/adv_api/detail/ain/impl/ain_impl.h`。模块名和模块内部子目录结构都会保留。

因此，在上述扫描目录中新增`.h`文件后重新制包，新文件会自动进入run包。文件名、源码文件数量和安装文件数量均不固定，其他类型的文件不会进入包。run包清单只描述制包时存在的头文件，不是目标目录的完整镜像。首次安装时，仓库中已经删除或重命名的路径不会出现在清单中，安装器无法据此删除CANN中原有的旧路径文件。涉及删除或重命名时，不应仅依赖该run包验证目录最终状态。

常用参数如下：

| 参数 | 说明 |
| --- | --- |
| `--full` | 以full模式安装或更新包内全部AICore头文件。 |
| `--uninstall` | 恢复基线中的原文件，并删除安装前不存在的文件。 |
| `--check` | 校验run包归档和payload，不修改CANN。 |
| `--install-path=<PATH>` | 指定安装或卸载使用的绝对路径；默认使用`ASCEND_HOME_PATH`。 |
| `--force` | 放宽asc-devkit版本或已安装补丁文件一致性检查。 |
| `--list` | 列出run包中的文件，由run包的makeself外层提供。 |
| `-h`、`--help` | 显示帮助信息。 |

`--full`、`--uninstall`和`--check`是互斥操作，一次只能指定一个。

### 检查包内容

安装前可以检查归档和payload校验和，并查看包版本、源码提交、架构、要求的asc-devkit版本及文件数量。该操作不会修改CANN：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --check
```

列出run包内的文件：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --list
```

### 安装与卸载

安装到当前`ASCEND_HOME_PATH`：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --full
```

也可以通过绝对路径明确指定安装位置：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run \
    --full --install-path=/path/to/cann
```

安装器依次尝试将`--install-path`本身、`<install-path>/cann`或`<install-path>/ascend-toolkit/latest`解析为CANN根目录。解析后的目录必须包含`asc/include/adv_api/`、`asc/impl/adv_api/detail/`和`share/info/asc-devkit/version.info`，且当前用户必须是该CANN根目录的属主。

首次安装时，备份状态保存在解析后的CANN根目录下：

```text
var/asc-comm-dev-patch/baseline/
```

- 如果目标文件已经存在，安装器会保存原文件后再替换；卸载时恢复原文件及其权限。
- 如果目标文件原本不存在，安装器会将其记录为`absent`后创建；卸载时删除该文件。
- 同一CANN目录中重复安装run包时，始终保留各路径首次被补丁接管前的基线，不会把上一次安装的补丁文件覆盖到基线中。
- 如果上一个asc-comm补丁中的路径不再出现在新包中，重复安装会将该已记录路径恢复到原始状态。但安装器无法处理从未被补丁记录、且本次包中也不存在的旧CANN文件。

恢复安装前的文件。使用过`--install-path`安装时，卸载也应传入相同路径：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --uninstall
# 指定路径安装对应的卸载方式
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run \
    --uninstall --install-path=/path/to/cann
```

制包时会记录asc-devkit版本和系统架构。安装时要求系统架构匹配，并默认要求目标asc-devkit版本与制包环境一致。重复安装或卸载前还会校验当前已安装的补丁文件，防止覆盖或丢弃用户在CANN目录中直接进行的修改。

仅在明确了解兼容性和数据覆盖风险时使用`--force`。安装时，该参数可以跳过asc-devkit版本不一致和已安装补丁文件被修改的检查；卸载时，可以强制丢弃已安装补丁文件上的修改并恢复基线。`--force`不能绕过系统架构检查或包内payload校验。

安装完成后需要重新编译待验证的Ascend C Kernel，已有`.o`或`.bin`不会自动更新。

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
