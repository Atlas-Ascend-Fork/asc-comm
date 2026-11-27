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

"""Render algorithm discovery without creating performance plans or metrics."""

import json
from pathlib import Path

from report import _external_algorithm, _query_requests, _uint, read_records


def write_query_report(run_dir, cluster):
    root = Path(run_dir)
    output = root / "query_summary.json"
    records = root / "collected/ranks/rank_000000/records.jsonl"
    result = {
        "schema_version": 1,
        "kind": "algorithm_query",
        "status": "FAIL",
        "run_id": cluster["run_id"],
        "query_source": None,
        "options": {},
        "query_mode": None,
        "queries": [],
        "algorithms": [],
        "matched_algorithms": [],
        "unsupported_algorithms": [],
        "errors": [],
    }
    try:
        values = read_records(records)
        jobs = [v for v in values if v["type"] == "job"]
        if len(jobs) != 1:
            raise ValueError("query-only report requires one job")
        job = jobs[0]
        metadata = job.get("metadata", {})
        if (
            metadata.get("run_mode") != "query_only"
            or job["job_id"] != cluster["run_id"]
        ):
            raise ValueError("query-only job identity/mode differs from this run")
        if any(v["job_id"] != job["job_id"] for v in values):
            raise ValueError("query-only records contain another job")
        if any(v["type"] not in {"job", "query", "failure"} for v in values):
            raise ValueError(
                "query-only records must not contain performance plans/results"
            )
        if job.get("schema_version") != 2:
            raise ValueError("query-only report requires schema 2 records")
        options = job["options"]
        if not isinstance(options.get("algorithms"), list) or any(
            not _external_algorithm(v) for v in options["algorithms"]
        ):
            raise ValueError("query-only requested algorithm names are invalid")
        if options["ranks"] != cluster["expected_ranks"] or metadata.get(
            "query_source"
        ) not in {
            "native_mc2",
            "mock",
        }:
            raise ValueError("query-only rank count or query source is invalid")
        result.update(options=options, query_source=metadata["query_source"])
        mode, requests = _query_requests(
            job, options, job["lengths"], options["algorithms"]
        )
        result["query_mode"] = mode
        queries = [v for v in values if v["type"] == "query"]
        result["queries"] = queries
        for failure in (v for v in values if v["type"] == "failure"):
            result["errors"].append(
                failure.get("stage", "query")
                + ": "
                + failure.get("error", "backend failure")
            )
        if len(queries) != len(requests):
            raise ValueError("query-only response count differs from declared requests")
        candidates = set()
        for request, query in zip(requests, queries):
            if (
                _uint(query.get("query_id"), "query id") != request["id"]
                or query.get("query_mode") != mode
                or query.get("data_len_set") is not True
                or _uint(query.get("data_len"), "query data_len") != request["data_len"]
                or _uint(query.get("requested_bytes"), "query requested_bytes")
                != request["requested_bytes"]
            ):
                raise ValueError("query-only response does not match its request")
            if (
                query["ret"] != 0
                or query["release_ret"] != 0
                or query.get("error_stage")
            ):
                raise ValueError(
                    "query-only API or argument release failed"
                    + (": " + query["error"] if query.get("error") else "")
                )
            names = query["algorithms"]
            if not isinstance(names, list) or any(
                not _external_algorithm(n) for n in names
            ):
                raise ValueError(
                    "query-only response has an invalid external algorithm name"
                )
            candidates.update(names)
        if cluster["status"] != "PASS":
            raise ValueError("MPI query did not complete successfully on every rank")
        if not result["errors"]:
            result["status"] = "PASS"
            result["algorithms"] = sorted(candidates)
            selected = set(options["algorithms"])
            result["matched_algorithms"] = sorted(selected & candidates)
            result["unsupported_algorithms"] = sorted(selected - candidates)
    except (KeyError, TypeError, ValueError, OSError) as error:
        result.update(
            status="FAIL",
            algorithms=[],
            matched_algorithms=[],
            unsupported_algorithms=[],
        )
        result["errors"].append(str(error))
    result["candidate_count"] = len(result["algorithms"])
    output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    if result["status"] == "PASS":
        label = "MOCK" if result["query_source"] == "mock" else "NATIVE MC2"
        print(
            "算法查询（"
            + label
            + "）：去重候选 "
            + str(len(result["algorithms"]))
            + " 个",
            flush=True,
        )
        print(
            "用户指定："
            + (", ".join(result["options"]["algorithms"]) or "未指定，查询全部候选"),
            flush=True,
        )
        for index, name in enumerate(result["algorithms"], 1):
            print("  " + str(index) + ". " + name, flush=True)
        if not result["algorithms"]:
            print("当前查询条件未返回候选算法。", flush=True)
        if result["options"]["algorithms"]:
            print(
                "指定算法匹配：" + ", ".join(result["matched_algorithms"]), flush=True
            )
            if result["unsupported_algorithms"]:
                print(
                    "查询未命中：" + ", ".join(result["unsupported_algorithms"]),
                    flush=True,
                )
    else:
        print("算法查询失败：" + "; ".join(result["errors"]), flush=True)
    print("Query summary: " + str(output), flush=True)
    return {
        "status": result["status"],
        "path": str(output),
        "candidate_count": len(result["algorithms"]),
        "errors": result["errors"],
    }
