#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Build the isolated six-operation CCU package; never install or launch MPI."""

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

from build_package import (
    EXTERNAL_ASCENDC_HELPERS,
    SDK_BUILD_TOOLS,
    atomic_json,
    create_default_run_dir,
    digest,
    resolve_ascendc_util_dir,
    resolve_cann_environment_script,
    resolve_makeself,
    run_build,
)

TOOL = Path(__file__).resolve().parents[1]
MARKER_NAME = "asccomm_api_bench_ccu_package.info"
MARKER_FIELDS = {
    "package": "asccomm_api_bench_ccu",
    "schema": "3",
    "mode": "ccu",
    "operators": "allgather,allreduce,reducescatter,alltoall,alltoallv,alltoallvwrite",
    "algorithm": "runtime",
    "runtime_algorithm_attr": "1",
    "comm_engine_attr": "1",
    "supported_engines": "5,6",
    "kernel_iterations": "1",
    "measurement_mode": "full",
    "send_vector_layout": "mission_major_2xranks_bytes",
    "peer_alignment_bytes": "128",
    "report_layout": "magic,wait_duration_ns",
    "window_owner": "HcclGetHcclBuffer",
}


def read_marker(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError("missing regular CCU package marker")
    fields = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if not separator or not key or key in fields:
            raise ValueError("invalid or duplicate CCU marker field")
        fields[key] = value
    if fields != MARKER_FIELDS:
        raise ValueError("incompatible CCU package marker")
    return fields


def stage_source(source, archive):
    """Reuse this tool's migrated build helpers without changing their source."""
    shared = TOOL / "custom_op_aicpu"
    staged = archive / "source"
    shutil.copytree(source, staged)
    shutil.copytree(shared / "cmake", staged / "cmake")
    for name in ("build.sh", "CMakePresets.json"):
        shutil.copy2(shared / name, staged / name)
    shutil.copy2(shared / "op_host/CMakeLists.txt", staged / "op_host/CMakeLists.txt")
    preset = staged / "CMakePresets.json"
    config = json.loads(preset.read_text())
    config["configurePresets"][0]["cacheVariables"]["vendor_name"]["value"] = (
        "asccomm_api_bench_ccu"
    )
    preset.write_text(json.dumps(config, indent=2) + "\n")
    environment_script = archive / "scripts/benchmark_environment.sh"
    environment_script.parent.mkdir()
    shutil.copy2(TOOL / "scripts/benchmark_environment.sh", environment_script)
    return staged


def build_ccu_package(
    source_dir,
    run_dir,
    cann_root,
    *,
    cmake="cmake",
    makeself=None,
    ascendc_util_dir=None,
    jobs=None,
    timeout=1800,
):
    source, run, cann = (
        Path(value).expanduser().resolve() for value in (source_dir, run_dir, cann_root)
    )
    if source == run or source in run.parents or run in source.parents:
        raise ValueError("run directory must be separate from package source")
    if (
        not isinstance(timeout, (int, float))
        or not math.isfinite(timeout)
        or timeout <= 0
    ):
        raise ValueError("timeout must be finite and positive")
    if jobs is not None and (type(jobs) is not int or jobs < 1):
        raise ValueError("jobs must be a positive integer")
    for name in (
        "CMakeLists.txt",
        MARKER_NAME + ".in",
        "op_host",
        "op_kernel",
        "scripts",
    ):
        if not (source / name).exists():
            raise ValueError("missing CCU package source: " + name)
    archive = run / "ccu_build"
    archive.mkdir(parents=True, exist_ok=False)
    manifest_path = archive / "build_manifest.json"
    manifest = {
        "schema_version": 1,
        "status": "building",
        "stage": "preflight",
        "source_dir": str(source),
        "cann_root": str(cann),
        "measurement_mode": "full",
        "profile": "collectives_ccu",
        "started_at": datetime.now(timezone.utc).isoformat(),
    }
    atomic_json(manifest_path, manifest)
    try:
        for directory in ("include", "compiler"):
            if not (cann / directory).is_dir():
                raise ValueError("missing installed SDK directory: " + directory)
        for name in SDK_BUILD_TOOLS:
            tool = cann / name
            if not tool.is_file() or not os.access(tool, os.X_OK):
                raise ValueError("missing executable SDK build tool: " + name)
        environment_script = resolve_cann_environment_script(cann)
        helper_dir = resolve_ascendc_util_dir(cann, ascendc_util_dir)
        resolved_cmake = shutil.which(str(cmake))
        resolved_makeself = resolve_makeself(makeself, cann_root=cann)
        if not resolved_cmake:
            raise ValueError("installed CMake executable is required")
        tools = {
            "cmake": Path(resolved_cmake),
            "makeself": Path(resolved_makeself),
            "cann_environment": environment_script,
            **{name: cann / name for name in SDK_BUILD_TOOLS},
            **{name: helper_dir / name for name in EXTERNAL_ASCENDC_HELPERS},
        }
        manifest["external_tools"] = {
            name: {"path": str(path), "sha256": digest(path)}
            for name, path in tools.items()
        }
        staged = stage_source(source, archive)
        manifest["source_files"] = {
            str(path.relative_to(staged)): digest(path)
            for path in sorted(staged.rglob("*"))
            if path.is_file()
        }
        environment = dict(os.environ)
        environment.update(
            ASCEND_HOME_PATH=str(cann),
            ASCEND_CANN_PACKAGE_PATH=str(cann),
            MC2_BENCH_BUILD_DIR=str(archive / "work"),
            MC2_BENCH_CMAKE=resolved_cmake,
            MC2_BENCH_MAKESELF=resolved_makeself,
            MC2_BENCH_ASCENDC_UTIL_DIR=str(helper_dir),
            MC2_BENCH_COMM_ONLY="OFF",
        )
        if jobs is not None:
            environment["MC2_BENCH_BUILD_JOBS"] = str(jobs)
        else:
            environment.pop("MC2_BENCH_BUILD_JOBS", None)
        command = ["bash", str(staged / "build.sh"), "package"]
        manifest.update(stage="build", command=command)
        atomic_json(manifest_path, manifest)
        code = run_build(command, staged, environment, archive / "build.log", timeout)
        manifest["returncode"] = code
        if code:
            raise RuntimeError(
                "CCU package build failed; see " + str(archive / "build.log")
            )
        manifest["stage"] = "package_validation"
        candidates = sorted((archive / "work").glob("custom_opp_*.run"))
        if (
            len(candidates) != 1
            or not candidates[0].is_file()
            or candidates[0].is_symlink()
        ):
            raise ValueError("build must produce exactly one regular CCU installer")
        marker = read_marker(archive / "work" / MARKER_NAME)
        output = archive / candidates[0].name
        shutil.copy2(candidates[0], output)
        shutil.copy2(archive / "work" / MARKER_NAME, archive / MARKER_NAME)
        manifest.update(
            status="built",
            stage="completed",
            marker=marker,
            installer={
                "name": output.name,
                "path": str(output),
                "sha256": digest(output),
            },
        )
        return output
    except BaseException as error:
        manifest.update(status="failed", error=str(error))
        with (archive / "build.log").open("a", encoding="utf-8") as log:
            log.write("\n[ERROR] " + str(error) + "\n")
        raise
    finally:
        manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
        atomic_json(manifest_path, manifest)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=TOOL / "custom_op_ccu")
    parser.add_argument(
        "--run-dir",
        type=Path,
        help="archive root; omitted: create a new directory under build_out/packages/ccu",
    )
    parser.add_argument(
        "--cann-root",
        type=Path,
        default=os.environ.get("ASCEND_HOME_PATH")
        or os.environ.get("ASCEND_CANN_PACKAGE_PATH")
        or "/usr/local/Ascend/cann",
    )
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--makeself")
    parser.add_argument("--ascendc-util-dir", type=Path)
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--timeout", type=float, default=1800)
    args = parser.parse_args(argv)
    try:
        run_dir = args.run_dir
        if run_dir is None:
            run_dir = create_default_run_dir("ccu")
            print("Build output: " + str(run_dir), file=sys.stderr)
        installer = build_ccu_package(
            args.source_dir,
            run_dir,
            args.cann_root,
            cmake=args.cmake,
            makeself=args.makeself,
            ascendc_util_dir=args.ascendc_util_dir,
            jobs=args.jobs,
            timeout=args.timeout,
        )
        print(str(installer))
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("CCU package build failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
