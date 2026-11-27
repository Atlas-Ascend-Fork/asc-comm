#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""Render final selected benchmark records; this module does not parse profiling.

Every result already represents one explicitly selected measurement batch. No
sample averaging, batch selection, algorithm inference or timing conversion is
performed here. Host phase timestamps alone do not prove device association.
"""

import argparse
import collections
import csv
import json
import math
from pathlib import Path
import re
import sys

from profiling.external_algorithms import is_external_name as _external_algorithm

# Human-facing headers follow hccl_api_test; structured records retain their
# stable field names and metric keys for collection and profiling adapters.
COLUMN_LABELS = {
    "op": "算子类型",
    "engine": "执行引擎",
    "dtype": "数据类型",
    "requested_bytes": "数据量",
    "ranks": "卡数",
    "algorithm": "api算法",
    "status": "状态",
    "api_head_us": "api头开销(us)",
    "api_expand_us": "api展开耗时(us)",
    "api_comm_us": "api通信耗时(us)",
    "api_comm_gbps": "api通信带宽(GB/s)",
    "api_tail_us": "api尾开销(us)",
    "api_kernel_us": "api kernel耗时(us)",
    "api_kernel_skew_p95_us": "api kernel启动偏差p95(us)",
    "api_kernel_skew_max_us": "api kernel启动偏差max(us)",
    "api_framework_us": "api框架耗时(us)",
    "api_loopgroup_us": "api loopgroup耗时(us)",
    "api_algorithm_us": "api纯算法耗时(us)",
    "api_kfc_head_us": "api KFC head耗时(us)",
    "api_kfc_load_us": "api KFC 参数加载及判定耗时(us)",
    "api_kfc_signal_us": "api KFC 完成信号段耗时(us)",
    "api_kfc_tail_us": "api KFC 队列收尾耗时(us)",
    "api_probe_complete_samples": "api探针完整样本数(卡×轮)",
    "api_probe_expected_samples": "api探针预期样本数(卡×轮)",
    "api_probe_complete_pct": "api探针完整率(%)",
}

AICPU_METRICS = (
    "api_head_us",
    "api_expand_us",
    "api_comm_us",
    "api_comm_gbps",
    "api_tail_us",
    "api_kernel_us",
    "api_kernel_skew_p95_us",
    "api_kernel_skew_max_us",
)
CCU_METRICS = (
    "api_comm_us",
    "api_framework_us",
    "api_comm_gbps",
    "api_loopgroup_us",
    "api_algorithm_us",
    "api_kfc_head_us",
    "api_kfc_load_us",
    "api_kfc_signal_us",
    "api_kfc_tail_us",
    "api_kernel_skew_p95_us",
    "api_kernel_skew_max_us",
)
# Coverage stays in structured diagnostics, outside CSV and terminal columns.
CCU_AUDIT_METRICS = (
    "api_probe_complete_samples",
    "api_probe_expected_samples",
    "api_probe_complete_pct",
)
SUCCESS = {"hit", "fallback", "algorithm_unconfirmed"}
FAILURES = {
    "execution_failed",
    "validation_failed",
    "collection_failed",
    "parse_failed",
    "unfinished",
}
STATUSES = SUCCESS | FAILURES | {"unsupported", "pending"}
KINDS = {"supported", "fallback_attempt", "unsupported"}
WRITE_SHAPE_FIELDS = {
    "requested_bytes",
    "input_count",
    "output_count",
    "input_bytes",
    "output_bytes",
    "bandwidth_bytes",
    "per_rank_count",
    "per_peer_count",
}
WRITE_LAYOUT_FIELDS = {
    "pattern",
    "array_layout",
    "mission_count",
    "wire_dtype",
    "per_peer_bytes",
    "send_offsets_bytes",
    "send_sizes_bytes",
    "remote_window_offsets_bytes",
    "local_data_size_bytes",
    "window_bytes",
}
WRITE_DTYPE_BYTES = {
    **dict.fromkeys(("int8", "uint8", "hif8", "fp8e4m3", "fp8e5m2", "fp8e8m0"), 1),
    **dict.fromkeys(("int16", "uint16", "fp16", "bfp16", "bf16"), 2),
    **dict.fromkeys(("int32", "uint32", "fp32"), 4),
    **dict.fromkeys(("int64", "uint64", "fp64"), 8),
}


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _uint(value, field):
    _require(type(value) is int and 0 <= value <= 2**64 - 1, field + " must be uint64")
    return value


def _unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        _require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def validate_write_layout(record, options):
    """Validate the copied legacy tensor shape and 512-byte receive-slot strides."""
    shape = record.get("shape")
    _require(
        isinstance(shape, dict) and set(shape) == WRITE_SHAPE_FIELDS,
        "AlltoAllvWrite requires a complete actual shape",
    )
    for name, value in shape.items():
        _uint(value, "shape." + name)
    ranks = _uint(options.get("ranks"), "ranks")
    _require(ranks > 0, "AlltoAllvWrite ranks must be positive")
    width = WRITE_DTYPE_BYTES.get(options.get("dtype"))
    _require(width is not None, "AlltoAllvWrite payload dtype is unsupported")
    requested = shape["requested_bytes"]
    _require(requested > 0, "AlltoAllvWrite request must be positive")
    adjusted = max(requested, ranks)
    alignment = ranks * 512 if adjusted > 16 * 1024 * 1024 else ranks
    adjusted = adjusted // alignment * alignment
    count = max(1, (adjusted + width - 1) // width)
    alignment = ranks * 128 if count >= ranks * 128 else ranks
    count = (count + alignment - 1) // alignment * alignment
    peer_bytes = count // ranks * width
    stride = (peer_bytes + 511) // 512 * 512
    total_bytes = ranks * peer_bytes
    _require(
        shape["input_bytes"]
        == shape["output_bytes"]
        == shape["bandwidth_bytes"]
        == total_bytes
        and shape["input_count"] == shape["output_count"] == total_bytes // width
        and shape["input_count"] <= 2**63 - 1
        and shape["per_peer_count"] == peer_bytes // width
        and shape["per_rank_count"] == 0,
        "AlltoAllvWrite balanced input/window shape mismatch",
    )
    layout = record.get("write_layout")
    _require(
        isinstance(layout, dict) and set(layout) == WRITE_LAYOUT_FIELDS,
        "AlltoAllvWrite requires its complete byte write_layout",
    )
    _require(
        layout["pattern"] == "balanced"
        and layout["array_layout"] == "mission_major"
        and type(layout["mission_count"]) is int
        and layout["mission_count"] == 2
        and layout["wire_dtype"] == options["dtype"],
        "AlltoAllvWrite requires legacy balanced mission-major payload semantics",
    )
    for name, expected in (
        ("per_peer_bytes", peer_bytes),
        ("local_data_size_bytes", peer_bytes),
        ("window_bytes", ranks * stride),
    ):
        _require(
            _uint(layout[name], "write_layout." + name) == expected,
            "AlltoAllvWrite write_layout " + name + " mismatch",
        )
    for name in ("send_offsets_bytes", "send_sizes_bytes"):
        values = layout[name]
        _require(
            isinstance(values, list) and len(values) == 2 * ranks,
            "AlltoAllvWrite requires two mission-major rank arrays",
        )
        for index, value in enumerate(values):
            _uint(value, "write_layout." + name)
            mission, destination = divmod(index, ranks)
            expected = (
                destination * peer_bytes + mission * (peer_bytes // 2)
                if name == "send_offsets_bytes"
                else (peer_bytes // 2 if mission == 0 else peer_bytes - peer_bytes // 2)
            )
            _require(
                value == expected, "AlltoAllvWrite mission-major send byte mismatch"
            )
    offsets = layout["remote_window_offsets_bytes"]
    _require(
        isinstance(offsets, list) and len(offsets) == ranks,
        "AlltoAllvWrite requires one remote window offset per source rank",
    )
    for rank, value in enumerate(offsets):
        _require(
            _uint(value, "remote_window_offsets_bytes") == rank * stride,
            "AlltoAllvWrite remote window placement mismatch",
        )
    return layout


def validate_write_plan(plan, layout, rank):
    """Bind one rank's actual byte parameters to the selected result layout."""
    fields = {
        "global_rank",
        "send_offsets_bytes",
        "send_sizes_bytes",
        "remote_window_offset_bytes",
        "local_data_size_bytes",
        "window_bytes",
    }
    _require(
        isinstance(plan, dict) and set(plan) == fields,
        "AlltoAllvWrite index requires a complete per-rank write_plan",
    )
    _require(
        _uint(plan["global_rank"], "write_plan.global_rank") == rank,
        "AlltoAllvWrite write_plan belongs to another rank",
    )
    for name in ("send_offsets_bytes", "send_sizes_bytes"):
        _require(
            isinstance(plan[name], list),
            "AlltoAllvWrite write_plan arrays must be lists",
        )
        for value in plan[name]:
            _uint(value, "write_plan." + name)
        _require(
            plan[name] == layout[name],
            "AlltoAllvWrite write_plan send bytes differ from layout",
        )
    for name, expected in (
        ("remote_window_offset_bytes", layout["remote_window_offsets_bytes"][rank]),
        ("local_data_size_bytes", layout["local_data_size_bytes"]),
        ("window_bytes", layout["window_bytes"]),
    ):
        _require(
            _uint(plan[name], "write_plan." + name) == expected,
            "AlltoAllvWrite write_plan " + name + " differs from layout",
        )


def actual_algorithm_name(record):
    """Public name of the observed executor; never substitute the request."""
    observation = record.get("algorithm_observation", {})
    if record.get("status") not in {"hit", "fallback"} and not observation.get(
        "confirmed"
    ):
        return ""
    actual = record.get("actual_algorithm", "")
    if _external_algorithm(actual):
        return actual
    name = observation.get("external_name", "")
    return (
        name
        if observation.get("confirmed") is True and _external_algorithm(name)
        else ""
    )


def _entry(value, schema_version=1):
    _require(isinstance(value, dict), "entry must be an object")
    _uint(value.get("id"), "entry.id")
    _require(
        _uint(value.get("requested_bytes"), "entry.requested_bytes") > 0,
        "requested_bytes must be positive",
    )
    _require(
        isinstance(value.get("algorithm"), str), "entry.algorithm must be a string"
    )
    _require(value.get("kind") in KINDS, "unknown plan kind")
    if schema_version == 1:
        _require(
            bool(value["algorithm"]) != (value["kind"] == "unsupported"),
            "unsupported entry must have no algorithm; executable entry requires one",
        )
    else:
        _require(
            value["kind"] != "fallback_attempt", "schema 2 forbids fallback_attempt"
        )
        _require(
            value["kind"] == "unsupported" or bool(value["algorithm"]),
            "executable entry requires an algorithm",
        )
    _require(
        not value["algorithm"] or _external_algorithm(value["algorithm"]),
        "plan algorithm must be an external [] name",
    )
    return value


def _query_requests(job, options, lengths, requested_algorithms):
    """Check v2 query intent independently of the measurement matrix."""
    for field in ("begin_set", "end_set"):
        _require(type(options.get(field)) is bool, field + " must be boolean")
    begin = _uint(options.get("begin"), "begin")
    end = _uint(options.get("end"), "end")
    _require(0 < begin <= end and lengths[0] == begin, "invalid measurement bounds")
    _require(
        (options["begin_set"] or begin == 32768)
        and (options["end_set"] or end == 268435456),
        "unset measurement bounds must retain their CLI defaults",
    )
    explicit_size = options["begin_set"] or options["end_set"]
    mode = (
        "topology_all"
        if requested_algorithms or not explicit_size
        else "single_point"
        if begin == end
        else "range_union"
    )
    _require(
        job.get("query_mode") == mode,
        "query_mode contradicts CLI size/algorithm intent",
    )
    if mode == "single_point":
        _require(lengths == [begin], "single-point job must measure only that point")
    expected = [0] if mode == "topology_all" else lengths
    requests = job.get("query_requests")
    _require(
        isinstance(requests, list) and len(requests) == len(expected),
        "query_requests do not cover the declared query mode",
    )
    for index, (request, length) in enumerate(zip(requests, expected)):
        _require(
            isinstance(request, dict)
            and set(request) == {"id", "requested_bytes", "data_len"},
            "query request requires id, requested_bytes and data_len",
        )
        _require(
            _uint(request["id"], "query request id") == index,
            "query request ids must be sequential",
        )
        _require(
            _uint(request["requested_bytes"], "query request requested_bytes") == length
            and _uint(request["data_len"], "query request data_len") == length,
            "query request must pass raw requested bytes (or explicit topology zero)",
        )
    return mode, requests


def _plan_coverage(
    schema_version,
    lengths,
    requested_algorithms,
    queries,
    plans,
    combinations,
    complete,
    names_unresolved,
):
    """Validate support before execution; v1 retains its historical point policy."""
    expected_pairs = {}
    candidates = []
    for query in queries.values():
        for algorithm in query["algorithms"]:
            if _external_algorithm(algorithm) and algorithm not in candidates:
                candidates.append(algorithm)
    if schema_version == 2:
        if complete and not names_unresolved:
            selected = requested_algorithms or candidates
            expected_pairs = {
                (length, algorithm): "supported"
                if algorithm in candidates
                else "unsupported"
                for length in lengths
                for algorithm in selected
            }
        if plans:
            _require(complete, "plan requires all declared queries to succeed")
            _require(
                not names_unresolved,
                "query algorithm name contract is unresolved; no plan may use its union",
            )
            for entry in plans.values():
                pair = entry["requested_bytes"], entry["algorithm"]
                expected = expected_pairs.get(pair)
                if not candidates and not requested_algorithms and not pair[1]:
                    expected = "unsupported"
                _require(
                    entry["kind"] == expected,
                    "plan contradicts candidate union support",
                )
    else:
        if plans:
            _require(complete, "executable plan requires all point queries to succeed")
            for entry in plans.values():
                _require(
                    entry["requested_bytes"] not in names_unresolved,
                    "query algorithm name contract is unresolved; no plan may use that point",
                )
                returned = queries[entry["requested_bytes"]]["algorithms"]
                expected = (
                    "unsupported"
                    if not returned
                    else "supported"
                    if entry["algorithm"] in returned
                    else "fallback_attempt"
                )
                _require(
                    entry["kind"] == expected, "plan contradicts point query support"
                )
        if complete:
            for length in lengths:
                if length in names_unresolved:
                    continue
                returned = queries[length]["algorithms"]
                if returned:
                    for algorithm in requested_algorithms or returned:
                        expected_pairs[(length, algorithm)] = (
                            "supported" if algorithm in returned else "fallback_attempt"
                        )
            _require(
                {pair for pair in combinations if pair[1]} <= set(expected_pairs),
                "plan includes an unrequested algorithm",
            )
    return expected_pairs, candidates


def _validate_result(record, entry, metric_names, ranks):
    _require(record.get("entry") == entry, "result entry differs from its plan")
    status = record.get("status")
    _require(status in STATUSES, "unknown result status")
    _uint(record.get("batch"), "result.batch")
    _require(type(record.get("ret")) is int, "result.ret must be an integer")
    _require(
        isinstance(record.get("actual_algorithm"), str),
        "actual_algorithm must be a string",
    )
    _require(isinstance(record.get("evidence"), str), "evidence must be a string")
    observation = record.get("algorithm_observation", {})
    _require(isinstance(observation, dict), "algorithm_observation must be an object")
    observed = observation.get("confirmed") is True
    if observed:
        by_rank = observation.get("ranks", {})
        _require(
            _external_algorithm(record["actual_algorithm"])
            or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", record["actual_algorithm"]),
            "observed algorithm must be an SDK executor identifier or external name",
        )
        _require(
            record["actual_algorithm"]
            and record["evidence"]
            and observation.get("executor") == record["actual_algorithm"]
            and isinstance(by_rank, dict)
            and set(by_rank) == {str(rank) for rank in range(ranks)}
            and all(
                isinstance(value, dict)
                and value.get("executor") == record["actual_algorithm"]
                for value in by_rank.values()
            ),
            "observed algorithm requires consistent selected-executor evidence from every rank",
        )
        requested_executor = observation.get("requested_executor")
        external_name = observation.get("external_name", "")
        _require(
            isinstance(external_name, str)
            and (
                not external_name
                or (
                    _external_algorithm(external_name)
                    and all(
                        value.get("external_name") == external_name
                        for value in by_rank.values()
                    )
                )
            ),
            "public algorithm mapping must agree on every rank",
        )
        if requested_executor is not None:
            _require(
                isinstance(requested_executor, str)
                and requested_executor
                and all(
                    value.get("requested_executor") == requested_executor
                    for value in by_rank.values()
                ),
                "requested executor mapping must agree on every rank",
            )
    if record["actual_algorithm"] and not _external_algorithm(
        record["actual_algorithm"]
    ):
        _require(
            status == "algorithm_unconfirmed" or observed,
            "internal actual algorithm confirmation requires selected-executor observations",
        )
    if status in {"hit", "fallback"}:
        _require(
            record["actual_algorithm"] and record["evidence"] and record["ret"] == 0,
            "confirmed result requires actual algorithm, evidence and success",
        )
        requested_executor = observation.get("requested_executor") if observed else None
        _require(
            (record["actual_algorithm"] == (requested_executor or entry["algorithm"]))
            == (status == "hit"),
            "hit/fallback does not match actual algorithm",
        )
    if entry["kind"] == "unsupported":
        _require(
            status == "unsupported", "unsupported plan entry must remain unsupported"
        )
    metrics = record.get("metrics")
    _require(isinstance(metrics, dict), "metrics must be an object")
    _require(set(metrics) <= set(metric_names), "metric not applicable to this engine")
    _require(
        not metrics or (status in SUCCESS and record["ret"] == 0),
        "failed/unexecuted result cannot have valid performance values",
    )
    for name, value in metrics.items():
        _require(
            type(value) in {int, float} and math.isfinite(value) and value >= 0,
            "metric must be finite and nonnegative: " + name,
        )
    shape = record.get("shape", {})
    _require(isinstance(shape, dict), "shape must be an object")
    if shape.get("requested_bytes", 0):
        _require(
            shape["requested_bytes"] == entry["requested_bytes"],
            "shape length mismatch",
        )
    for name, value in shape.items():
        _uint(value, "shape." + name)


def read_records(path):
    records = []
    with Path(path).open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line, object_pairs_hook=_unique_pairs)
                _require(isinstance(value, dict), "record must be an object")
                records.append(value)
            except (ValueError, TypeError) as exc:
                raise ValueError(f"line {line_number}: {exc}") from exc
    return records


def build_report(records):
    _require(
        records and records[0].get("type") == "job", "first record must be a job header"
    )
    job = records[0]
    schema_version = job.get("schema_version")
    _require(
        type(schema_version) is int and schema_version in {1, 2},
        "unsupported schema_version",
    )
    job_id = job.get("job_id")
    _require(isinstance(job_id, str) and job_id, "job_id must not be empty")
    options = job.get("options", {})
    _require(
        options.get("engine") in {"aicpu_ts", "ccu_sched", "ccu_ms"}, "invalid engine"
    )
    _require(
        options.get("op")
        in {
            "allgather",
            "allreduce",
            "reducescatter",
            "alltoall",
            "alltoallv",
            "alltoallvwrite",
        },
        "invalid operator",
    )
    _require(
        options.get("op") != "alltoallvwrite"
        or options.get("engine") in {"ccu_sched", "ccu_ms"},
        "AlltoAllvWrite requires a CCU engine",
    )
    _require(
        isinstance(options.get("dtype"), str) and options["dtype"], "missing dtype"
    )
    _require(_uint(options.get("ranks"), "ranks") > 0, "ranks must be positive")
    lengths = job.get("lengths")
    _require(isinstance(lengths, list) and lengths, "job lengths must not be empty")
    for length in lengths:
        _require(_uint(length, "length") > 0, "length must be positive")
    _require(lengths == sorted(set(lengths)), "lengths must be unique and increasing")
    requested_algorithms = options.get("algorithms", [])
    _require(
        isinstance(requested_algorithms, list)
        and all(_external_algorithm(a) for a in requested_algorithms),
        "requested algorithms must be external [] names",
    )
    _require(
        len(set(requested_algorithms)) == len(requested_algorithms),
        "duplicate requested algorithm",
    )
    query_mode, query_requests = (
        _query_requests(job, options, lengths, requested_algorithms)
        if schema_version == 2
        else ("legacy_per_point", [])
    )
    expected_queries = (
        {request["requested_bytes"]: request for request in query_requests}
        if schema_version == 2
        else dict.fromkeys(lengths)
    )
    metric_names = AICPU_METRICS if options["engine"] == "aicpu_ts" else CCU_METRICS
    metadata = job.get("metadata", {})
    _require(isinstance(metadata, dict), "job metadata must be an object")
    measurement_mode = metadata.get("measurement_mode", "unknown")
    _require(
        measurement_mode in {"unknown", "full", "comm_only"},
        "invalid measurement_mode",
    )
    _require(
        measurement_mode != "comm_only" or options["engine"] == "aicpu_ts",
        "comm_only measurement mode requires the AICPU engine",
    )
    native_ccu_event = (
        options["engine"] != "aicpu_ts"
        and metadata.get("timing_source") in {"acl_event", "profiling"}
        and metadata.get("execution_source") == "native_acl_aclnn"
        and metadata.get("timing_scope")
        in {
            "custom_op_event_rank0_mean",
            "custom_op_event_max_of_rank_means",  # Previously recorded runs.
        }
    )
    allowed_metrics = metric_names + (
        CCU_AUDIT_METRICS if options["engine"] != "aicpu_ts" else ()
    )
    if native_ccu_event:
        # Keep the whole-operator Event in structured diagnostics, outside
        # the CCU CSV columns. Event-only captures still accept only this metric.
        allowed_metrics = (
            ("api_kernel_us",)
            if metadata.get("timing_source") == "acl_event"
            else allowed_metrics + ("api_kernel_us",)
        )
    queries, plans, results, phases, failures = {}, {}, {}, [], []
    comm_engine = {"aicpu_ts": 2, "ccu_sched": 6, "ccu_ms": 5}[options["engine"]]
    cc_types = set()
    combinations = set()
    for record in records:
        _require(
            type(record.get("schema_version")) is int
            and record["schema_version"] == schema_version,
            "cannot mix schema_version values",
        )
        _require(record.get("job_id") == job_id, "cannot mix jobs or scenarios")
        kind = record.get("type")
        if kind == "job":
            _require(record is job, "duplicate job header; scenarios cannot be merged")
        elif kind == "query":
            length = _uint(record.get("requested_bytes"), "query.requested_bytes")
            _require(
                length in expected_queries and length not in queries,
                "duplicate or unexpected query length",
            )
            if schema_version == 2:
                request = expected_queries[length]
                _require(
                    _uint(record.get("query_id"), "query.query_id") == request["id"]
                    and record["query_id"] == len(queries)
                    and record.get("query_mode") == query_mode,
                    "query identity or mode differs from its declared request",
                )
            cc_type = _uint(record.get("cc_type"), "query.cc_type")
            _require(cc_type <= 255, "query.cc_type must be uint8")
            cc_types.add(cc_type)
            _require(len(cc_types) == 1, "query cc_type differs across the job")
            _require(
                _uint(record.get("comm_engine"), "query.comm_engine") == comm_engine,
                "query comm_engine differs from job engine",
            )
            _uint(record.get("ret"), "query.ret")
            _uint(record.get("release_ret"), "query.release_ret")
            _require(
                type(record.get("data_len_set")) is bool, "data_len_set must be boolean"
            )
            if record["data_len_set"]:
                _uint(record.get("data_len"), "query.data_len")
            else:
                _require(record.get("data_len") is None, "unset dataLen must be null")
            if schema_version == 2 and record["data_len_set"]:
                _require(
                    record["data_len"] == request["data_len"],
                    "query dataLen differs from its declared raw-byte request",
                )
            if (
                schema_version == 2
                and record["ret"] == 0
                and record["release_ret"] == 0
                and not record.get("error_stage")
            ):
                _require(
                    record["data_len_set"],
                    "successful schema 2 query requires explicit dataLen setter",
                )
            algorithms = record.get("algorithms")
            _require(
                isinstance(algorithms, list)
                and all(isinstance(a, str) and a for a in algorithms),
                "query algorithms must be nonempty strings",
            )
            _require(
                len(set(algorithms)) == len(algorithms), "duplicate query algorithm"
            )
            # A failing release may follow a successful query. Preserve its
            # returned names as raw diagnostic data; query_failed below prevents
            # any of these records from supplying an executable plan.
            # Keep the raw record intact. The distinction between an omitted
            # setter and an explicit zero matters for diagnostics, but both
            # mean the provider skips its length clauses.
            length_requested = record["data_len_set"] and record["data_len"] > 0
            queries[length] = dict(
                record,
                length_filter_requested=length_requested,
                length_filter_applied=bool(
                    length_requested
                    and record["ret"] == 0
                    and record["release_ret"] == 0
                    and not record.get("error_stage")
                ),
            )
        elif kind == "plan":
            entry = _entry(record.get("entry"), schema_version)
            pair = entry["requested_bytes"], entry["algorithm"]
            _require(
                entry["id"] not in plans and pair not in combinations,
                "duplicate plan entry",
            )
            _require(entry["requested_bytes"] in lengths, "plan length absent from job")
            plans[entry["id"]] = entry
            combinations.add(pair)
        elif kind == "result":
            entry = _entry(record.get("entry"), schema_version)
            _require(
                entry["id"] not in results,
                "duplicate final result; select one batch before reporting",
            )
            results[entry["id"]] = record
        elif kind == "failure":
            _require(
                isinstance(record.get("stage"), str) and record["stage"],
                "failure requires a stage",
            )
            _require(
                isinstance(record.get("error"), str) and record["error"],
                "failure requires an error",
            )
            _require(
                type(record.get("ret")) is int and -(2**63) <= record["ret"] < 2**63,
                "failure.ret must be int64",
            )
            failures.append(record)
        elif kind == "phase":
            _require(
                record.get("phase")
                in {"initialization", "query", "warmup", "measurement", "validation"},
                "invalid phase",
            )
            _require(record.get("event") in {"begin", "end"}, "invalid phase event")
            phases.append(record)
        else:
            raise ValueError("unknown record type: " + str(kind))

    query_failed = any(
        q["ret"] != 0 or q["release_ret"] != 0 or q.get("error_stage")
        for q in queries.values()
    )
    names_unresolved = {
        length
        for length, query in queries.items()
        if query["ret"] == 0
        and query["release_ret"] == 0
        and not query.get("error_stage")
        and any(not _external_algorithm(name) for name in query["algorithms"])
    }
    complete_queries = len(queries) == len(expected_queries) and not query_failed
    expected_pairs, candidates = _plan_coverage(
        schema_version,
        lengths,
        requested_algorithms,
        queries,
        plans,
        combinations,
        complete_queries,
        names_unresolved,
    )
    missing_pairs = set(expected_pairs) - combinations
    _require(set(results) <= set(plans), "result has no matching plan")
    for entry_id, record in results.items():
        _validate_result(record, plans[entry_id], allowed_metrics, options["ranks"])
        if schema_version == 2 and plans[entry_id]["kind"] == "unsupported":
            _require(
                not record["actual_algorithm"] and not record["evidence"],
                "unsupported schema 2 result must have no execution evidence",
            )
        if options["op"] == "alltoallvwrite" and (
            record["status"] in SUCCESS
            and record["ret"] == 0
            or "write_layout" in record
        ):
            validate_write_layout(record, options)

    diagnostics = []
    counts = collections.Counter()
    for entry in sorted(
        plans.values(), key=lambda e: (e["requested_bytes"], e["algorithm"], e["id"])
    ):
        record = results.get(entry["id"])
        if record is None:
            status = "unsupported" if entry["kind"] == "unsupported" else "unfinished"
            record = {
                "entry": entry,
                "status": status,
                "metrics": {},
                "error": "no final selected result record"
                if status == "unfinished"
                else "",
                "actual_algorithm": "",
                "evidence": "",
                "shape": {},
            }
        elif record["status"] == "pending":
            record = dict(
                record,
                status="unfinished",
                error=record.get("error") or "result remained pending",
            )
        counts[record["status"]] += 1
        diagnostics.append(dict(record))
    for length, algorithm in sorted(missing_pairs):
        diagnostics.append(
            {
                "entry": {
                    "requested_bytes": length,
                    "algorithm": algorithm,
                    "kind": expected_pairs[(length, algorithm)],
                },
                "status": "unfinished",
                "metrics": {},
                "actual_algorithm": "",
                "evidence": "",
                "shape": {},
                "error": "expected combination has no recorded plan entry",
            }
        )
        counts["unfinished"] += 1
    # An empty successful union describes every measurement point as unsupported.
    # Historical v1 records instead describe only the corresponding query point.
    represented = {entry["requested_bytes"] for entry in plans.values()}
    empty_points = (
        lengths
        if schema_version == 2
        and complete_queries
        and not candidates
        and not names_unresolved
        and not requested_algorithms
        else []
        if schema_version == 2
        else [
            length
            for length, query in sorted(queries.items())
            if not query["algorithms"]
            and query["ret"] == 0
            and query["release_ret"] == 0
            and not query.get("error_stage")
        ]
    )
    for length in empty_points:
        if length not in represented:
            record = {
                "entry": {
                    "requested_bytes": length,
                    "algorithm": "",
                    "kind": "unsupported",
                },
                "status": "unsupported",
                "metrics": {},
                "actual_algorithm": "",
                "evidence": "",
                "shape": {},
            }
            counts["unsupported"] += 1
            diagnostics.append(dict(record))
    diagnostics.sort(
        key=lambda r: (r["entry"]["requested_bytes"], r["entry"]["algorithm"])
    )
    missing_performance = []
    rows = []
    for record in diagnostics:
        status = record["status"]
        # Display the observed executor's public name; retain internal names
        # and the original request in diagnostics, outside presentation columns.
        algorithm = actual_algorithm_name(record)
        if status in SUCCESS and not record.get("metrics"):
            record["performance_status"] = "missing"
            missing_performance.append(record["entry"])
        else:
            record["performance_status"] = (
                "available" if record.get("metrics") else "not_measured"
            )
        missing = "PARSE_FAIL" if status == "parse_failed" else "-"
        metrics = record.get("metrics", {})
        # Match the delayed-Commit diagnostic presentation while preserving
        # numeric raw measurements in JSON. Missing/failed values stay missing.
        values = [
            "NA"
            if measurement_mode == "comm_only"
            and status in SUCCESS
            and name in {"api_expand_us", "api_kernel_us"}
            and name in metrics
            else metrics.get(name, missing)
            for name in metric_names
        ]
        rows.append(
            [
                options["op"],
                options["dtype"],
                record["entry"]["requested_bytes"],
                options["ranks"],
            ]
            + values
            + [algorithm]
        )
    incomplete_queries = len(queries) != len(expected_queries)
    missing_plan = bool(missing_pairs)
    failed = (
        bool(failures)
        or query_failed
        or bool(names_unresolved)
        or incomplete_queries
        or missing_plan
        or bool(missing_performance)
        or any(counts[status] for status in FAILURES)
    )
    summary = {
        "schema_version": schema_version,
        "job_id": job_id,
        "options": options,
        "lengths": lengths,
        "metadata": job.get("metadata", {}),
        "measurement_mode": measurement_mode,
        "planned": len(plans),
        "measured": sum(bool(r.get("metrics")) for r in diagnostics),
        "success": sum(
            r["status"] == "hit" and bool(r.get("metrics")) for r in diagnostics
        ),
        "fallback": counts["fallback"],
        "algorithm_unconfirmed": counts["algorithm_unconfirmed"],
        "observed_algorithms": sum(
            bool(record.get("actual_algorithm"))
            and (
                record["status"] in {"hit", "fallback"}
                or record.get("algorithm_observation", {}).get("confirmed") is True
            )
            for record in diagnostics
        ),
        "unconfirmed_algorithm_names": sum(
            record["status"] in SUCCESS and not actual_algorithm_name(record)
            for record in diagnostics
        ),
        "unsupported": counts["unsupported"],
        "failed": bool(failed),
        "counts": dict(counts),
        "query_count": len(queries),
        "query_failed": bool(query_failed),
        "queries": [
            queries[length] for length in expected_queries if length in queries
        ],
        "failures": failures,
        "incomplete_queries": incomplete_queries,
        "missing_plan": missing_plan,
        "missing_performance": missing_performance,
        "data_len_unset_points": [
            length for length, query in queries.items() if not query["data_len_set"]
        ],
        "length_filter_skipped_points": [
            length
            for length, query in queries.items()
            if not query["length_filter_requested"]
        ],
        "names_contract_unresolved_points": [
            length for length in expected_queries if length in names_unresolved
        ],
        "query_scope": ["operator", "engine", "topology", "optional_per_rank_bytes"],
        "execution_checks_not_covered": [
            "dtype",
            "reduce",
            "overlap",
            "order",
            "symmetric_memory",
            "resources",
        ],
        "phase_record_count": len(phases),
        "profiling_association_verified": False,
    }
    if schema_version == 2:
        summary.update(
            query_mode=query_mode,
            query_requests=query_requests,
            candidate_algorithms=candidates
            if complete_queries and not names_unresolved
            else [],
            requested_algorithms_supported=[
                name for name in requested_algorithms if name in candidates
            ]
            if complete_queries and not names_unresolved
            else [],
            requested_algorithms_unsupported=[
                name for name in requested_algorithms if name not in candidates
            ]
            if complete_queries and not names_unresolved
            else [],
            query_scope=["operator", "engine", "topology"]
            + ([] if query_mode == "topology_all" else ["raw_requested_bytes"]),
            query_policy={
                "candidate_aggregation": "union"
                if query_mode == "range_union"
                else "single_query",
                "measurement_matrix": "measurement_lengths_x_selected_candidates",
                "length_filter": "intentionally_disabled"
                if query_mode == "topology_all"
                else "raw_requested_bytes",
                "unsupported_algorithm": "skip",
                "fallback_owner": "query_provider",
            },
        )
    if options["op"] == "alltoallvwrite":
        summary["operator_contract"] = {
            "wire_dtype": options["dtype"],
            "offset_unit": "bytes",
            "output_buffer": "legacy_output_tensor",
            "bandwidth_scope": "per_rank_input_including_self",
            "pattern": "balanced",
            "mission_count": 2,
            "mission_count_scope": "parameter_array_layout",
        }
    column_keys = (
        ["op", "dtype", "requested_bytes", "ranks"] + list(metric_names) + ["algorithm"]
    )
    return (
        [COLUMN_LABELS[key] for key in column_keys],
        rows,
        summary,
        diagnostics,
    )


def write_report(records, output_dir):
    columns, rows, summary, diagnostics = build_report(records)
    output = Path(output_dir)
    output.mkdir(parents=True, exist_ok=True)
    # The BOM lets spreadsheet applications recognize the Chinese UTF-8 headers.
    with (output / "report.csv").open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.writer(stream)
        writer.writerow(columns)
        writer.writerows(rows)
    for filename, value in (
        ("summary.json", summary),
        ("diagnostics.json", diagnostics),
    ):
        with (output / filename).open("w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
    return summary


def query_policy_messages(summary):
    if summary.get("schema_version") != 2:
        return []
    unsupported = summary.get("requested_algorithms_unsupported", [])
    if unsupported:
        return ["查询未命中，已跳过算法：" + ", ".join(unsupported)]
    if (
        not summary.get("query_failed")
        and not summary.get("incomplete_queries")
        and not summary.get("names_contract_unresolved_points")
        and not summary.get("candidate_algorithms")
    ):
        return ["查询无候选，本轮不执行性能测试。"]
    return []


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("records", type=Path, help="one job's structured JSONL records")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        summary = write_report(read_records(args.records), args.output_dir)
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print("report failed: " + str(exc), file=sys.stderr)
        return 2
    for message in query_policy_messages(summary):
        print(message, file=sys.stderr)
    return 1 if summary["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
