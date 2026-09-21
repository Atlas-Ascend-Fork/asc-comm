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
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: run.sh [all|simd-serial|simt-serial|simt-parallel] [options]
Options:
  --api API            write, read, notify, or all; default: all
  --batch-sizes LIST   default: 1,8,32,64,128,256
  --jetty-counts LIST  default: 1,2,4,8
  --iterations N       timed operations per Jetty; default: 1024
  --warmup N           requested warmup operations per Jetty; default: 96
  --payload-bytes N    default: 64
  --devices N          2 or 8; default: 2
  --log FILE           result log path
  --build-profile NAME all-full, all-fast, simt-full, simt-fast, or simd-full
                       simt-scope-control (A/N), simt-scope-publisher (P), simt-scope-sq-prepare (Q)
                       simt-scope-parallel-publish (R), simt-scope-publish-core (S), simt-scope-publish-audit (S-parallel-3barriers, CompletionV1)
  # simt-matrix-bs1/8/32/64/128/256: current CompletionV1 full matrix, one batch per artifact
  --include-simt-serial  include the diagnostic SIMT-Serial mode in all
EOF
}

mode="${1:-all}"
if [[ $# -gt 0 ]]; then shift; fi
api=all
batch_sizes="1,8,32,64,128,256"
batch_sizes_set=false
build_profile=""
jetty_counts="1,2,4,8"
iterations=1024
warmup=96
payload_bytes=64
devices=2
log_file=""
include_simt_serial=false
case_timeout_seconds="${MULTI_JETTY_BATCH_PERF_TIMEOUT_SECONDS:-180}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --api) api="$2"; shift 2 ;;
        --batch-sizes) batch_sizes="$2"; batch_sizes_set=true; shift 2 ;;
        --build-profile) build_profile="$2"; shift 2 ;;
        --jetty-counts) jetty_counts="$2"; shift 2 ;;
        --iterations) iterations="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --payload-bytes) payload_bytes="$2"; shift 2 ;;
        --devices) devices="$2"; shift 2 ;;
        --log) log_file="$2"; shift 2 ;;
        --include-simt-serial) include_simt_serial=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

[[ "$mode" == "all" || "$mode" == "simd-serial" || "$mode" == "simt-serial" ||
   "$mode" == "simt-parallel" ]] || { usage; exit 1; }
[[ "$api" == "all" || "$api" == "write" || "$api" == "read" || "$api" == "notify" ]] || {
    echo "api must be write, read, notify, or all" >&2; exit 1;
}
[[ "$devices" == "2" || "$devices" == "8" ]] || { echo "devices must be 2 or 8" >&2; exit 1; }
[[ "$iterations" =~ ^[0-9]+$ && "$iterations" -gt 0 ]] || {
    echo "iterations must be positive" >&2; exit 1;
}
[[ "$warmup" =~ ^[0-9]+$ ]] || { echo "warmup must be non-negative" >&2; exit 1; }
[[ "$payload_bytes" =~ ^[0-9]+$ && "$payload_bytes" -gt 0 ]] || {
    echo "payload-bytes must be positive" >&2; exit 1;
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# An explicit profile never falls back to another (possibly stale) binary.
if [[ -z "$build_profile" && -x "${script_dir}/build/all-full/multi_jetty_batch_perf" ]]; then
    build_profile=all-full
fi
backend=all
fast_build=0
scope=none
matrix_batch=0
binary="${script_dir}/build/multi_jetty_batch_perf"
if [[ -n "$build_profile" ]]; then
    case "$build_profile" in
        all-full|all-fast|simt-full|simt-fast|simd-full) ;;
        simt-matrix-bs1|simt-matrix-bs8|simt-matrix-bs32|simt-matrix-bs64|simt-matrix-bs128|simt-matrix-bs256)
            scope=matrix; matrix_batch="${build_profile##*bs}" ;;
        simt-scope-control) scope=control ;;
        simt-scope-publisher) scope=publisher ;;
        simt-scope-sq-prepare) scope=sq-prepare ;;
        simt-scope-parallel-publish) scope=parallel-publish ;;
        simt-scope-publish-core) scope=publish-core ;;
        simt-scope-publish-audit) scope=publish-audit ;;
        *) echo "invalid build profile: $build_profile" >&2; exit 1 ;;
    esac
    backend="${build_profile%-*}"
    [[ "$scope" == none ]] || backend=simt
    [[ "$build_profile" != *-fast ]] || fast_build=1
    binary="${script_dir}/build/${build_profile}/multi_jetty_batch_perf"
fi
[[ -x "$binary" ]] || { echo "binary not found: $binary; build the selected profile first" >&2; exit 1; }
build_info="BUILD_CONFIG | Backend=legacy | Fast=unknown"
if [[ -n "$build_profile" ]]; then
    build_info="$("$binary" --build-info)"
    expected_build_info="BUILD_CONFIG | Backend=${backend} | Fast=${fast_build}"
    [[ "$scope" == none ]] || expected_build_info+=" | Scope=${scope}"
    [[ "$scope" != publish-audit ]] || expected_build_info+=" | AuditVersion=9 | CompletionVersion=1"
    if [[ "$scope" == matrix ]]; then
        expected_build_info+=" | AuditVersion=10 | CompletionVersion=1 | MatrixBatch=${matrix_batch} | Profiling=0"
    fi
    [[ "$build_info" == "$expected_build_info" ]] || {
        echo "binary configuration does not match profile ${build_profile}: ${build_info}" >&2; exit 1;
    }
fi
if [[ "$scope" != none ]]; then
    if [[ "$scope" == matrix ]]; then
        if [[ "$batch_sizes_set" == false ]]; then batch_sizes="$matrix_batch"; fi
        [[ "$mode" == simt-parallel && "$batch_sizes" == "$matrix_batch" && "$iterations" == 1024 &&
            "$warmup" == 96 && "$payload_bytes" == 64 && "$devices" == 2 &&
            -z "${MULTI_JETTY_ACL_CONFIG:-}" && ( -z "${MULTI_JETTY_BATCH_TRACE:-}" || "${MULTI_JETTY_BATCH_TRACE}" == 0 ) ]] || {
            echo "Matrix requires selected batch, 64B, 1024 requests, warmup96, two devices, trace off, default ACL" >&2; exit 1;
        }
    else
    [[ "$mode" == simt-parallel && "$api" == write && "$batch_sizes" == 256 && "$jetty_counts" == 4 &&
        "$iterations" == 1024 && ( "$warmup" == 96 || "$warmup" == 256 ) &&
        "$payload_bytes" == 64 && "$devices" == 2 && -z "${MULTI_JETTY_ACL_CONFIG:-}" &&
        ( -z "${MULTI_JETTY_BATCH_TRACE:-}" || "${MULTI_JETTY_BATCH_TRACE}" == 0 ) ]] || {
            echo "Scope profiles require Write/64B/BS256/J4, 1024 requests, warmup96 or 256, two devices, trace off, default ACL config" >&2
            exit 1
        }
    fi
    scope_schedule=N
    if [[ "$scope" == publish-audit || "$scope" == matrix ]]; then
        [[ "${MULTI_JETTY_PARALLEL_CONTROL:-}" == publisher-core-parallel ]] || {
            echo "S parallel control requires MULTI_JETTY_PARALLEL_CONTROL=publisher-core-parallel" >&2; exit 1;
        }
        scope_schedule=S-parallel-3barriers
    elif [[ "$scope" == publish-core ]]; then
        [[ "${MULTI_JETTY_PARALLEL_CONTROL:-}" == publisher-core ]] || {
            echo "Publish core profile requires MULTI_JETTY_PARALLEL_CONTROL=publisher-core" >&2; exit 1;
        }
        scope_schedule=S
    elif [[ "$scope" == parallel-publish ]]; then
        [[ "${MULTI_JETTY_PARALLEL_CONTROL:-}" == publisher-parallel ]] || {
            echo "Parallel publish profile requires MULTI_JETTY_PARALLEL_CONTROL=publisher-parallel" >&2; exit 1;
        }
        scope_schedule=R
    elif [[ "$scope" == sq-prepare ]]; then
        [[ "${MULTI_JETTY_PARALLEL_CONTROL:-}" == publisher-sq-prepare ]] || {
            echo "SQ prepare profile requires MULTI_JETTY_PARALLEL_CONTROL=publisher-sq-prepare" >&2; exit 1;
        }
        scope_schedule=Q
    elif [[ "$scope" == publisher ]]; then
        [[ "${MULTI_JETTY_PARALLEL_CONTROL:-}" == publisher-commit ]] || {
            echo "Publisher profile requires MULTI_JETTY_PARALLEL_CONTROL=publisher-commit" >&2; exit 1;
        }
        scope_schedule=P
    elif [[ "${MULTI_JETTY_PARALLEL_CONTROL:-}" == serial-commit ]]; then
        scope_schedule=A
    elif [[ -n "${MULTI_JETTY_PARALLEL_CONTROL:-}" ]]; then
        echo "Control profile accepts only serial-commit (A) or empty control (N)" >&2; exit 1
    fi
    [[ -f "${script_dir}/build/${build_profile}/scope-ready.sha256" ]] &&
        sha256sum --check --status "${script_dir}/build/${build_profile}/scope-ready.sha256" || {
            echo "Scope binary has no matching resource-approved build; do not run" >&2; exit 1;
        }
fi
if [[ "$backend" == simt && "$mode" == simd-serial ]] ||
   [[ "$backend" == simd && ( "$mode" == simt-* || "$include_simt_serial" == true ) ]]; then
    echo "requested mode is not available in ${build_profile}" >&2; exit 1
fi
if [[ "$fast_build" == 1 ]]; then
    [[ "$mode" != simt-serial && "$include_simt_serial" == false ]] || {
        echo "fast profiles do not contain SIMT-Serial" >&2; exit 1;
    }
    if [[ "$batch_sizes_set" == false ]]; then batch_sizes="32,256"; fi
fi
IFS=',' read -r -a batches <<< "$batch_sizes"
for batch in "${batches[@]}"; do
    [[ "$batch" == "1" || "$batch" == "8" || "$batch" == "32" || "$batch" == "64" ||
       "$batch" == "128" || "$batch" == "256" ]] && ((iterations % batch == 0)) || {
        echo "batch sizes must be selected from 1,8,32,64,128,256 and divide iterations" >&2
        exit 1
    }
    if [[ "$fast_build" == 1 && "$mode" != simd-serial && "$batch" != 32 && "$batch" != 256 ]]; then
        echo "fast SIMT profiles support only batch sizes 32,256" >&2; exit 1
    fi
done
IFS=',' read -r -a jettys <<< "$jetty_counts"
for count in "${jettys[@]}"; do
    [[ "$count" == "1" || "$count" == "2" || "$count" == "4" || "$count" == "8" ]] || {
        echo "Jetty counts must be selected from 1,2,4,8" >&2
        exit 1
    }
done

mkdir -p "${script_dir}/results"
if [[ -z "$log_file" ]]; then
    log_file="${script_dir}/results/${mode}_$(date +%Y%m%d_%H%M%S).log"
fi
mkdir -p "$(dirname "$log_file")"
exec > >(tee "$log_file") 2>&1
source_head="$(git -C "$script_dir" rev-parse HEAD 2>/dev/null || echo UNKNOWN)"
source_tree=clean
[[ -z "$(git -C "$script_dir" status --porcelain 2>/dev/null)" ]] || source_tree=dirty
echo "RUN_IDENTITY | Head=${source_head} | Tree=${source_tree}"
echo "$build_info"
if [[ "$scope" != none ]]; then
    if [[ "$scope" == matrix ]]; then
        echo "SCOPE_CASE | Scope=matrix | Schedule=${scope_schedule} | BatchSize=${matrix_batch} | Profiling=0 | Trace=off | AclConfig=default"
    else
        echo "SCOPE_CASE | Scope=${scope} | Schedule=${scope_schedule} | VF=256,1024 | Trace=off | AclConfig=default"
    fi
fi
echo "RUN_BINARY | Profile=${build_profile:-legacy} | Path=${binary}"
sha256sum "$binary"

modes=(simd-serial simt-parallel)
if [[ "$mode" != "all" ]]; then
    modes=("$mode")
elif [[ "$include_simt_serial" == "true" ]]; then
    modes=(simd-serial simt-serial simt-parallel)
fi
if [[ "$mode" == all && "$backend" == simt ]]; then
    modes=(simt-parallel)
    [[ "$include_simt_serial" != true ]] || modes=(simt-serial simt-parallel)
elif [[ "$mode" == all && "$backend" == simd ]]; then
    modes=(simd-serial)
fi
apis=(write read notify)
[[ "$api" == "all" ]] || apis=("$api")
active_dir=""
active_pids=()
rank_status=()
cleanup() {
    if [[ ${#active_pids[@]} -ne 0 ]]; then kill "${active_pids[@]}" 2>/dev/null || true; fi
    if [[ -n "$active_dir" ]]; then rm -rf -- "$active_dir"; fi
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

trace_field() {
    local line="$1" key="$2" value
    value="${line#*| ${key}=}"
    if [[ "$value" == "$line" ]]; then
        printf 'NA'
    else
        printf '%s' "${value%% |*}"
    fi
}

run_case() {
    local case_mode="$1" case_api="$2" batch="$3" jetty_count="$4"
    local case_warmup work_dir root_file rank status pair_status perf_line lanes batches_per_jetty
    local failure_trace failure_suffix failure_kind
    case_warmup=$(( (warmup + batch - 1) / batch * batch ))
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/multi_jetty_batch_perf.XXXXXX")"
    active_dir="$work_dir"
    root_file="${work_dir}/root_info.bin"
    export MULTI_JETTY_BATCH_PERF_SYNC_DIR="$work_dir"
    active_pids=()
    rank_status=()
    for ((rank = 0; rank < devices; ++rank)); do
        timeout --kill-after=5s --signal=TERM "${case_timeout_seconds}s" \
            "$binary" "$rank" "$devices" "$root_file" "$case_mode" "$case_api" "$iterations" \
            "$case_warmup" "$payload_bytes" "$batch" "$jetty_count" \
            >"${work_dir}/rank_${rank}.log" 2>&1 &
        active_pids+=("$!")
    done
    status=0
    for ((rank = 0; rank < devices; ++rank)); do
        if wait "${active_pids[$rank]}"; then
            rank_status[$rank]=0
        else
            rank_status[$rank]=1
            status=1
        fi
    done
    active_pids=()
    lanes=1
    [[ "$case_mode" == "simd-serial" ]] || lanes="$batch"
    batches_per_jetty=$((iterations / batch))
    for ((rank = 0; rank < devices; rank += 2)); do
        if [[ -n "${MULTI_JETTY_BATCH_TRACE:-}" ]]; then
            if [[ "${MULTI_JETTY_BATCH_TRACE}" == "expert" ]]; then
                grep -E '^(DRAIN_TRACE|SQ_TRACE|SQ_AUDIT|MEMORY_ALIAS|MEMORY_GUARD)' \
                    "${work_dir}/rank_${rank}.log" || true
            else
                grep -E '^(BATCH_TRACE|DRAIN_TRACE|SQ_TRACE|SQ_AUDIT|MEMORY_ALIAS|MEMORY_GUARD)' \
                    "${work_dir}/rank_${rank}.log" || true
            fi
        fi
        failure_audit=""
        failure_completion=""
        if [[ "$scope" == publish-audit || "$scope" == matrix ]]; then
            grep -E '^(COMPLETION_STATE|COMPLETION_ERROR|COMPLETION_CHECK|COMPLETION_PROFILE|NOTIFY_AUDIT_SUMMARY|PUBLISH_AUDIT|CQ_AUDIT|CQ_RESOURCE_PAIR|CQ_SOURCE|DATA_AUDIT|DATA_WORDS)' \
                "${work_dir}/rank_${rank}.log" "${work_dir}/rank_$((rank + 1)).log" || true
            failure_completion="$(grep -m1 '^COMPLETION_ERROR |' "${work_dir}/rank_${rank}.log" || true)"
            failure_audit="$(grep -m1 -E '^CQ_AUDIT .* \| (Status|SubStatus)=[1-9][0-9]*( \||$)' \
                "${work_dir}/rank_${rank}.log" || true)"
        fi
        pair_status=PASS
        ((rank_status[rank] == 0 && rank_status[rank + 1] == 0)) || pair_status=FAIL
        failure_suffix=""
        if [[ "$pair_status" == "FAIL" ]]; then
            failure_trace="$(grep -m1 -E '^DRAIN_TRACE .* \| Status=-1( \||$)' \
                "${work_dir}/rank_${rank}.log" || true)"
            if [[ -n "$failure_completion" ]]; then
                failure_suffix=" | FailureKind=Completion | CompletionFault=$(trace_field "$failure_completion" Kind)"
                failure_suffix+=" | FailureJetty=$(trace_field "$failure_completion" Jetty)"
                failure_suffix+=" | FailurePhase=$(trace_field "$failure_completion" Phase)"
                failure_suffix+=" | CqSequence=$(trace_field "$failure_completion" Sequence)"
                failure_suffix+=" | CqLocalNum=$(trace_field "$failure_completion" LocalNum)"
                failure_suffix+=" | CqeStatus=$(trace_field "$failure_completion" Status)"
                failure_suffix+=" | CqeSubStatus=$(trace_field "$failure_completion" SubStatus)"
            elif [[ -n "$failure_audit" ]]; then
                failure_suffix=" | FailureKind=CqeSnapshot"
                failure_suffix+=" | CqResourceJetty=$(trace_field "$failure_audit" ResourceJetty)"
                failure_suffix+=" | CqSequence=$(trace_field "$failure_audit" Sequence)"
                failure_suffix+=" | CqLocalNum=$(trace_field "$failure_audit" LocalNum)"
                failure_suffix+=" | CqMatchedJetty=$(trace_field "$failure_audit" MatchedJetty)"
                failure_suffix+=" | CqeStatus=$(trace_field "$failure_audit" Status)"
                failure_suffix+=" | CqeSubStatus=$(trace_field "$failure_audit" SubStatus)"
            elif [[ -n "$failure_trace" ]]; then
                failure_suffix=" | FailureKind=CQE"
                failure_suffix+=" | FailureJetty=$(trace_field "$failure_trace" Jetty)"
                failure_suffix+=" | FailurePhase=$(trace_field "$failure_trace" Phase)"
                failure_suffix+=" | ExpectedCqTail=$(trace_field "$failure_trace" ExpectedCqTail)"
                failure_suffix+=" | CqTail=$(trace_field "$failure_trace" CqTail)"
                failure_suffix+=" | SqTail=$(trace_field "$failure_trace" SqTail)"
                failure_suffix+=" | CqeStatus=$(trace_field "$failure_trace" CqeStatus)"
                failure_suffix+=" | CqeSubStatus=$(trace_field "$failure_trace" CqeSubStatus)"
                failure_suffix+=" | PollReason=$(trace_field "$failure_trace" PollReason)"
            else
                failure_kind=ProcessOrTimeout
                if grep -q '^COMPLETION_CHECK | Status=FAIL' "${work_dir}/rank_${rank}.log"; then
                    failure_kind=CompletionValidation
                elif grep -q '^COMPLETION_PROFILE_CHECK | Status=FAIL' "${work_dir}/rank_${rank}.log"; then
                    failure_kind=CompletionProfileValidation
                elif grep -qE 'mismatch:' "${work_dir}/rank_${rank}.log" \
                    "${work_dir}/rank_$((rank + 1)).log"; then
                    failure_kind=RemoteValidation
                fi
                failure_suffix=" | FailureKind=${failure_kind} | CqeStatus=NA"
            fi
        fi
        perf_line="$(grep -m1 '^PERF_DATA |' "${work_dir}/rank_${rank}.log" || true)"
        if [[ -n "$perf_line" ]]; then
            echo "${perf_line/PERF_DATA/RESULT}${failure_suffix} | Status=${pair_status}"
        else
            echo "RESULT | API=${case_api} | DataSize/B=${payload_bytes} | Jettys=${jetty_count}" \
                 "| LanesPerJetty=${lanes} | BatchSize=${batch} | BatchesPerJetty=${batches_per_jetty}" \
                 "| TotalDBs=0 | Mode=${case_mode} | IssueTime/ns=0 | Bandwidth/GB/s=0${failure_suffix}" \
                 "| Status=FAIL"
        fi
        if [[ "$pair_status" == "FAIL" ]]; then
            if [[ "${MULTI_JETTY_BATCH_TRACE:-}" == "expert" ]]; then
                grep -E '^(DRAIN_TRACE|SQ_TRACE|SQ_AUDIT|MEMORY_ALIAS|MEMORY_GUARD)|mismatch:|failure|timeout' \
                    "${work_dir}/rank_${rank}.log" "${work_dir}/rank_$((rank + 1)).log" >&2 || true
            else
                cat "${work_dir}/rank_${rank}.log" "${work_dir}/rank_$((rank + 1)).log" >&2
            fi
        fi
    done
    rm -rf -- "$work_dir"
    active_dir=""
    return "$status"
}

overall=0
cases_run=0
cases_pass=0
cases_fail=0
for current_mode in "${modes[@]}"; do
    for current_api in "${apis[@]}"; do
        for batch in "${batches[@]}"; do
            for jetty_count in "${jettys[@]}"; do
                cases_run=$((cases_run + 1))
                if run_case "$current_mode" "$current_api" "$batch" "$jetty_count"; then
                    cases_pass=$((cases_pass + 1))
                else
                    cases_fail=$((cases_fail + 1))
                    overall=1
                fi
            done
        done
    done
done
if [[ "$scope" == matrix ]]; then
    echo "MATRIX_SUMMARY | BatchSize=${matrix_batch} | Cases=${cases_run} | Pass=${cases_pass} | Fail=${cases_fail} | Status=$([[ $overall == 0 ]] && echo PASS || echo FAIL)"
fi
echo "result log: $log_file" >&2
exit "$overall"
