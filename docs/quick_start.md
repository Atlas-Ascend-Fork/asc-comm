# 快速开始

## 环境准备<a name="prepare&install"></a>

执行源码构建和UT验证前，请准备如下基础环境：

| 依赖 | 要求 |
| :---: | :---: |
| CANN Toolkit | 与当前分支或Tag配套 |
| python | python 3 |
| gcc/g++ | 支持C++17，建议gcc与g++版本一致 |
| cmake | >= 3.16.0 |

## 环境变量配置<a name="cann-env-setup"></a>

执行`build.sh`前，需要先配置CANN环境变量。

```bash
# 默认路径安装，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/cann/set_env.sh
# 指定路径安装
# source ${install_path}/cann/set_env.sh
```

## 源码编译步骤<a name="source-build"></a>

### 下载源码<a name="source-download"></a>

开发者可通过如下命令下载本仓源码：

```bash
git clone https://gitcode.com/hbhdhd/asc-comm.git
cd asc-comm
```

### 依赖检查<a name="dependency-check"></a>

本仓源码构建和UT验证依赖如下环境：

- CANN Toolkit
- python 3
- gcc/g++
- cmake >= 3.16.0

UT依赖googletest。若系统中没有GTest，可以通过`CANN_3RD_LIB_PATH`指向CANN third_party目录。详细说明请参考[三方依赖与兼容性](./guide/dependencies.md)。

### 编译<a name="compile&install"></a>

进入本仓代码根目录，执行如下命令：

```bash
bash build.sh
```

asc-comm当前AICore Hcomm接口为header-only形态。直接执行构建脚本时，脚本会完成环境检查；如需构建UT，请参考[UT验证](#ut-verify)。

### UT验证<a name="ut-verify"></a>

在开源仓根目录执行下列命令构建Hcomm UT。

```bash
bash build.sh -t
```

构建目录默认为：

```text
build/ut-hcomm
```

如果需要指定离线GTest路径，可直接使用CMake命令构建UT。

```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<path-to-third-party>
cmake --build build/ut-hcomm
```
