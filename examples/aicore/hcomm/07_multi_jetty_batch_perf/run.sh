#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
# Run the two-rank SIMT matrix and store all per-rank logs alongside the summary.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
batch=256
api=all
jetty_counts=1,2,4,8
log_file=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --batch-size) batch="${2:?batch size required}"; shift 2 ;;
        --api) api="${2:?API required}"; shift 2 ;;
        --jetty-counts) jetty_counts="${2:?Jetty list required}"; shift 2 ;;
        --log) log_file="${2:?log path required}"; shift 2 ;;
        -h|--help) echo "Usage: run.sh [--batch-size N] [--api all|write|read|notify] [--jetty-counts 1,2,4,8] [--log FILE]"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done
[[ "$batch" =~ ^(1|8|32|64|128|256)$ && "$api" =~ ^(all|write|read|notify)$ ]] || { echo "Invalid batch size or API" >&2; exit 1; }
profile="simt-matrix-bs${batch}"
[[ "$jetty_counts" =~ ^(1|2|4|8)(,(1|2|4|8))*$ ]] || { echo "Invalid Jetty list" >&2; exit 1; }
IFS=',' read -r -a jettys <<< "$jetty_counts"
declare -A seen=()
for j in "${jettys[@]}"; do
    [[ -z "${seen[$j]:-}" ]] || { echo "Duplicate Jetty count: $j" >&2; exit 1; }
    seen[$j]=1
done
binary="${script_dir}/build/${profile}/multi_jetty_batch_perf"
expected="BUILD_CONFIG | Backend=simt | Layout=matrix-v4 | CompletionVersion=1 | MatrixBatch=${batch}"
[[ -x "$binary" && "$("$binary" --build-info)" == "$expected" ]] || { echo "Binary configuration does not match selected batch; build it first" >&2; exit 1; }
[[ -f "${script_dir}/build/${profile}/build-ready.sha256" ]] &&
    sha256sum --check --status "${script_dir}/build/${profile}/build-ready.sha256" || { echo "Binary has no matching successful build" >&2; exit 1; }
[[ -n "$log_file" ]] || log_file="${script_dir}/results/matrix_bs${batch}_$(date +%Y%m%d_%H%M%S).log"
mkdir -p "$(dirname "$log_file")"
log_dir="$(mktemp -d "${log_file}.ranks.XXXXXX")"
exec > >(tee "$log_file") 2>&1
# Write identity and log locations to the metadata file.
{
    echo "RUN_IDENTITY | Head=$(git -C "$script_dir" rev-parse HEAD 2>/dev/null || echo UNKNOWN) | Tree=$([[ -z "$(git -C "$script_dir" status --porcelain 2>/dev/null)" ]] && echo clean || echo dirty)"
    echo "$expected"
    echo "RUN_BINARY | Profile=${profile} | Path=${binary}"
    sha256sum "$binary"
    echo "RANK_LOGS | Directory=${log_dir}"
} > "${log_file}.metadata"
apis=(write read notify)
[[ "$api" == all ]] || apis=("$api")
pids=()
cleanup() { if [[ ${#pids[@]} -gt 0 ]]; then kill "${pids[@]}" 2>/dev/null || true; fi; }
trap cleanup EXIT
trap 'exit 130' INT TERM
cases=0
passed=0
failed=0
warmup=$(((96 + batch - 1) / batch * batch))
for current_api in "${apis[@]}"; do
    for j in "${jettys[@]}"; do
        dir="${log_dir}/${current_api}_j${j}"
        mkdir -p "$dir"
        pids=()
        control_endpoint="tcp://127.0.0.1:$((20000 + ($$ % 40000)))"
        for rank in 0 1; do
            timeout --signal=TERM --kill-after=5s 180s \
                "$binary" "$rank" 2 "$control_endpoint" "$current_api" 1024 "$warmup" 64 "$batch" "$j" \
                >"${dir}/rank_${rank}.log" 2>&1 &
            pids+=("$!")
        done
        status=PASS
        codes=()
        for pid in "${pids[@]}"; do
            code=0
            wait "$pid" || code=$?
            codes+=("$code")
            [[ "$code" == 0 ]] || status=FAIL
        done
        pids=()
        # Successful process exits alone do not establish correctness.
        grep -q '^COMPLETION_CHECK | Status=PASS$' "${dir}/rank_0.log" || status=FAIL
        data_rank=1
        [[ "$current_api" != read ]] || data_rank=0
        grep -q '^DATA_AUDIT_SUMMARY .* | BadRequests=0$' "${dir}/rank_${data_rank}.log" || status=FAIL
        if [[ "$current_api" == notify ]]; then
            grep -q '^NOTIFY_AUDIT_SUMMARY | BadNotifies=0$' "${dir}/rank_1.log" || status=FAIL
        fi
        perf="$(grep -m1 '^PERF_DATA |' "${dir}/rank_0.log" || true)"
        [[ -n "$perf" ]] || status=FAIL
        if [[ -n "$perf" ]]; then
            echo "${perf/PERF_DATA/RESULT} | Pair=0-1 | Status=${status}"
        else
            echo "RESULT | API=${current_api} | Mode=MultiJetty | DataSize/B=64 | BatchSize=${batch} | Jettys=${j} | ConcurrentJettys=$((j < 1024 / batch ? j : 1024 / batch)) | IssueTicksPerRequest=NA | Pair=0-1 | Status=FAIL"
        fi
        cases=$((cases + 1))
        if [[ "$status" == PASS ]]; then
            passed=$((passed + 1))
        else
            failed=$((failed + 1))
            echo "CASE_ERROR | API=${current_api} | Jettys=${j} | RankExit=${codes[*]} | Logs=${dir}"
            cat "${dir}"/rank_*.log
        fi
        # Collect validation failures, stop after timeout, signal or setup error.
        if grep -qE '^(ACL failure|HCCL failure)' "${dir}"/rank_*.log; then
            echo "MATRIX_SUMMARY | BatchSize=${batch} | Cases=${cases} | Pass=${passed} | Fail=${failed} | Status=ABORTED"
            exit 1
        fi
        for code in "${codes[@]}"; do
            if [[ "$code" != 0 && "$code" != 2 ]]; then
                echo "MATRIX_SUMMARY | BatchSize=${batch} | Cases=${cases} | Pass=${passed} | Fail=${failed} | Status=ABORTED"
                exit 1
            fi
        done
    done
done
echo "MATRIX_SUMMARY | BatchSize=${batch} | Cases=${cases} | Pass=${passed} | Fail=${failed} | Status=$([[ $failed == 0 ]] && echo PASS || echo FAIL)"
[[ "$failed" == 0 ]]
