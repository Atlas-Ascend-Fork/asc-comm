#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Display a native run's already validated report without inventing metrics."""

import csv
import json
from pathlib import Path
import sys
import unicodedata

from report import CCU_AUDIT_METRICS, COLUMN_LABELS


def _cell(value):
    """Keep errors and CSV values on one terminal line, including control bytes."""
    return "".join(
        "\\x{:02x}".format(ord(character))
        if ord(character) < 32 or ord(character) == 127
        else character
        for character in str(value)
    )


def _display_width(value):
    return sum(
        0
        if unicodedata.combining(character)
        else 2
        if unicodedata.east_asian_width(character) in {"W", "F"}
        else 1
        for character in value
    )


def _csv_value(row, name):
    # Older archived reports use the internal English field names.
    return row.get(COLUMN_LABELS[name], row.get(name, "-"))


def _entry_description(entry):
    return "bytes={} algorithm={}".format(
        entry.get("requested_bytes", "-"), entry.get("algorithm") or "-"
    )


def _reasons(summary, diagnostics):
    reasons = []
    if summary.get("profiling_analysis_error"):
        reasons.append(
            "Profiling analysis failed: " + summary["profiling_analysis_error"]
        )
    for failure in summary.get("failures", []):
        reasons.append(
            "{} failed (ret={}): {}".format(
                failure.get("stage", "operation"),
                failure.get("ret", "-"),
                failure.get("error", "no detail"),
            )
        )
    if summary.get("query_failed"):
        failed_queries = [
            query
            for query in summary.get("queries", [])
            if query.get("ret") or query.get("release_ret") or query.get("error_stage")
        ]
        for query in failed_queries:
            reasons.append(
                "Query failed: dataLen={} stage={} ret={} release_ret={}".format(
                    query.get("data_len", "-"),
                    query.get("error_stage") or "query",
                    query.get("ret", "-"),
                    query.get("release_ret", "-"),
                )
            )
    if summary.get("incomplete_queries"):
        reasons.append(
            "Query records are incomplete; no complete candidate set was confirmed."
        )
    unresolved = summary.get("names_contract_unresolved_points", [])
    if unresolved:
        reasons.append(
            "Query returned unresolved algorithm names; execution cannot be confirmed."
        )
    unsupported = summary.get("requested_algorithms_unsupported", [])
    if unsupported:
        reasons.append(
            "Requested algorithms were unsupported and skipped: "
            + ", ".join(unsupported)
        )
    elif (
        not summary.get("query_failed")
        and not summary.get("incomplete_queries")
        and not unresolved
        and summary.get("candidate_algorithms") == []
    ):
        reasons.append("Query returned no candidates; operator execution was skipped.")
    for entry in summary.get("missing_performance", []):
        reasons.append(
            "Result has no performance metrics: " + _entry_description(entry)
        )
    if summary.get("missing_plan"):
        reasons.append("The expected measurement plan is incomplete.")
    for record in diagnostics:
        if record.get("error"):
            reasons.append(
                "{} status={}{}: {}".format(
                    _entry_description(record.get("entry", {})),
                    record.get("status", "-"),
                    " stage=" + str(record["failure_stage"])
                    if record.get("failure_stage")
                    else "",
                    record["error"],
                )
            )
    # The summary and a selected result may describe the same failure.
    return list(dict.fromkeys(reasons))


def print_native_report(report_dir, summary, *, stream=None):
    """Print validated metrics and explicit no-measurement/failure conclusions.

    The caller has already validated records with report.write_report and may
    have set failed/cluster_status from the whole-job rank audit. Reading its
    CSV preserves the reported values and statuses for both native engines.
    Metrics are neither substituted nor derived here. Profiling reports show
    every applicable CSV metric; Event-only reports retain the compact view.
    """
    output = sys.stdout if stream is None else stream
    directory = Path(report_dir)
    with (directory / "report.csv").open(newline="", encoding="utf-8-sig") as source:
        rows = list(csv.DictReader(source))
    diagnostics = json.loads(
        (directory / "diagnostics.json").read_text(encoding="utf-8")
    )
    if not isinstance(diagnostics, list):
        raise ValueError("native report diagnostics must be a list")
    options = summary.get("options", {})
    failed = bool(summary.get("failed")) or summary.get("cluster_status") not in (
        None,
        "PASS",
    )
    measured = summary.get("measured", 0)
    status = "FAIL" if failed else "PASS" if measured else "SKIPPED"
    print(
        "Native performance: {} | measured={} planned={} skipped={}".format(
            status,
            measured,
            summary.get("planned", 0),
            summary.get("unsupported", 0),
        ),
        file=output,
    )
    metadata = summary.get("metadata", {})
    profiling = metadata.get("timing_source") == "profiling" or bool(
        summary.get("profiling_analysis_requested")
    )
    if metadata.get("query_source"):
        print(
            "Query source: {}{}; execution: {}; timing: {}".format(
                _cell(metadata["query_source"]),
                " (" + _cell(metadata["query_stub"]) + ")"
                if metadata.get("query_stub")
                else "",
                _cell(metadata.get("execution_source", "unknown")),
                _cell(metadata.get("timing_source", "unknown")),
            ),
            file=output,
        )
    if rows:
        hidden_fields = set(CCU_AUDIT_METRICS)
        if options.get("engine") in {"ccu_sched", "ccu_ms"}:
            hidden_fields.add("api_kernel_us")
        if profiling:
            names = {label: name for name, label in COLUMN_LABELS.items()}
            fields = [
                names.get(name, name)
                for name in rows[0]
                if names.get(name, name) not in hidden_fields
            ]
            fields.insert(fields.index("op") + 1, "engine")
        else:
            fields = (
                "op",
                "engine",
                "dtype",
                "requested_bytes",
                "ranks",
                "algorithm",
                "api_kernel_us",
                "status",
            )
            fields = tuple(field for field in fields if field not in hidden_fields)
        columns = tuple(COLUMN_LABELS[name] for name in fields)
        values = [
            [
                options.get("engine", "-")
                if field == "engine"
                else _csv_value(row, field) or "-"
                for field in fields
            ]
            for row in rows
        ]
        text_rows = [[_cell(value) for value in row] for row in [columns, *values]]
        widths = [
            max(_display_width(row[index]) for row in text_rows)
            for index in range(len(columns))
        ]
        for index, row in enumerate(text_rows):
            print(
                "  ".join(
                    value + " " * (width - _display_width(value))
                    for value, width in zip(row, widths)
                ).rstrip(),
                file=output,
            )
            if index == 0:
                print("  ".join("-" * width for width in widths), file=output)
    if measured:
        if options.get("engine") in {
            "ccu_sched",
            "ccu_ms",
        }:
            if not profiling:
                print(
                    "Raw ACL Event measurements are retained in diagnostics.json.",
                    file=output,
                )
        elif metadata.get("timing_source") == "acl_event":
            print(
                COLUMN_LABELS["api_kernel_us"]
                + ": whole custom-op ACL Event in us, rank 0 iteration mean; "
                "not pure communication or algorithm time.",
                file=output,
            )
        elif profiling:
            print(
                "Profiling metrics: phases and kernel duration come from associated device profiling records.",
                file=output,
            )
        if summary.get("algorithm_unconfirmed"):
            print(
                "algorithm_unconfirmed: performance was measured; the actual algorithm has not been confirmed.",
                file=output,
            )
    elif profiling and summary.get("native_event_records"):
        print(
            "NO PROFILING METRICS: no valid associated profiling values are available; "
            "raw ACL Event measurements are retained.",
            file=output,
        )
    else:
        print(
            "NO MEASUREMENTS: no measured performance values were produced.",
            file=output,
        )
    if failed:
        print("Run failed; any measured rows are retained for diagnosis.", file=output)
        if summary.get("cluster_status") not in (None, "PASS"):
            print(
                "Cluster status: "
                + _cell(summary["cluster_status"])
                + "; inspect per-rank status and logs.",
                file=output,
            )
    if profiling:
        print(
            "Profiling association: "
            + ("verified" if summary.get("profiling_association_verified") else "FAIL"),
            file=output,
        )
        if summary.get("native_event_records"):
            print(
                "Raw ACL Event records: " + _cell(summary["native_event_records"]),
                file=output,
            )
    reasons = _reasons(summary, diagnostics)
    for reason in reasons[:10]:
        print("Reason: " + _cell(reason), file=output)
    if len(reasons) > 10:
        print("Additional reasons: " + str(len(reasons) - 10), file=output)
    if failed or not measured:
        print("Diagnostics: " + str(directory / "diagnostics.json"), file=output)
    output.flush()
