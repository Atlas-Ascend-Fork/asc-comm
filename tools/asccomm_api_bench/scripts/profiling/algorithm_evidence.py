# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""Attribute legacy selected-executor logs to native run/rank/entry evidence."""

from bisect import bisect_right
import hashlib
import json
from pathlib import Path
import re

from . import api_core as api
from . import context_evidence as contexts
from . import external_algorithms as external
from . import legacy_algorithms as legacy

# Copied from hccl_api_test scripts/verify_algorithm_request.py at SOURCE_COMMIT.
ACCEPTED = re.compile(
    r"\[MC2_EXT_ALG\]\s+accepted,\s*opType\[(\d+)\],\s*"
    r"algConfig\[(.*?)\]\s*->\s*algName\[([^\]]+)\]"
)
SOURCES = (
    "mc2-tiling",
    "kfc",
    "base-selector",
    "graph-mode",
    "resource",
    "selector",
)
SUCCESS = {"hit", "fallback", "algorithm_unconfirmed"}


def _read(path, root, inputs):
    path.resolve().relative_to(root.resolve())
    data = path.read_bytes()
    inputs[str(path)] = hashlib.sha256(data).hexdigest()
    return data.decode("utf-8", errors="replace")


def _require(condition, reason):
    if not condition:
        raise ValueError(reason)


def _api_record(pid, number, line, path):
    # Same extraction as the legacy discover_algorithm_logs API branch.
    timestamp = legacy.parse_run_log_timestamp(line)
    match = legacy.API_ALGORITHM_LOG_RE.search(line)
    if timestamp is None or match is None:
        return None
    operator = api.canonical_operator(match.group("operator"))
    dtype = api.normalize_dtype(match.group("src_dtype"))
    if operator is None or dtype is None:
        return None
    return legacy.ApiAlgorithmLogRecord(
        pid,
        timestamp,
        number,
        int(match.group("rank")),
        match.group("group"),
        operator,
        match.group("algorithm"),
        dtype,
        path,
    )


def _rank_entries(rank_dir, rank, owner, job, inputs, plans=None):
    manifest = json.loads(_read(rank_dir / "manifest.json", rank_dir, inputs))
    status = json.loads(_read(rank_dir / "status.json", rank_dir, inputs))
    samples = json.loads(_read(rank_dir / "native_samples.json", rank_dir, inputs))
    pid = manifest.get("backend_pid")
    _require(type(pid) is int and pid > 0, "missing backend PID")
    for value in (manifest, status):
        _require(
            value.get("global_rank") == rank and value.get("run_id") == owner["run_id"],
            "rank artifacts belong to another run/rank",
        )
        _require(
            value.get("backend_pid") == pid,
            "rank artifacts have different backend PIDs",
        )
    _require(
        status.get("exit_code") == 0 and status.get("backend_returncode", 0) == 0,
        "rank backend did not complete successfully",
    )
    _require(
        manifest.get("world_size")
        == samples.get("world_size")
        == job["options"]["ranks"],
        "rank sample world size differs from job",
    )
    _require(
        samples.get("backend_pid") == pid
        and samples.get("rank") == rank
        and samples.get("engine") == job["options"]["engine"]
        and samples.get("execution_source") == "native_acl_aclnn",
        "native samples do not identify this backend/rank/engine",
    )
    entries = {}
    intervals = []
    for sample in samples["samples"]:
        key = (sample["entry_id"], sample["batch"])
        _require(
            key not in entries and sample.get("validated") is True,
            "duplicate or unvalidated entry",
        )
        scope = sample["algorithm_log_scope"]
        begin, end = (
            legacy.parse_run_log_timestamp(scope[name]) for name in ("begin", "end")
        )
        _require(
            begin is not None and end is not None and begin < end,
            "invalid entry log bounds",
        )
        _require(
            not intervals or begin > intervals[-1][1],
            "entry log intervals overlap or are reordered",
        )
        intervals.append((begin, end, key))
        entries[key] = {
            "sample": sample,
            "api": [],
            "ccu": [],
            "accepted": set(),
            "mappings": set(),
            "files": set(),
        }
    starts = [begin for begin, _end, _key in intervals]
    operator = api.canonical_operator(job["options"]["op"])
    op_types = {
        value.casefold(): key for key, value in legacy.CCU_OP_TYPE_TO_OPERATOR.items()
    }
    context_events, selections = [], []
    for path in sorted((rank_dir / "logs").rglob("plog-*.log")):
        match = legacy.PLOG_PID_RE.fullmatch(path.name)
        if match is None or int(match[1]) != pid:
            continue
        for number, line in enumerate(_read(path, rank_dir, inputs).splitlines(), 1):
            timestamp = legacy.parse_run_log_timestamp(line)
            if timestamp is None:
                continue
            ccu_record = None
            if job["options"]["engine"] != "aicpu_ts":
                context = contexts.context_event(
                    line, pid, number, path, operator, job["options"]["dtype"]
                )
                if context is not None:
                    context_events.append(context)
                ccu_record = legacy.ccu_algorithm_record_from_log_line(
                    pid, number, line, path
                )
                if (
                    ccu_record is not None
                    and ccu_record.source == "engine-ctx"
                    and context is not None
                    and not context["applicable"]
                ):
                    ccu_record = None
                if ccu_record is not None and (
                    ccu_record.operator is None
                    or ccu_record.operator.casefold() == operator.casefold()
                ):
                    selections.append(ccu_record)
                    if ccu_record.source == "engine-ctx" and context is None:
                        context_events.append(
                            {
                                "timestamp": timestamp,
                                "action": "unknown",
                                "file": str(path),
                                "line": number,
                            }
                        )
                else:
                    ccu_record = None
            # Ordered, disjoint closed intervals have at most one owner.
            index = bisect_right(starts, timestamp) - 1
            if index < 0 or timestamp > intervals[index][1]:
                continue
            entry = entries[intervals[index][2]]
            api_record = _api_record(pid, number, line, path)
            if (
                api_record is not None
                and api_record.operator.casefold() == operator.casefold()
            ):
                _require(
                    api_record.rank == rank, "algorithm log rank differs from backend"
                )
                _require(
                    api_record.src_dtype == job["options"]["dtype"],
                    "algorithm log dtype differs from job",
                )
                entry["api"].append(api_record)
                entry["files"].add(str(path))
            if ccu_record is not None:
                entry["ccu"].append(ccu_record)
                entry["files"].add(str(path))
            accepted = ACCEPTED.search(line)
            if accepted and int(accepted[1]) == op_types[operator.casefold()]:
                name = accepted[2].replace(" ", "").replace("\t", "")
                if external.is_external_name(name):
                    entry["accepted"].add((name, accepted[3]))
                    entry["mappings"].add((name, accepted[3]))
                    entry["files"].add(str(path))
            candidates = external.candidate_mappings(
                line, op_types[operator.casefold()], job["options"]["engine"]
            )
            if candidates:
                entry["mappings"].update(candidates)
                entry["files"].add(str(path))
    if job["options"]["engine"] != "aicpu_ts":
        contexts.bind_contexts(
            entries, intervals, plans or {}, context_events, selections
        )
    return entries, samples


def _executor(entry, samples, result, engine):
    _require(
        entry["sample"]["requested_bytes"] == result["entry"]["requested_bytes"],
        "log entry length differs from plan",
    )
    calls = samples["warmup"] + samples["iterations"]
    size = result["entry"]["requested_bytes"]
    for source in SOURCES if engine != "aicpu_ts" else ():
        records = sorted(
            (r for r in entry["ccu"] if r.source == source),
            key=lambda r: (r.timestamp, r.line_number, str(r.log_path)),
        )
        if records:
            inferred = legacy.infer_ordered_ccu_algorithms(records, [size], calls)
            return inferred.get(size), source
    # The legacy API per-size rule requires complete call coverage and one
    # group/executor. No request name or queried candidate supplies missing logs.
    records = entry["api"]
    algorithms = {record.algorithm for record in records}
    groups = {record.group for record in records}
    if (
        len(records) == calls
        and len(algorithms) == len(groups) == 1
        and "" not in groups
    ):
        return next(iter(algorithms)), "mc2-alg-info"
    if not records and engine != "aicpu_ts":
        context = entry.get("context_evidence", {})
        return context.get("executor"), context.get(
            "source", "missing_or_conflicting_selected_executor"
        )
    return None, "missing_or_conflicting_selected_executor"


def enrich(records, run_dir):
    """Return an audit; update only algorithm evidence, never performance values.

    PLOG discovery stays within the collected rank directories. Native entry
    bounds isolate call logs; context lifetimes may span entries in that same
    process. The old parser and all-rank resolver identify the actual executor. SDK log
    mappings or copied registration metadata supply its public display name.
    Missing execution or mapping evidence leaves the algorithm cell blank.
    """
    job = records[0]
    audit = {"source_commit": legacy.SOURCE_COMMIT, "inputs": {}, "entries": []}
    if job.get("metadata", {}).get("execution_source") != "native_acl_aclnn":
        return audit
    selected = [
        r
        for r in records
        if r.get("type") == "result"
        and r.get("status") in SUCCESS
        and r.get("ret") == 0
    ]
    if not selected:
        return audit
    run_dir = Path(run_dir)
    rank_data, errors = {}, {}
    try:
        owner_path = run_dir / "launch_manifest.json"
        if not owner_path.is_file():
            owner_path = run_dir / "run_owner.json"
        owner = json.loads(_read(owner_path, run_dir, audit["inputs"]))
        _require(
            isinstance(owner.get("run_id"), str) and owner["run_id"],
            "missing run identity",
        )
    except (OSError, ValueError, KeyError, TypeError) as error:
        owner = None
        errors["run"] = str(error)
    ranks = job["options"]["ranks"]
    plans = {
        record["entry"]["id"]: record["entry"]
        for record in records
        if record.get("type") == "plan"
    }
    for rank in range(ranks):
        if owner is None:
            continue
        rank_dir = run_dir / "collected/ranks" / ("rank_%06d" % rank)
        try:
            rank_data[rank] = _rank_entries(
                rank_dir, rank, owner, job, audit["inputs"], plans
            )
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors[str(rank)] = str(error)
    for result in selected:
        key = (result["entry"]["id"], result["batch"])
        by_rank, evidence, mappings = {}, {}, {}
        reasons = dict(errors)
        for rank, (entries, samples) in rank_data.items():
            try:
                entry = entries[key]
                algorithm, source = _executor(
                    entry, samples, result, job["options"]["engine"]
                )
                by_rank[rank] = algorithm
                external_name, name_source = external.resolve(
                    job["options"]["op"], algorithm, entry["mappings"]
                )
                evidence[str(rank)] = {
                    "executor": algorithm,
                    "external_name": external_name,
                    "external_name_source": name_source,
                    "source": source,
                    "files": sorted(entry["files"]),
                }
                if source.startswith("engine-ctx"):
                    evidence[str(rank)]["context_evidence"] = entry.get(
                        "context_evidence", {}
                    )
                requested = (
                    result["entry"]["algorithm"].replace(" ", "").replace("\t", "")
                )
                accepted = {
                    executor
                    for config, executor in entry["accepted"]
                    if config == requested
                }
                mappings[rank] = next(iter(accepted)) if len(accepted) == 1 else None
                evidence[str(rank)]["requested_executor"] = mappings[rank]
            except (ValueError, KeyError, TypeError) as error:
                reasons[str(rank)] = str(error)
        observation_key = (
            job["options"]["op"],
            job["options"]["dtype"],
            "api",
            result["entry"]["requested_bytes"],
            ranks,
        )
        actual = legacy.resolve_summary_algorithms(
            {observation_key: by_rank}, ranks
        ).get(observation_key)
        if actual and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", actual) is None:
            reasons["executor"] = "selected executor is not an SDK identifier"
            actual = None
        requested_executor = (
            next(iter(set(mappings.values())))
            if len(mappings) == ranks
            and None not in mappings.values()
            and len(set(mappings.values())) == 1
            else None
        )
        external_names = {value["external_name"] for value in evidence.values()}
        external_name = (
            next(iter(external_names))
            if actual and len(evidence) == ranks and len(external_names) == 1
            else ""
        )
        observation = {
            "confirmed": actual is not None,
            "executor": actual,
            "external_name": external_name,
            "requested_executor": requested_executor,
            "ranks": evidence,
            "errors": reasons,
            "source_commit": legacy.SOURCE_COMMIT,
        }
        result["algorithm_observation"] = observation
        result["actual_algorithm"] = actual or ""
        result["evidence"] = (
            "same-run selected-executor logs; see algorithm_observation"
            if actual
            else ""
        )
        result["status"] = "algorithm_unconfirmed"
        if actual and requested_executor:
            result["status"] = "hit" if actual == requested_executor else "fallback"
        elif actual and actual == result["entry"]["algorithm"]:
            result["status"] = "hit"
        audit["entries"].append({"entry_id": key[0], "batch": key[1], **observation})
    job.setdefault("metadata", {})["algorithm_evidence_source"] = legacy.SOURCE_COMMIT
    return audit
