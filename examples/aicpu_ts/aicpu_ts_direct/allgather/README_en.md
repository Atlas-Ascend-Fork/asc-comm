# Custom Communication Op - AllGather

## Sample Introduction

This sample shows how to develop an AllGather communication op with the HCCL communication programming APIs. It covers:

1. Acquiring communication resources through the HCOMM control plane and implementing the AllGather collective with asc-comm POD AICPU dataplane primitives.
2. Building and deploying the custom op package independently.

This sample uses the control-plane-parsed POD entity architecture. Prepare matching asc-comm and hcomm source environments before building, and set `ASC_COMM_SOURCE_DIR`, `HCOMM_SOURCE_DIR`, `ASCCOMM_DEVICE_LIBRARY`, and `HCOMM_DEVICE_LIBRARY` when needed. CMake rejects an incompatible CANN dataplane header at configuration time.

## Directory Layout

```text
├── CMakeLists.txt                      # Build configuration
├── op_host/
│   ├── allgather.cc                  # AllGatherCustom implementation
├── op_kernel_aicpu/
│   ├── libcustom_allgather_aicpu_kernel.json  # AICPU kernel description
│   ├── aicpu_kernel.cc                 # AICPU kernel implementation in C++ mode
├── inc/
│   ├── aicpu_ts_custom_allgather.h         # Custom AllGather API header
│   └── common.h                        # Common types
└── testcase/
    ├── main.cc                         # Sample implementation
    └── Makefile                        # Sample build configuration
```

The custom op build uses the local [cmake](../cmake) modules and [build.sh](../build.sh).

## Environment Preparation

### Requirements

This sample supports Ascend 950PR&950DT products in a single-host, N-device topology (N >= 2).

Required tools:

- gcc and g++ 7.3.0 to 13.3.x
- cmake >= 3.16.0

### Install CANN Toolkit

Install the CANN Toolkit by following the [CANN installation guide](https://www.hiascend.com/document/redirect/CannCommunityInstWizard).

### Set Environment Variables

```bash
source /usr/local/Ascend/cann/set_env.sh
# Or, for a custom installation path:
# source $install_path/cann/set_env.sh
```

If hccl is not installed, hcomm may fail to initialize the communication domain because `$ASCEND_HOME_PATH/share/info/hccl/version.info` is missing. Install hccl, or create the following placeholder file for this sample:

```bash
mkdir -p $ASCEND_HOME_PATH/share/info/hccl
echo 'Version=9.2.0' > $ASCEND_HOME_PATH/share/info/hccl/version.info
```

## Build the Custom Op Package

From the `examples/aicpu_ts/aicpu_ts_direct` directory, run:

```bash
bash build.sh \
  --vendor=cust \
  --ops=allgather_aicpu \
  --custom_ops_path=./allgather
```

- `--vendor` is the custom op vendor ID.
- `--ops` is the custom op name.
- `--custom_ops_path` is the custom op project path.

## Install the Custom Op Package

The run package is generated under `./build_out`:

```bash
./build_out/cann-custom_allgather_aicpu_linux-<arch>.run --install --install-path=<ascend_cann_path>
```

Installed files include:

- Header: `$ASCEND_HOME_PATH/opp/vendors/cust/include/aicpu_ts_custom_allgather.h`
- Host library: `$ASCEND_HOME_PATH/opp/vendors/cust/lib64/libhccl_custom_allgather_aicpu.so`
- AICPU kernel description: `$ASCEND_HOME_PATH/opp/vendors/cust/aicpu/config/libcustom_allgather_aicpu_kernel.json`
- AICPU kernel package: `$ASCEND_HOME_PATH/opp/vendors/cust/aicpu/kernel/aicpu_custom_allgather.tar.gz`
- Install script: `$ASCEND_HOME_PATH/opp/vendors/cust/scripts/install.sh`

## Run the Custom Op

### Disable AICPU Package Signature Verification

A package built from source has no signature. Use the `npu-smi` tool from Ascend HDK 25.5.T2.B001 or later to disable custom-op verification. Run the following as root on the physical machine:

```shell
npu-smi set -t custom-op-secverify-enable -i 0 -d 1
npu-smi set -t custom-op-secverify-mode -i 0 -d 0
```

> Disabling driver signature verification introduces security risk. Ensure the custom communication op is trusted and secure.

### Add the AICPU Package to the Load Whitelist

Append the following entry to `$ASCEND_HOME_PATH/conf/ascend_package_load.ini`:

```ini
name:aicpu_custom_allgather.tar.gz
install_path:2
optional:true
package_path:opp/vendors/cust/aicpu/kernel
load_as_per_soc:false
```

### Build the Testcase

```bash
cd examples/aicpu_ts/aicpu_ts_direct/allgather/testcase
make
```

### Run the Testcase

```bash
make test

# Or run the binary directly:
export ASCEND_DEVICE_LIB_DIR=$ASCEND_HOME_PATH/lib64/device/lib64
export LD_LIBRARY_PATH=$ASCEND_HOME_PATH/opp/vendors/cust/lib64:$ASCEND_DEVICE_LIB_DIR:$LD_LIBRARY_PATH
./custom_allgather_aicpu
```

### Expected Output

The sample gathers every rank input into each rank output in rank order. With two devices, the output is similar to:

```text
Found 2 NPU device(s) available
rankId: 1, input: [1 1]
rankId: 0, input: [0 0]
rankId: 0, output: [ 0 0 1 1 ]
rankId: 1, output: [ 0 0 1 1 ]
```
