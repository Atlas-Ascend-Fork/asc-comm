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

默认构建会复用已有的`build/`目录，不会自动清理构建产物。需要清理时，请显式执行`bash build.sh --make_clean`。

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

本地修改asc-comm头文件后，可生成轻量开发验证包，将当前工作树中的相关头文件安装到已有CANN环境，无需重新生成完整toolkit包：

```bash
bash build.sh --pkg
```

生成文件默认位于`build_out/`：

```text
cann-asc-comm_9.2.0_linux-<arch>.run
```

其中`<arch>`为`aarch64`或`x86_64`。制包时会递归扫描以下目录中的全部`.h`文件：

| 仓库扫描目录 | CANN安装目录 |
| --- | --- |
| `include/aicore/hcomm/` | `asc/include/adv_api/hcomm/` |
| `src/aicore/hcomm/` | `asc/impl/adv_api/detail/hcomm/` |
| `include/`中的其他目录 | `asc/include/comm_api/`，保留相对路径 |
| `src/`中的其他目录 | `asc/impl/comm_api/`，保留相对路径 |

安装目录沿用asc-devkit子包布局，各扫描目录内部的相对目录结构会保留。安装时还会创建以下Hcomm软链：

```text
asc/include/comm_api/aicore/hcomm -> ../../adv_api/hcomm
asc/impl/comm_api/aicore/hcomm    -> ../../adv_api/detail/hcomm
```

例如，`include/aicore/ain/`安装到`asc/include/comm_api/aicore/ain/`，`include/ccu/`安装到`asc/include/comm_api/ccu/`。run包仅收集上述目录中制包时存在的`.h`文件；`.cpp`、`.inc`等其他类型的文件不会进入包。在这些目录中新增头文件后重新制包，新文件会自动进入run包。

> 注意：run包不会清理目标CANN中未列入包清单的文件。验证头文件删除或重命名时，需要先清理目标CANN中的对应旧文件。

常用参数如下：

| 参数 | 说明 |
| --- | --- |
| `--full` | 以full模式安装或更新包内全部asc-comm头文件及Hcomm软链。 |
| `--uninstall` | 恢复基线中的原文件，并删除安装前不存在的文件。 |
| `--check` | 校验run包归档、payload和依赖兼容性，不修改CANN。 |
| `--install-path=<PATH>` | 指定Ascend安装根目录；root默认使用`/usr/local/Ascend`，非root默认使用`$HOME/Ascend`。 |
| `--install-for-all` | 安装时允许所有用户读取和访问包内文件；root安装默认启用。 |
| `--force` | 安装或卸载时丢弃包管理文件中的用户修改。 |
| `--quiet` | 静默执行，适用于脚本调用。 |
| `--list` | 列出run包中的文件，由run包的makeself外层提供。 |
| `-h`、`--help` | 显示帮助信息。 |

`--full`、`--uninstall`和`--check`是互斥操作，一次只能指定一个。

### 检查包内容

安装前可以检查归档和payload校验和，并查看包版本、源码提交、架构、要求的asc-devkit、hcomm、runtime版本及文件数量。该操作不会修改CANN：

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --check
```

列出run包内的文件：

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --list
```

### 安装

安装到默认Ascend根目录：

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --full
```

也可以通过绝对路径明确指定安装位置：

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --full --install-path=/path/to/cann
```

安装器依次尝试将`--install-path`本身、`<install-path>/cann`或`<install-path>/ascend-toolkit/latest`解析为CANN根目录。解析后的目录必须包含`asc/include/adv_api/`、`asc/impl/adv_api/detail/`和`share/info/`，且当前用户必须是该CANN根目录的属主。

非root用户需要允许其他用户使用安装内容时，执行：

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --full --install-for-all --install-path=/path/to/cann
```

- 权限行为与asc-devkit子包一致。root安装时，头文件权限为`555`，Hcomm目录权限为`755`，`comm_api`下的受管目录权限为`555`。
- 非root安装默认将上述权限分别设置为`550`、`750`和`550`；使用`--install-for-all`后设置为`555`、`755`和`555`。目标CANN根目录及其父目录需要允许其他用户读取和进入，否则安装器会拒绝操作。
- 安装器会保存安装前已存在的受管文件和正确的Hcomm软链。目标错误的软链，或者占用软链路径的普通文件、目录，不会被覆盖。
- 制包时会记录asc-devkit、hcomm、runtime的主次版本和系统架构。系统架构必须匹配；依赖版本不兼容时会输出告警并继续安装。

### 重复安装

使用`--full`将新run包安装到同一CANN目录即可更新。安装器会保留第一次安装前的文件，用于后续卸载。

如果包管理的文件或软链被手工修改，重复安装会拒绝覆盖。确认可以丢弃这些修改后，使用：

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --full --force --install-path=/path/to/cann
```

`--force`不能跳过系统架构检查、包内payload校验，也不会覆盖目标错误的软链或占用软链路径的普通文件、目录。

### 卸载

恢复安装前的文件。使用过`--install-path`安装时，卸载也应传入相同路径：

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --uninstall
# 指定路径安装对应的卸载方式
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --uninstall --install-path=/path/to/cann
```

卸载会恢复安装前已存在的文件及其权限，并删除由run包新增的文件和软链。如果包管理的文件或软链被手工修改，卸载会拒绝操作；确认可以丢弃这些修改后，在卸载命令中增加`--force`。

安装、重复安装和卸载过程会写入日志。root用户日志目录为`/var/log/ascend_seclog`，非root用户为`$HOME/var/log/ascend_seclog`；过程日志写入`ascend_install.log`，操作审计写入`operation.log`。

安装完成后需要重新编译待验证的内容，已有`.o`或`.bin`不会自动更新。

run包版本从仓库根目录的`version.cmake`读取。可以直接调用制包脚本指定输出目录和制包所用CANN：

```bash
bash scripts/package/build_package.sh \
    --cann_path=/path/to/cann \
    --output-dir=/path/to/output
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

`examples/aicore/hcomm/01_hcomm_write_read_nbi`提供AIV直驱URMA `WriteNbi`和`ReadNbi`点对点通信样例。该样例使用独立CMake工程构建：

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/aicore/hcomm/01_hcomm_write_read_nbi
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
