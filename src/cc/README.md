<!--
Copyright (c) 2026 Huawei Technologies Co., Ltd.
This program is free software, you can redistribute it and/or modify it under the terms and conditions of
CANN Open Software License Agreement Version 2.0 (the "License").
See LICENSE in the root of the software repository for the full text of the License.
-->

# CC Communication Client

This directory is the migration target of `impl/adv_api/detail/hccl` from
asc-devkit (device-side client, host-side `mc2_client` algorithms, KFC server
sources, and tiling; see Issue #66 for the migration plan). Modules are
migrated in stages: common, aicpu_kfc, then ops.
