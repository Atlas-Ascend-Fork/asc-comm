#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
set -euo pipefail

if [ "$#" -lt 4 ] || [ "$#" -gt 9 ]; then
    echo "用法：$0 <卡数> <rank_table.json> <notify|faa|cas> <次数> [预热次数] [batch] [最小长度指数] [最大长度指数] [日志文件]"
    exit 1
fi

nranks="$1"
rank_table="$2"
mode="$3"
iterations="$4"
warmup="${5:-100}"
batch="${6:-0}"
min_exp="${7:-3}"
max_exp="${8:-20}"

if ! [[ "$nranks" =~ ^[0-9]+$ ]] || [ "$nranks" -lt 2 ]; then
    echo "卡数必须是大于等于2的整数"
    exit 1
fi
if [ ! -f "$rank_table" ]; then
    echo "rank table文件不存在：$rank_table"
    exit 1
fi
case "$mode" in notify|faa|cas) ;; *) echo "不支持的接口：$mode"; exit 1 ;; esac
if ! [[ "$iterations" =~ ^[0-9]+$ ]] || [ "$iterations" -lt 1 ]; then
    echo "次数必须是正整数"
    exit 1
fi
if ! [[ "$warmup" =~ ^[0-9]+$ ]]; then
    echo "预热次数必须是非负整数"
    exit 1
fi
if ! [[ "$batch" =~ ^[0-9]+$ ]]; then
    echo "batch必须是非负整数；0表示等于正式测试次数"
    exit 1
fi
if [ "$batch" -eq 0 ] || [ "$batch" -gt "$iterations" ]; then
    batch="$iterations"
fi
if ! [[ "$min_exp" =~ ^[0-9]+$ ]] || ! [[ "$max_exp" =~ ^[0-9]+$ ]] || \
    [ "$min_exp" -gt "$max_exp" ] || [ "$max_exp" -gt 30 ]; then
    echo "长度指数必须满足 0 <= 最小值 <= 最大值 <= 30"
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
binary="${script_dir}/build/simt_notify_atomic_perf"
if [ ! -x "$binary" ]; then
    echo "可执行文件不存在或不可执行：$binary"
    exit 1
fi

mkdir -p "${script_dir}/results"
log_file="${9:-${script_dir}/results/${mode}_$(date +%Y%m%d_%H%M%S).log}"
mkdir -p "$(dirname "$log_file")"
exec > >(tee "$log_file") 2>&1

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/asc_comm_simt_notify_atomic_perf.XXXXXX")"
pids=()
cleanup() {
    if [ ${#pids[@]} -ne 0 ]; then
        kill "${pids[@]}" 2>/dev/null || true
    fi
    rm -rf "$work_dir"
}
trap cleanup EXIT SIGINT SIGTERM

run_case() {
    local path="$1"
    local op="$2"
    local bytes="$3"
    local case_dir="${work_dir}/${path}_${op}_${bytes}"
    mkdir -p "$case_dir"
    export ASC_COMM_SIMT_NOTIFY_ATOMIC_PERF_SYNC_DIR="$case_dir"

    pids=()
    for ((rank = 0; rank < nranks; ++rank)); do
        "$binary" "$rank" "$nranks" "$rank_table" "$path" "$op" \
            "$iterations" "$warmup" "$bytes" "$batch" >"${case_dir}/rank_${rank}.log" 2>&1 &
        pids+=("$!")
    done

    local status=0
    for pid in "${pids[@]}"; do
        if ! wait "$pid"; then
            status=1
        fi
    done
    pids=()
    cat "${case_dir}/rank_0.log"
    if [ "$status" -ne 0 ]; then
        for ((rank = 1; rank < nranks; ++rank)); do
            cat "${case_dir}/rank_${rank}.log"
        done
    fi
    return "$status"
}

echo "计时口径：仅统计填写WQE和敲DB；Drain/PollCq位于计时区间外；batch=${batch}"
if [ "$mode" = "notify" ]; then
    for ((exp = min_exp; exp <= max_exp; ++exp)); do
        bytes=$((1 << exp))
        run_case simt notify "$bytes"
        run_case aiv notify "$bytes"
        run_case aiv write "$bytes"
        echo
    done
else
    run_case simt "$mode" 8
    run_case aiv "$mode" 8
fi

echo "日志：$log_file"
if [ "$mode" = "notify" ]; then
    echo "画图：python3 ${script_dir}/plot.py $log_file"
fi
