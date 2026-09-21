#!/bin/bash
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set -eu

rank_size=2
ipport="tcp://127.0.0.1:8899"
local_npus=2
first_rank=0
first_device=0
bin_path="./build/one_multi_path"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -pes)
            rank_size="$2"
            shift 2
            ;;
        -ipport)
            ipport="$2"
            shift 2
            ;;
        -gnpus)
            local_npus="$2"
            shift 2
            ;;
        -fpe)
            first_rank="$2"
            shift 2
            ;;
        -fnpu)
            first_device="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

pids=()
for local_idx in $(seq 0 $((local_npus - 1))); do
    rank=$((first_rank + local_idx))
    device=$((first_device + local_idx))
    "${bin_path}" "${rank_size}" "${rank}" "${ipport}" "${device}" &
    pids+=("$!")
done

ret=0
for pid in "${pids[@]}"; do
    wait "${pid}" || ret=$?
    echo "wait ${pid} finished"
done

exit "${ret}"
