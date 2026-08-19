# Hcomm开发run包构建与安装

开发run包用于将当前工作树中的Hcomm Device头文件、实现头文件和Host辅助头文件安装到已有CANN环境，
便于本地编译和功能验证。该run包不是完整的CANN安装包。

## 构建run包

先加载目标CANN环境：

```bash
source /path/to/cann/set_env.sh
```

在仓库根目录执行：

```bash
bash scripts/package/build_package.sh
```

默认生成文件：

```text
build_out/Ascend-asccomm_1.0.0_linux-<arch>.run
```

也可以显式指定CANN路径、输出目录和包版本：

```bash
bash scripts/package/build_package.sh \
    --cann-path=/path/to/cann \
    --output-dir=/path/to/output \
    --package-version=1.0.0
```

run包中的目录映射如下：

| 仓库目录 | CANN安装目录 |
| --- | --- |
| `include/aicore/hcomm/` | `asc/include/adv_api/hcomm/` |
| `src/aicore/hcomm/` | `asc/impl/adv_api/detail/hcomm/` |
| `include/hcomm/` | `<arch>-linux/include/hcomm/` |

## 检查run包

校验run包：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --check
```

查看包内文件：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --list
```

## 安装

未传入`--install-path`时，安装器使用环境变量`ASCEND_HOME_PATH`。因此，加载目标CANN的
`set_env.sh`后可以直接安装：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --full
```

也可以显式指定安装路径：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run \
    --full --install-path=/path/to/cann
```

`--install-path`必须是绝对路径。安装器依次检查该路径、`<install-path>/cann`和
`<install-path>/ascend-toolkit/latest`，并选择其中有效的CANN根目录。安装命令必须由CANN根目录的
属主执行。

首次安装时，原文件和目录权限备份在：

```text
<cann-root>/var/asc-comm-dev-patch/baseline/
```

安装完成后需要重新编译Ascend C Kernel，已有目标文件不会自动更新。

## 卸载

未显式指定安装路径时，加载安装时使用的同一CANN环境后执行：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --uninstall
```

如果安装时传入了`--install-path`，卸载时应传入相同路径：

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run \
    --uninstall --install-path=/path/to/cann
```

卸载会恢复安装前存在的文件和目录权限，并删除安装前不存在的文件。若安装后的受管文件被手动修改，
安装器会停止卸载以避免丢失修改；确认可以丢弃这些修改后，可增加`--force`执行。
