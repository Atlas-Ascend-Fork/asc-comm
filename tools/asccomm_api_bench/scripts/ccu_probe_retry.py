# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""Recollect missing CCU probe entries through the existing native launcher.

Each attempt is a separate single-size, single-request MPI job. Selection uses
probe availability, never timing. Original captures/audits stay immutable; the
final report selects a complete result record and records its source job.
"""

import copy
import hashlib
import json
import math
from pathlib import Path

from report import SUCCESS, build_report, read_records, write_report
from profiling.ccu_core import API_PROBE_COLUMNS

MAX_ATTEMPTS = 3
PROBE_FIELDS = tuple(field for field, _label in API_PROBE_COLUMNS)


def _hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _write(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def _key(record):
    return record["entry"]["id"], record["batch"]


def _coverage(audit):
    return {
        (item["entry_id"], item["batch"]): item
        for item in audit.get("ccu_probes", {}).get("entries", [])
    }


def _has_probes(record, coverage):
    metrics = record.get("metrics", {})
    return (
        record.get("status") in SUCCESS
        and record.get("ret") == 0
        and coverage.get("complete_samples", 0) > 0
        and all(
            type(metrics.get(field)) in (int, float)
            and math.isfinite(metrics[field])
            and metrics[field] >= 0
            for field in PROBE_FIELDS
        )
    )


def single_entry_arguments(arguments, entry):
    """Keep runtime/query settings, replacing only size/step/algorithm options."""
    result, index = [], 0
    while index < len(arguments):
        argument = arguments[index]
        key, separator, _value = argument.partition("=")
        if key in {"-b", "-e", "-f", "-i", "--alg"}:
            index += 1 if separator else 2
        else:
            result.append(argument)
            index += 1
    return result + [
        "-b",
        str(entry["requested_bytes"]),
        "-e",
        str(entry["requested_bytes"]),
        "--alg",
        entry["algorithm"],
    ]


def _source(run_dir):
    records_path = run_dir / "profile_analysis/enriched_records.jsonl"
    audit_path = run_dir / "profile_analysis/profiling_audit.json"
    audit = _json(audit_path)
    digest = _hash(records_path)
    if digest != audit.get("enriched_records_sha256"):
        raise ValueError("CCU retry source differs from its profiling audit")
    records = read_records(records_path)
    build_report(records)
    return (
        records,
        audit,
        {
            "records": str(records_path),
            "records_sha256": digest,
            "audit": str(audit_path),
            "audit_sha256": _hash(audit_path),
        },
    )


def _replacement(parent_job, target, run_dir, parent_run_id):
    cluster = _json(run_dir / "cluster_summary.json")
    if cluster.get("status") != "PASS" or cluster.get("run_id") == parent_run_id:
        raise ValueError("single-size retry did not complete as a fresh successful job")
    records, audit, source = _source(run_dir)
    if audit.get("profiling_association_verified") is not True:
        raise ValueError("single-size retry has unverified profiling association")
    job = records[0]
    for field in ("op", "engine", "dtype", "reduce", "ranks", "devices_per_node"):
        if job["options"].get(field) != parent_job["options"].get(field):
            raise ValueError("single-size retry changed scenario field: " + field)
    for field in (
        "warmup",
        "iterations",
        "measurement_iterations",
        "measurement_mode",
        "commit_delay_nops",
        "execution_source",
        "timing_source",
        "query_source",
        "query_stub",
    ):
        if job.get("metadata", {}).get(field) != parent_job.get("metadata", {}).get(
            field
        ):
            raise ValueError("single-size retry changed measurement setting: " + field)
    if job["lengths"] != [target["entry"]["requested_bytes"]]:
        raise ValueError("retry must measure exactly the failed byte size")
    results = [value for value in records if value["type"] == "result"]
    if (
        len(results) != 1
        or results[0]["entry"]["algorithm"] != target["entry"]["algorithm"]
    ):
        raise ValueError("retry must measure exactly the failed requested algorithm")
    selected = results[0]
    coverage = _coverage(audit).get(_key(selected), {})
    if not _has_probes(selected, coverage):
        raise ValueError(
            "single-size retry still has no complete five-stage probe samples"
        )
    if selected.get("shape") != target.get("shape"):
        raise ValueError("single-size retry changed actual tensor shape")
    source.update(
        run_id=cluster["run_id"],
        entry_id=selected["entry"]["id"],
        batch=selected["batch"],
        coverage=coverage,
    )
    replacement = copy.deepcopy(selected)
    replacement.update(
        job_id=parent_job["job_id"],
        entry=copy.deepcopy(target["entry"]),
        batch=target["batch"],
    )
    replacement["retry_source"] = source
    return replacement, source


def retry_missing_probes(args, summary, backend_args, run_child):
    """Return retry history, publishing a selected report only after recovery."""
    run_dir = Path(args.output_dir)
    records_path = run_dir / "profile_analysis/enriched_records.jsonl"
    audit_path = run_dir / "profile_analysis/profiling_audit.json"
    if not records_path.is_file() or not audit_path.is_file():
        return None
    records, audit, initial_source = _source(run_dir)
    coverage = _coverage(audit)
    targets = [
        value
        for value in records
        if value["type"] == "result"
        and value.get("status") in SUCCESS
        and value.get("ret") == 0
        and _key(value) in coverage
        and not _has_probes(value, coverage[_key(value)])
    ]
    if not targets:
        return None
    report_dir = run_dir / "report"
    history_path = report_dir / "ccu_probe_retries.json"
    history = {
        "max_attempts": MAX_ATTEMPTS,
        "initial_source": initial_source,
        "targets": len(targets),
        "recovered": 0,
        "attempts": [],
        "selections": [],
    }
    _write(history_path, history)
    print(
        "CCU探针缺失：{}项，将按尺寸和算法单独补采；记录={}".format(
            len(targets), history_path
        ),
        flush=True,
    )
    replacements = {}
    for target in targets:
        entry = target["entry"]
        for attempt in range(1, MAX_ATTEMPTS + 1):
            child = copy.copy(args)
            child.output_dir = (
                run_dir
                / "retries"
                / ("entry_%06d" % entry["id"])
                / ("attempt_%d" % attempt)
            )
            child.skip_build = True
            child.records = None
            child.ccu_probe_retry_child = True
            child.retry_context = dict(
                parent_run_id=summary["run_id"], entry_id=entry["id"], attempt=attempt
            )
            item = dict(
                child.retry_context,
                requested_bytes=entry["requested_bytes"],
                algorithm=entry["algorithm"],
                run_dir=str(child.output_dir),
                status="running",
            )
            history["attempts"].append(item)
            _write(history_path, history)
            print(
                "CCU探针补采：bytes={} algorithm={} attempt={}/{}".format(
                    entry["requested_bytes"], entry["algorithm"], attempt, MAX_ATTEMPTS
                ),
                flush=True,
            )
            try:
                if run_child(child, single_entry_arguments(backend_args, entry)) != 0:
                    cluster_path = child.output_dir / "cluster_summary.json"
                    if cluster_path.is_file() and any(
                        "launcher interrupted" in message
                        for message in _json(cluster_path).get("transport_errors", [])
                    ):
                        raise KeyboardInterrupt
                    raise ValueError(
                        "single-size retry failed; see its cluster_summary.json"
                    )
                replacement, source = _replacement(
                    records[0], target, child.output_dir, summary["run_id"]
                )
                replacements[_key(target)] = replacement
                history["selections"].append(
                    dict(
                        entry_id=entry["id"],
                        batch=target["batch"],
                        attempt=attempt,
                        source=source,
                    )
                )
                history["recovered"] += 1
                item["status"] = "recovered"
            except KeyboardInterrupt:
                item["status"] = "interrupted"
                _write(history_path, history)
                raise
            except Exception as error:
                item.update(status="failed", error=str(error))
            _write(history_path, history)
            if item["status"] == "recovered":
                print(
                    "CCU探针补采成功：bytes={} algorithm={}".format(
                        entry["requested_bytes"], entry["algorithm"]
                    ),
                    flush=True,
                )
                break
        else:
            print(
                "CCU探针补采未恢复：bytes={} algorithm={}，保留原结果".format(
                    entry["requested_bytes"], entry["algorithm"]
                ),
                flush=True,
            )
    history["unresolved"] = len(targets) - len(replacements)
    _write(history_path, history)
    remaining_issues = [
        issue
        for issue in audit.get("issues", [])
        if not (
            issue.get("stage") == "ccu_probes"
            and (issue.get("entry_id"), issue.get("batch")) in replacements
        )
    ]
    selected = []
    for value in records:
        if value["type"] == "result" and _key(value) in replacements:
            selected.append(replacements[_key(value)])
        elif (
            value["type"] == "failure"
            and value.get("stage") == "profiling"
            and value.get("error")
            == "profiling association or coverage failed; see profiling_audit.json"
            and not remaining_issues
        ):
            # Only remove the parser's generic issue record after all original
            # issues are accounted for. Other failure stages remain failures.
            continue
        else:
            selected.append(value)
    selected[0].setdefault("metadata", {})["ccu_probe_retry_audit"] = str(history_path)
    effective_coverage = copy.deepcopy(audit.get("ccu_probes", {}))
    effective_coverage["entries"] = [
        dict(
            replacements[(item["entry_id"], item["batch"])]["retry_source"]["coverage"],
            entry_id=item["entry_id"],
            batch=item["batch"],
        )
        if (item["entry_id"], item["batch"]) in replacements
        else item
        for item in effective_coverage.get("entries", [])
    ]
    selected[0]["metadata"]["ccu_probes"] = effective_coverage
    build_report(selected)
    selected_path = report_dir / "selected_records.jsonl"
    selected_path.write_text(
        "".join(
            json.dumps(value, ensure_ascii=False, allow_nan=False) + "\n"
            for value in selected
        ),
        encoding="utf-8",
    )
    verified = (
        not remaining_issues
        and not history["unresolved"]
        and (
            audit.get("profiling_association_verified") is True
            or bool(audit.get("issues"))
        )
    )
    composite_path = report_dir / "selected_profiling_audit.json"
    composite = {
        "initial_source": initial_source,
        "selections": history["selections"],
        "issues": remaining_issues,
        "original_issues": audit.get("issues", []),
        "ccu_probes": effective_coverage,
        "profiling_association_verified": verified,
        "enriched_records_sha256": _hash(selected_path),
    }
    _write(composite_path, composite)
    report_summary = write_report(selected, report_dir)
    report_summary.update(
        profiling_association_verified=verified,
        profiling_analysis_requested=True,
        profiling_audit=str(composite_path),
        cluster_status=summary["status"],
        enriched_records_sha256=composite["enriched_records_sha256"],
        ccu_probe_retries=str(history_path),
    )
    report_summary["failed"] = report_summary["failed"] or not verified
    _write(report_dir / "summary.json", report_summary)
    from console_report import print_native_report

    print("CCU探针补采后最终报告：", flush=True)
    print_native_report(report_dir, report_summary)
    print("Report: " + str(report_dir / "report.csv"), flush=True)
    return {
        "audit": str(history_path),
        "recovered": len(replacements),
        "unresolved": history["unresolved"],
        "profiling_association_verified": verified,
        "report": {
            "status": "FAIL" if report_summary["failed"] else "PASS",
            "directory": str(report_dir),
        },
        "analysis": {
            "returncode": 0 if verified else 1,
            "report_failed": report_summary["failed"],
            "audit_path": str(composite_path),
            "enriched_records": str(selected_path),
            "enriched_records_sha256": composite["enriched_records_sha256"],
            "profiling_association_verified": verified,
        },
    }
