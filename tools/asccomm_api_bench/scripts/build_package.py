#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Build the migrated AICPU custom operators without installing them.

The upstream build.sh still drives CMake's binary and package targets. The CLI
automatically allocates an output directory unless --run-dir is supplied. Every
invocation gets a new run-local build directory and a digest-bearing receipt.
Reusing a completed installer is separate from launcher's --skip-build, which
validates the operators already installed on every host and never installs.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


SOURCE_COMMIT = "493be569dcaa2d7a4c2cb8892e950c03588c7243"
DEFAULT_BUILD_ROOT = Path(__file__).resolve().parents[1] / "build_out" / "packages"
MARKER_NAME = "asccomm_api_bench_package.info"
MARKER_FIELDS = {
    "package": "asccomm_api_bench",
    "schema": "1",
    "source_commit": SOURCE_COMMIT,
    "mode": "aicpu_ts",
    "operator": "all",
    "algorithm": "runtime",
    "runtime_algorithm_attr": "1",
    "kernel_iterations": "1",
}
EXTERNAL_ASCENDC_HELPERS = ("ascendc_gen_options.py", "parse_ini_to_json.py")
SDK_BUILD_TOOLS = (
    "toolkit/tools/opbuild/op_build",
    "toolkit/toolchain/hcc/bin/aarch64-target-linux-gnu-g++",
    "toolkit/toolchain/hcc/bin/aarch64-target-linux-gnu-gcc",
)
SDK_MAKESELF_DIRECTORIES = (
    "toolkit/tools/op_project_templates/ascendc/customize/cmake/util/makeself",
    "tools/op_project_templates/ascendc/customize/cmake/util/makeself",
    "tools/tikcpp/ascendc_kernel_cmake/fwk_modules/util/makeself",
)


def create_default_run_dir(profile):
    """Allocate a separate archive for each standalone CLI invocation."""
    parent = DEFAULT_BUILD_ROOT / profile
    parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix="package-", dir=str(parent)))


def resolve_cann_environment_script(cann_root):
    """Use the same selected-root/parent set_env contract as deployment."""
    for script in (cann_root / "set_env.sh", cann_root.parent / "set_env.sh"):
        if script.is_file():
            return script
    raise ValueError("CANN set_env.sh not found under selected SDK root or its parent")


def resolve_makeself(override=None, *, cann_root=None):
    """Honor explicit selections, then discover the selected SDK tool or PATH."""
    selected = (
        override if override is not None else os.environ.get("MC2_BENCH_MAKESELF")
    )
    if selected is not None:
        executable = shutil.which(str(Path(selected).expanduser()))
        if executable is None:
            source = "--makeself" if override is not None else "MC2_BENCH_MAKESELF"
            raise ValueError(
                "explicit makeself executable is unavailable or not executable: "
                + repr(str(selected))
                + " ("
                + source
                + "); correct this setting; automatic SDK/PATH discovery is disabled"
            )
        return str(Path(executable).resolve())
    if cann_root is not None:
        cann = Path(cann_root).expanduser().resolve()
        for relative in SDK_MAKESELF_DIRECTORIES:
            executable = cann / relative / "makeself.sh"
            header = executable.with_name("makeself-header.sh")
            if (
                executable.is_file()
                and os.access(executable, os.X_OK)
                and header.is_file()
                and os.access(header, os.R_OK)
            ):
                # Makeself locates its header relative to argv[0]. Preserve a
                # script symlink when the SDK keeps the pair in that directory.
                return str(executable)
    for name in ("makeself", "makeself.sh"):
        executable = shutil.which(name)
        if executable is not None:
            return str(Path(executable).resolve())
    raise ValueError(
        "makeself executable is unavailable; no complete makeself.sh/makeself-header.sh pair "
        "was found in the selected CANN SDK, and neither makeself nor makeself.sh was found on PATH; "
        "check the CANN installation or set --makeself /path/to/makeself.sh or MC2_BENCH_MAKESELF"
    )


def resolve_ascendc_util_dir(cann_root, override=None):
    """Locate the installed SDK helpers without vendoring their Apache sources."""
    selected = override or os.environ.get("MC2_BENCH_ASCENDC_UTIL_DIR")
    if selected:
        candidates = [Path(selected).expanduser().resolve()]
    else:
        candidates = [
            Path(cann_root)
            / prefix
            / "op_project_templates/ascendc/customize/cmake/util"
            for prefix in ("tools", "toolkit/tools")
        ]
    for candidate in candidates:
        if all((candidate / name).is_file() for name in EXTERNAL_ASCENDC_HELPERS):
            return candidate.resolve()
    raise ValueError(
        "installed AscendC helpers ascendc_gen_options.py and parse_ini_to_json.py are missing; "
        "set MC2_BENCH_ASCENDC_UTIL_DIR or --ascendc-util-dir to the SDK helper directory"
    )


def digest(path):
    hasher = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def read_marker(path, measurement_mode):
    """Reject old fixed-algorithm packages and incompatible measurements."""
    if measurement_mode not in {"full", "comm_only"}:
        raise ValueError("measurement_mode must be full or comm_only")
    marker_path = Path(path)
    if marker_path.is_symlink() or not marker_path.is_file():
        raise ValueError("missing regular package marker: " + str(path))
    fields = {}
    for line in marker_path.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if not separator or not key or key in fields:
            raise ValueError("invalid or duplicate package marker field: " + line)
        fields[key] = value
    required = dict(MARKER_FIELDS, measurement_mode=measurement_mode)
    required["commit_delay_nops"] = (
        "5000000" if measurement_mode == "comm_only" else "0"
    )
    for key, value in required.items():
        if fields.get(key) != value:
            raise ValueError("incompatible package marker field " + key)
    return fields


def atomic_json(path, value):
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    os.replace(str(temporary), str(path))


def run_build(command, source, environment, log_path, timeout):
    """Bound the whole compiler process group, including timeout cleanup."""
    with log_path.open("x", encoding="utf-8") as stream:
        process = subprocess.Popen(
            command,
            cwd=str(source),
            env=environment,
            stdout=stream,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            return process.wait(timeout=timeout)
        except BaseException:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            # Reaping the shell does not prove that its compiler children have
            # stopped: a worker can ignore SIGTERM after the shell has exited.
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                process.poll()
                try:
                    os.killpg(process.pid, 0)
                except ProcessLookupError:
                    break
                time.sleep(min(0.02, max(0, deadline - time.monotonic())))
            else:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.wait()
            raise


def validate_existing_installer(installer, measurement_mode):
    """Validate a prior build receipt before copying a reused installer.

    This checks the marker and digest archived by a completed build. Deployment
    subsequently verifies the marker and libraries actually installed by it.
    """
    installer = Path(installer).expanduser().absolute()
    if installer.is_symlink() or not installer.is_file() or installer.suffix != ".run":
        raise ValueError("existing installer must be a regular .run file")
    receipt_path = installer.parent / "build_manifest.json"
    if receipt_path.is_symlink() or not receipt_path.is_file():
        raise ValueError("existing installer requires its build_manifest.json")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if receipt.get("schema_version") != 1 or receipt.get("status") not in {
        "built",
        "reused",
    }:
        raise ValueError("existing installer has no completed build receipt")
    saved_installer = receipt.get("installer", {})
    if saved_installer.get("name") != installer.name or saved_installer.get(
        "sha256"
    ) != digest(installer):
        raise ValueError("existing installer digest does not match its build receipt")
    marker = read_marker(installer.parent / MARKER_NAME, measurement_mode)
    if receipt.get("marker") != marker:
        raise ValueError("existing package marker does not match its build receipt")
    return installer, marker


def build_package(
    source_dir,
    run_dir,
    cann_root,
    measurement_mode="full",
    *,
    skip_build=False,
    installer=None,
    cmake="cmake",
    makeself=None,
    ascendc_util_dir=None,
    jobs=None,
    timeout=1800,
):
    """Return an archived installer Path, raising on any incomplete build.

    source_dir is the migrated custom_op_aicpu directory. run_dir/build must
    not already exist. A fresh CMake work tree, log, marker, installer and JSON
    receipt are kept there. No package is installed and MPI is never started.
    In API reuse mode (skip_build=True), installer must point at a completed
    builder archive; cann_root and source_dir are not used to build anything.
    """
    if measurement_mode not in {"full", "comm_only"}:
        raise ValueError("measurement_mode must be full or comm_only")
    if (
        not isinstance(timeout, (int, float))
        or not math.isfinite(timeout)
        or timeout <= 0
    ):
        raise ValueError("build timeout must be finite and positive")
    if jobs is not None and (type(jobs) is not int or jobs <= 0):
        raise ValueError("build jobs must be a positive integer")
    if not skip_build and installer is not None:
        raise ValueError("installer is only valid for explicit package reuse")
    if skip_build and installer is None:
        raise ValueError("explicit package reuse requires an installer")

    source = Path(source_dir).expanduser().resolve()
    run = Path(run_dir).expanduser().resolve()
    if run == source or run in source.parents or source in run.parents:
        raise ValueError("run directory must be separate from package source")
    archive = run / "build"
    reuse = (
        validate_existing_installer(installer, measurement_mode) if skip_build else None
    )
    cann = Path(cann_root).expanduser().resolve() if cann_root is not None else None
    if not skip_build:
        for name in ("build.sh", "CMakeLists.txt", "asccomm_api_bench_package.info.in"):
            if not (source / name).is_file():
                raise ValueError(
                    "missing migrated package source: " + str(source / name)
                )

    archive.mkdir(parents=True, exist_ok=False)
    manifest = {
        "schema_version": 1,
        "status": "building",
        "stage": "preflight",
        "source_commit": SOURCE_COMMIT,
        "source_dir": str(source),
        "cann_root": str(cann) if cann is not None else None,
        "measurement_mode": measurement_mode,
        "started_at": datetime.now(timezone.utc).isoformat(),
    }
    manifest_path = archive / "build_manifest.json"
    atomic_json(manifest_path, manifest)
    try:
        if reuse:
            old_installer, marker = reuse
            output = archive / old_installer.name
            shutil.copy2(str(old_installer), str(output))
            shutil.copy2(
                str(old_installer.parent / MARKER_NAME), str(archive / MARKER_NAME)
            )
            (archive / "build.log").write_text(
                "Validated and archived existing installer.\n", encoding="utf-8"
            )
            manifest["reused_from"] = str(old_installer)
        else:
            if cann is None or not all(
                (cann / name).is_dir() for name in ("include", "compiler")
            ):
                raise ValueError(
                    "CANN root must contain the installed include and compiler directories"
                )
            for name in SDK_BUILD_TOOLS:
                path = cann / name
                if not path.is_file() or not os.access(path, os.X_OK):
                    raise ValueError(
                        "missing executable SDK build tool required by package CMake: "
                        + str(path)
                    )
            cann_environment = resolve_cann_environment_script(cann)
            resolved_cmake = shutil.which(str(cmake))
            if resolved_cmake is None:
                raise ValueError("CMake executable is unavailable: " + str(cmake))
            resolved_makeself = resolve_makeself(makeself, cann_root=cann)
            helper_dir = resolve_ascendc_util_dir(cann, ascendc_util_dir)
            manifest["external_tools"] = {
                "cmake": {"path": resolved_cmake, "sha256": digest(resolved_cmake)},
                "makeself": {
                    "path": resolved_makeself,
                    "sha256": digest(resolved_makeself),
                },
                "cann_environment": {
                    "path": str(cann_environment),
                    "sha256": digest(cann_environment),
                },
                "sdk_build_tools": {
                    name: {"path": str(cann / name), "sha256": digest(cann / name)}
                    for name in SDK_BUILD_TOOLS
                },
                "ascendc_helpers": {
                    name: {
                        "path": str(helper_dir / name),
                        "sha256": digest(helper_dir / name),
                    }
                    for name in EXTERNAL_ASCENDC_HELPERS
                },
            }
            environment = dict(os.environ)
            environment.update(
                ASCEND_HOME_PATH=str(cann),
                ASCEND_CANN_PACKAGE_PATH=str(cann),
                MC2_BENCH_BUILD_DIR=str(archive / "work"),
                MC2_BENCH_CMAKE=resolved_cmake,
                MC2_BENCH_MAKESELF=resolved_makeself,
                MC2_BENCH_ASCENDC_UTIL_DIR=str(helper_dir),
                MC2_BENCH_COMM_ONLY="ON" if measurement_mode == "comm_only" else "OFF",
            )
            if jobs is not None:
                environment["MC2_BENCH_BUILD_JOBS"] = str(jobs)
            else:
                environment.pop("MC2_BENCH_BUILD_JOBS", None)
            command = ["bash", str(source / "build.sh"), "package"]
            manifest["command"] = command
            manifest["stage"] = "build"
            atomic_json(manifest_path, manifest)
            returncode = run_build(
                command, source, environment, archive / "build.log", timeout
            )
            manifest["returncode"] = returncode
            if returncode:
                raise RuntimeError(
                    "AICPU package build failed; see " + str(archive / "build.log")
                )
            work = archive / "work"
            manifest["stage"] = "package_validation"
            candidates = sorted(work.glob("custom_opp_*.run"))
            if (
                len(candidates) != 1
                or candidates[0].is_symlink()
                or not candidates[0].is_file()
            ):
                raise ValueError(
                    "build must produce exactly one regular custom_opp_*.run installer"
                )
            marker = read_marker(work / MARKER_NAME, measurement_mode)
            output = archive / candidates[0].name
            shutil.copy2(str(candidates[0]), str(output))
            shutil.copy2(str(work / MARKER_NAME), str(archive / MARKER_NAME))
        manifest.update(
            status="reused" if skip_build else "built",
            stage="completed",
            marker=marker,
            installer={
                "path": str(output),
                "name": output.name,
                "sha256": digest(output),
            },
        )
    except BaseException as error:
        manifest.update(status="failed", error=str(error))
        with (archive / "build.log").open("a", encoding="utf-8") as log:
            log.write("\n[ERROR] " + str(error) + "\n")
        raise
    finally:
        manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
        atomic_json(manifest_path, manifest)
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "custom_op_aicpu",
    )
    parser.add_argument(
        "--run-dir",
        type=Path,
        help="archive root; omitted: create a new directory under build_out/packages/aicpu",
    )
    parser.add_argument(
        "--cann-root",
        type=Path,
        default=os.environ.get("ASCEND_HOME_PATH")
        or os.environ.get("ASCEND_CANN_PACKAGE_PATH")
        or "/usr/local/Ascend/cann",
    )
    parser.add_argument(
        "--measurement-mode", choices=("full", "comm_only"), default="full"
    )
    parser.add_argument(
        "--reuse-installer",
        type=Path,
        help="reuse a completed builder archive without building",
    )
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument(
        "--makeself",
        help="explicit makeself executable; otherwise discover selected CANN SDK then PATH",
    )
    parser.add_argument(
        "--ascendc-util-dir", type=Path, help="installed SDK helper directory"
    )
    parser.add_argument("--jobs", type=int)
    args = parser.parse_args(argv)
    try:
        run_dir = args.run_dir
        if run_dir is None:
            run_dir = create_default_run_dir("aicpu")
            print("Build output: " + str(run_dir), file=sys.stderr)
        output = build_package(
            args.source_dir,
            run_dir,
            args.cann_root,
            args.measurement_mode,
            skip_build=args.reuse_installer is not None,
            installer=args.reuse_installer,
            cmake=args.cmake,
            makeself=args.makeself,
            ascendc_util_dir=args.ascendc_util_dir,
            jobs=args.jobs,
        )
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
