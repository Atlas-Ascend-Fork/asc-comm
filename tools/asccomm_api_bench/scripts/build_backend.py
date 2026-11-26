#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Build and verify the real MPI/ACLNN backend against an installed package.

The caller owns the operator installation lock. This module neither installs
operators nor starts MPI. A build uses a fresh directory; explicit reuse checks
its receipt against the current source, SDK, operators, MPI and build tools.
"""

from datetime import datetime, timezone
import hashlib
import json
import math
import os
import platform
from pathlib import Path
import shlex
import shutil

from build_package import (
    atomic_json,
    digest,
    resolve_cann_environment_script,
    run_build,
)


SOURCE_ROOT = Path(__file__).resolve().parents[1]
BACKEND_NAME = "asccomm_api_bench_backend"
QUERY_BACKEND_NAME = "asccomm_api_bench_query"
GENERATED_HEADERS = (
    "aclnn_all_gather_custom_v3.h",
    "aclnn_all_reduce_custom_v3.h",
    "aclnn_all_to_all_custom_v3.h",
    "aclnn_all_to_all_v_custom_v3.h",
    "aclnn_reduce_scatter_custom_v3.h",
)
CCU_GENERATED_HEADERS = (
    "aclnn_all_gather_ccu_bench.h",
    "aclnn_all_reduce_ccu_bench.h",
    "aclnn_all_to_all_ccu_bench.h",
    "aclnn_all_to_all_v_ccu_bench.h",
    "aclnn_reduce_scatter_ccu_bench.h",
    "aclnn_all_to_all_v_write_bench.h",
)
BUILD_ENVIRONMENT_KEYS = (
    "CFLAGS",
    "CXXFLAGS",
    "CPPFLAGS",
    "LDFLAGS",
    "CPATH",
    "C_INCLUDE_PATH",
    "CPLUS_INCLUDE_PATH",
    "LIBRARY_PATH",
    "CMAKE_PREFIX_PATH",
    "CMAKE_INCLUDE_PATH",
    "CMAKE_LIBRARY_PATH",
    "CMAKE_MODULE_PATH",
    "CMAKE_TOOLCHAIN_FILE",
    "MPICH_CXX",
    "MPICH_CC",
    "OMPI_CXX",
    "OMPI_CC",
    "I_MPI_CXX",
    "I_MPI_CC",
)


def _root(value, name):
    if value is None:
        raise ValueError(name + " is required")
    path = Path(value).expanduser().resolve()
    if not path.is_dir():
        raise ValueError(name + " is not a directory: " + str(path))
    return path


def _file(path, name, *, executable=False):
    if not path.is_file() or (executable and not os.access(path, os.X_OK)):
        raise ValueError("missing " + name + ": " + str(path))
    return {"path": str(path), "realpath": str(path.resolve()), "sha256": digest(path)}


def _tool(command, name):
    resolved = shutil.which(str(Path(command).expanduser()))
    if resolved is None:
        raise ValueError(name + " executable is unavailable: " + str(command))
    # Keep a wrapper's invocation path: MPI and compiler wrappers can depend on
    # argv[0], even when several names refer to the same executable or symlink.
    return _file(Path(resolved).absolute(), name + " executable", executable=True)


def _walk_files(path):
    """Follow SDK include directory links while stopping directory cycles."""
    pending = [(path, frozenset())]
    while pending:
        directory, ancestors = pending.pop()
        resolved = directory.resolve()
        if resolved in ancestors:
            raise ValueError("cyclic dependency directory link: " + str(directory))
        ancestors = ancestors | {resolved}
        for entry in sorted(directory.iterdir()):
            if entry.is_dir():
                pending.append((entry, ancestors))
            elif entry.is_file():
                yield entry


def _tree(path, name, *, allow_empty=False):
    if not path.is_dir():
        raise ValueError("missing " + name + " directory: " + str(path))
    hasher = hashlib.sha256()
    count = 0
    for file in sorted(_walk_files(path)):
        relative = file.relative_to(path).as_posix()
        hasher.update(relative.encode("utf-8") + b"\0")
        hasher.update(str(file.resolve()).encode("utf-8") + b"\0")
        hasher.update(digest(file).encode("ascii") + b"\0")
        count += 1
    if not count and not allow_empty:
        raise ValueError("empty " + name + " directory: " + str(path))
    return {"path": str(path), "sha256": hasher.hexdigest(), "file_count": count}


def _query_sdk_include_roots(cann):
    # Match the native-query CMake target and its compile/link probe.
    suffixes = ["asc/include", "asc/include/comm_api", "include", "include/hcomm"]
    machine = platform.machine().lower()
    if machine in {"aarch64", "arm64"}:
        suffixes.append("aarch64-linux/pkg_inc")
    elif machine in {"x86_64", "amd64"}:
        suffixes.append("x86_64-linux/pkg_inc")
    suffixes.append("pkg_inc")
    return tuple(cann / suffix for suffix in suffixes if (cann / suffix).is_dir())


def _source_signature(source):
    files = {"CMakeLists.txt": digest(source / "CMakeLists.txt")}
    for directory in ("src", "cmake"):
        path = source / directory
        if path.is_dir():
            for file in sorted(path.rglob("*")):
                if file.is_file():
                    files[file.relative_to(source).as_posix()] = digest(file)
    if not any(name.startswith("src/") for name in files):
        raise ValueError("missing native backend sources: " + str(source / "src"))
    # Changes to construction or timeout handling also invalidate an old build.
    for name in ("scripts/build_backend.py", "scripts/build_package.py"):
        files[name] = digest(source / name)
    return {"path": str(source), "files": files}


def _ccu_hcomm_library(hccl, extra_libraries):
    # An explicit provider takes priority over the SDK default. Inspect both
    # names so a libhcomm.so symlink to a differently named file still works.
    providers = set()
    for library in extra_libraries:
        resolved = library.resolve()
        if any(
            name == "libhcomm.so" or name.startswith("libhcomm.so.")
            for name in (library.name, resolved.name)
        ):
            providers.add(resolved)
    if len(providers) > 1:
        raise ValueError("multiple explicit libhcomm providers were supplied")
    if providers:
        return providers.pop()
    # Keep the HCCL provider and its companion from the same selected SDK.
    candidate = hccl / "lib64/libhcomm.so"
    return candidate if candidate.is_file() else None


def _configuration(
    cann_root,
    vendor_root,
    mpi_root,
    *,
    cmake,
    abi,
    extra_includes,
    extra_libraries,
    hccl_root,
    profile="aicpu",
    query_source="native_mc2",
    query_library=None,
    profiling=False,
    query_only=False,
):
    if profile not in {"aicpu", "ccu"}:
        raise ValueError("backend profile must be aicpu or ccu")
    if str(abi) not in {"0", "1"}:
        raise ValueError("backend ABI must be 0 or 1")
    if query_source not in {"native_mc2", "mock"}:
        raise ValueError("query source must be native_mc2 or mock")
    if query_source == "mock" and query_library is not None:
        raise ValueError("a query library cannot be selected for a query stub")
    if not isinstance(profiling, bool):
        raise ValueError("profiling must be a boolean")
    cann = _root(cann_root, "CANN root")
    if not isinstance(query_only, bool) or (query_only and profiling):
        raise ValueError("query_only must be a boolean and cannot enable profiling")
    vendor = None if query_only else _root(vendor_root, "operator vendor root")
    mpi = _root(mpi_root, "MPI root")
    hccl = _root(hccl_root if hccl_root is not None else cann, "HCCL root")
    includes = tuple(_root(path, "extra include") for path in extra_includes)
    requested_libraries = tuple(Path(path).expanduser() for path in extra_libraries)
    libraries = tuple(path.resolve() for path in requested_libraries)
    for path in (*includes, *libraries):
        if ";" in str(path):
            raise ValueError("CMake list paths cannot contain ';': " + str(path))
    op_include = None if query_only else vendor / "op_api/include"
    if not query_only:
        for name in CCU_GENERATED_HEADERS if profile == "ccu" else GENERATED_HEADERS:
            _file(op_include / name, "generated operator header")
    required_headers = (
        ["acl/acl.h"] if query_only else ["acl/acl.h", "aclnn/acl_meta.h"]
    )
    if profiling:
        required_headers.append("acl/acl_prof.h")
    for name in required_headers:
        if not any(
            (directory / name).is_file() for directory in (cann / "include", *includes)
        ):
            raise ValueError("missing SDK header: " + name)
    if not any(
        (directory / "hccl/hccl.h").is_file()
        for directory in (hccl / "include", *includes)
    ):
        raise ValueError("missing HCCL header: hccl/hccl.h")
    if (
        not query_only
        and profile == "ccu"
        and not any(
            (directory / "hccl/hccl_res.h").is_file()
            for directory in (hccl / "include", *includes)
        )
    ):
        raise ValueError(
            "missing HCCL header required by CCU window access: hccl/hccl_res.h"
        )
    mpi_include = mpi / "include"
    if not mpi_include.is_dir() or not any(
        path.name == "mpi.h" for path in _walk_files(mpi_include)
    ):
        raise ValueError("missing MPI header under " + str(mpi / "include"))
    tools = {
        "cmake": _tool(cmake, "CMake"),
        "cxx": _tool(os.environ.get("CXX", "c++"), "C++ compiler"),
        "make": _tool("make", "make"),
        "mpicxx": _file(mpi / "bin/mpicxx", "MPI C++ wrapper", executable=True),
        "cann_environment": _file(
            resolve_cann_environment_script(cann), "CANN environment script"
        ),
    }
    for name in ("mpirun", "mpiexec"):
        path = mpi / "bin" / name
        if path.exists():
            tools[name] = _file(path, "MPI " + name, executable=True)
    selected_libraries = {
        "acl": cann / "lib64/libascendcl.so",
        "hccl": hccl / "lib64/libhccl.so",
    }
    if not query_only:
        selected_libraries["operator"] = (
            vendor
            / "op_api/lib"
            / (
                "libasccomm_api_bench_ccu_opapi.so"
                if profile == "ccu"
                else "libcust_opapi.so"
            )
        )
    if profiling:
        selected_libraries["profiler"] = cann / "lib64/libmsprofiler.so"
    query_header = None
    query_include_root = None
    if query_source == "native_mc2":
        query_header = next(
            (
                include_root / relative
                for include_root in (cann / "asc/include", cann / "include")
                for relative in (
                    "adv_api/hccl/hccl_mc2.h",
                    "hccl/hccl_mc2.h",
                    "hccl_mc2.h",
                )
                if (include_root / relative).is_file()
            ),
            None,
        )
        if query_header is None:
            raise ValueError(
                "missing formal MC2 query header under selected CANN asc/include or include: hccl_mc2.h"
            )
        query_include_root = (
            cann / "asc/include"
            if cann / "asc/include" in query_header.parents
            else cann / "include"
        )
        selected_libraries["query"] = (
            Path(query_library).expanduser().resolve()
            if query_library is not None
            else cann / "lib64/libmc2_client.so"
        )
        if ";" in str(selected_libraries["query"]):
            raise ValueError("CMake query library path cannot contain ';'")
    if profile == "ccu":
        hcomm = _ccu_hcomm_library(hccl, requested_libraries)
        if hcomm is not None:
            selected_libraries["hcomm"] = hcomm
    nnopbase = cann / "lib64/libnnopbase.so"
    if not query_only and nnopbase.exists():
        selected_libraries["nnopbase"] = nnopbase
    mpi_libraries = {}
    for directory in (mpi / "lib", mpi / "lib64"):
        if directory.is_dir():
            for path in sorted(_walk_files(directory)):
                if path.name.startswith("libmpi"):
                    mpi_libraries[str(path.relative_to(mpi))] = _file(
                        path, "MPI library"
                    )
    if not mpi_libraries:
        raise ValueError(
            "missing MPI libraries under " + str(mpi / "lib") + " or lib64"
        )
    sdk_headers = _tree(cann / "include", "SDK include")
    query_sdk_headers = (
        sdk_headers
        if query_include_root == cann / "include"
        else _tree(query_include_root, "formal query SDK include")
        if query_include_root is not None
        else None
    )
    query_include_roots = (
        _query_sdk_include_roots(cann) if query_source == "native_mc2" else ()
    )
    query_dependency_headers = []
    for path in query_include_roots:
        # Parent receipts already include descendants; do not hash them twice.
        if any(parent in path.parents for parent in query_include_roots):
            continue
        query_dependency_headers.append(
            sdk_headers
            if path == cann / "include"
            else query_sdk_headers
            if path == query_include_root
            else _tree(path, "query dependency include", allow_empty=True)
        )
    signature = {
        "profile": profile,
        "profiling": profiling,
        "query_source": query_source,
        "query_header": _file(query_header, "formal MC2 query header")
        if query_header is not None
        else None,
        "query_sdk_headers": query_sdk_headers,
        "query_include_roots": [str(path) for path in query_include_roots],
        "query_dependency_headers": query_dependency_headers,
        "source": _source_signature(SOURCE_ROOT),
        "cann_root": str(cann),
        "vendor_root": str(vendor) if vendor is not None else "",
        "mpi_root": str(mpi),
        "hccl_root": str(hccl),
        "abi": str(abi),
        "build_environment": {
            name: os.environ[name]
            for name in BUILD_ENVIRONMENT_KEYS
            if name in os.environ
        },
        "tools": tools,
        "sdk_headers": sdk_headers,
        "hccl_headers": sdk_headers
        if hccl == cann
        else _tree(hccl / "include", "HCCL include"),
        "operator_headers": _tree(op_include, "generated operator include")
        if op_include is not None
        else None,
        "mpi_headers": _tree(mpi / "include", "MPI include"),
        "libraries": {
            key: _file(path, key + " library")
            for key, path in selected_libraries.items()
        },
        "mpi_libraries": mpi_libraries,
        "extra_includes": [_tree(path, "extra include") for path in includes],
        "extra_libraries": [_file(path, "extra library") for path in libraries],
    }
    if query_only:
        signature["query_only"] = True
    return signature


def _commands(signature, build):
    roots = {
        name: signature[name]
        for name in ("cann_root", "hccl_root", "mpi_root", "vendor_root")
    }
    tools = signature["tools"]
    configure = [
        tools["cmake"]["path"],
        "-S",
        str(SOURCE_ROOT),
        "-B",
        str(build),
        "-G",
        "Unix Makefiles",
        "-DBUILD_TESTING=OFF",
        "-DMC2_BENCH_DEVICE_BACKEND="
        + ("OFF" if signature.get("query_only") else "ON"),
        "-DMC2_BENCH_DEVICE_PROFILING=" + ("ON" if signature["profiling"] else "OFF"),
        "-DMC2_BENCH_DEVICE_PROFILER_LIBRARY="
        + signature["libraries"].get("profiler", {}).get("path", ""),
        "-DMC2_BENCH_CCU_BACKEND=" + ("ON" if signature["profile"] == "ccu" else "OFF"),
        "-DMC2_BENCH_MPI=ON",
        "-DMC2_BENCH_HCCL=ON",
        "-DMC2_BENCH_NATIVE_QUERY="
        + ("ON" if signature["query_source"] == "native_mc2" else "OFF"),
        "-DMC2_BENCH_QUERY_LIBRARY="
        + signature["libraries"].get("query", {}).get("path", ""),
        "-DMC2_BENCH_QUERY_INCLUDE_DIR="
        + (
            str(Path(signature["query_header"]["path"]).parent)
            if signature["query_header"] is not None
            else ""
        ),
        "-DCMAKE_CXX_COMPILER=" + tools["cxx"]["path"],
        "-DCMAKE_MAKE_PROGRAM=" + tools["make"]["path"],
        "-DMPI_CXX_COMPILER=" + tools["mpicxx"]["path"],
        "-DMC2_BENCH_SDK_ROOT=" + roots["cann_root"],
        "-DMC2_BENCH_HCCL_ROOT=" + roots["hccl_root"],
        "-DMC2_BENCH_HCCL_LIBRARY=" + signature["libraries"]["hccl"]["path"],
        "-DMC2_BENCH_DEVICE_HCOMM_LIBRARY="
        + signature["libraries"].get("hcomm", {}).get("path", ""),
        "-DMC2_BENCH_DEVICE_OP_INCLUDE="
        + (
            str(Path(roots["vendor_root"]) / "op_api/include")
            if roots["vendor_root"]
            else ""
        ),
        "-DMC2_BENCH_DEVICE_OP_LIBRARY="
        + signature["libraries"].get("operator", {}).get("path", ""),
        "-DMC2_BENCH_DEVICE_ACL_LIBRARY=" + signature["libraries"]["acl"]["path"],
        "-DMC2_BENCH_CXX11_ABI=" + signature["abi"],
    ]
    for suffix, key in (
        ("INCLUDES", "extra_includes"),
        ("LIBRARIES", "extra_libraries"),
    ):
        value = ";".join(item["path"] for item in signature[key])
        if suffix == "INCLUDES":
            configure.append("-DMC2_BENCH_SDK_EXTRA_INCLUDES=" + value)
        else:
            configure.append("-DMC2_BENCH_DEVICE_EXTRA_LIBRARIES=" + value)
        configure.append("-DMC2_BENCH_HCCL_EXTRA_" + suffix + "=" + value)
    query_dependencies = [signature["libraries"]["hccl"]["path"]]
    if "hcomm" in signature["libraries"]:
        query_dependencies.append(signature["libraries"]["hcomm"]["path"])
    query_dependencies.extend(item["path"] for item in signature["extra_libraries"])
    configure.append(
        "-DMC2_BENCH_QUERY_EXTRA_LIBRARIES="
        + ";".join(dict.fromkeys(query_dependencies))
    )
    configure.append(
        "-DMC2_BENCH_QUERY_EXTRA_INCLUDES="
        + ";".join(
            dict.fromkeys(
                [
                    *signature["query_include_roots"],
                    str(Path(roots["hccl_root"]) / "include"),
                ]
            )
        )
    )
    if signature.get("query_only"):
        configure.append("-DMC2_BENCH_QUERY_ONLY=ON")
    compile_command = [
        tools["cmake"]["path"],
        "--build",
        str(build),
        "--target",
        QUERY_BACKEND_NAME if signature.get("query_only") else BACKEND_NAME,
        "--parallel",
        str(os.cpu_count() or 1),
    ]
    return configure, compile_command


def _shell_command(signature, commands):
    tools = signature["tools"]
    # Names prefixed with mc2_backend_ and immutable arrays survive common SDK
    # setup variables and positional-argument changes. Paths are shell-quoted.
    path_prefix = [
        str(Path(tools[name]["path"]).parent)
        for name in ("mpicxx", "cmake", "cxx", "make")
    ]
    path_prefix.extend(
        (signature["cann_root"] + "/bin", os.environ.get("PATH", os.defpath))
    )
    library_prefix = [
        signature["hccl_root"] + "/lib64",
        signature["cann_root"] + "/lib64",
        signature["mpi_root"] + "/lib64",
        signature["mpi_root"] + "/lib",
    ]
    if signature["vendor_root"]:
        library_prefix.insert(0, signature["vendor_root"] + "/op_api/lib")
    if "query" in signature["libraries"]:
        query_directory = str(Path(signature["libraries"]["query"]["path"]).parent)
        if query_directory not in library_prefix:
            library_prefix.insert(0, query_directory)
    script = [
        "set -euo pipefail",
        "readonly mc2_backend_cann=" + shlex.quote(signature["cann_root"]),
        "readonly mc2_backend_hccl=" + shlex.quote(signature["hccl_root"]),
        "readonly mc2_backend_vendor=" + shlex.quote(signature["vendor_root"]),
        "readonly mc2_backend_mpi=" + shlex.quote(signature["mpi_root"]),
        "readonly mc2_backend_cxx=" + shlex.quote(tools["cxx"]["path"]),
        "readonly mc2_backend_path=" + shlex.quote(":".join(path_prefix)),
        "readonly mc2_backend_libraries=" + shlex.quote(":".join(library_prefix)),
    ]
    for index, command in enumerate(commands):
        script.append(
            "readonly -a mc2_backend_command_"
            + str(index)
            + "=("
            + " ".join(shlex.quote(arg) for arg in command)
            + ")"
        )
    script.extend(
        [
            "set +eu",
            "set --",
            "source " + shlex.quote(tools["cann_environment"]["path"]) + " </dev/null",
            "mc2_backend_environment_status=$?",
            "set -euo pipefail",
            'if [[ "${mc2_backend_environment_status}" -ne 0 ]]; then echo "[ERROR] Failed to source selected CANN environment" >&2; exit "${mc2_backend_environment_status}"; fi',
            'export ASCEND_HOME_PATH="${mc2_backend_cann}" ASCEND_CANN_PACKAGE_PATH="${mc2_backend_cann}"',
            'export ASCEND_OPP_PATH="${mc2_backend_cann}/opp" ASCEND_CUSTOM_OPP_PATH="${mc2_backend_vendor}"',
            'export MPI_HOME="${mc2_backend_mpi}" HCCL_DIR="${mc2_backend_hccl}" CXX="${mc2_backend_cxx}"',
            'export PATH="${mc2_backend_path}:${PATH:-}"',
            'export LD_LIBRARY_PATH="${mc2_backend_libraries}:${LD_LIBRARY_PATH:-}"',
        ]
    )
    for index in range(len(commands)):
        script.append('"${mc2_backend_command_' + str(index) + '[@]}"')
    return ["bash", "-c", "\n".join(script)]


def build_native_backend(
    cann_root,
    vendor_root,
    mpi_root,
    build_dir,
    *,
    cmake="cmake",
    abi="0",
    extra_includes=(),
    extra_libraries=(),
    timeout=1800,
    hccl_root=None,
    profile="aicpu",
    query_source="native_mc2",
    query_library=None,
    profiling=False,
    query_only=False,
):
    """Compile and return a fresh native backend, preserving logs on failure.

    extra_includes supplies both SDK/device and HCCL include paths;
    extra_libraries supplies both the HCCL link probe and device link target.
    CCU also links libhcomm from the selected HCCL SDK when available, unless
    extra_libraries explicitly selects a different libhcomm provider.
    These are explicit file/directory paths, rather than compiler flags.
    The caller must keep the installed operator package locked throughout.
    """
    if (
        not isinstance(timeout, (int, float))
        or not math.isfinite(timeout)
        or timeout <= 0
    ):
        raise ValueError("backend build timeout must be finite and positive")
    build = Path(build_dir).expanduser().resolve()
    if build == SOURCE_ROOT or build in SOURCE_ROOT.parents:
        raise ValueError("backend build directory must not contain its source")
    for protected in (
        SOURCE_ROOT / "src",
        SOURCE_ROOT / "cmake",
        cann_root,
        vendor_root,
        mpi_root,
        hccl_root,
    ):
        if protected is None:
            continue
        path = Path(protected).expanduser().resolve()
        if build == path or path in build.parents or build in path.parents:
            raise ValueError(
                "backend build directory overlaps source or installed dependencies: "
                + str(build)
            )
    build.mkdir(parents=True, exist_ok=False)
    manifest = {
        "schema_version": 1,
        "kind": "native_backend",
        "status": "building",
        "stage": "preflight",
        "started_at": datetime.now(timezone.utc).isoformat(),
    }
    receipt = build / "build_manifest.json"
    log = build / "build.log"
    atomic_json(receipt, manifest)
    try:
        signature = _configuration(
            cann_root,
            vendor_root,
            mpi_root,
            cmake=cmake,
            abi=abi,
            extra_includes=extra_includes,
            extra_libraries=extra_libraries,
            hccl_root=hccl_root,
            profile=profile,
            query_source=query_source,
            query_library=query_library,
            profiling=profiling,
            query_only=query_only,
        )
        commands = _commands(signature, build)
        manifest.update(signature=signature, commands=commands, stage="compile")
        atomic_json(receipt, manifest)
        result = run_build(
            _shell_command(signature, commands),
            SOURCE_ROOT,
            dict(os.environ),
            log,
            timeout,
        )
        manifest["returncode"] = result
        if result:
            raise RuntimeError("native backend build failed; see " + str(log))
        output = build / (QUERY_BACKEND_NAME if query_only else BACKEND_NAME)
        manifest["stage"] = "validation"
        if output.is_symlink():
            raise ValueError("native backend output must be a regular executable")
        binary = _file(output, "native backend executable", executable=True)
        # A changed source or installed SDK during compilation must never yield
        # a receipt that looks reusable for one of the two configurations.
        if signature != _configuration(
            cann_root,
            vendor_root,
            mpi_root,
            cmake=cmake,
            abi=abi,
            extra_includes=extra_includes,
            extra_libraries=extra_libraries,
            hccl_root=hccl_root,
            profile=profile,
            query_source=query_source,
            query_library=query_library,
            profiling=profiling,
            query_only=query_only,
        ):
            raise ValueError(
                "backend dependencies changed during build; rebuild with a stable installation"
            )
        manifest.update(status="built", stage="completed", backend=binary)
    except BaseException as error:
        manifest.update(status="failed", error=str(error))
        with log.open("a", encoding="utf-8") as stream:
            stream.write("\n[ERROR] " + str(error) + "\n")
        raise
    finally:
        manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
        atomic_json(receipt, manifest)
    return output


def verify_native_backend(
    cann_root,
    vendor_root,
    mpi_root,
    build_dir,
    *,
    cmake="cmake",
    abi="0",
    extra_includes=(),
    extra_libraries=(),
    hccl_root=None,
    profile="aicpu",
    query_source="native_mc2",
    query_library=None,
    profiling=False,
    query_only=False,
):
    """Return a completed backend only when all build inputs still match."""
    build = Path(build_dir).expanduser().resolve()
    receipt = build / "build_manifest.json"
    if receipt.is_symlink() or not receipt.is_file():
        raise ValueError(
            "native backend reuse requires its build_manifest.json: " + str(receipt)
        )
    try:
        manifest = json.loads(receipt.read_text(encoding="utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(
            "invalid native backend build receipt: " + str(receipt)
        ) from error
    if (
        not isinstance(manifest, dict)
        or manifest.get("schema_version") != 1
        or manifest.get("kind") != "native_backend"
        or manifest.get("status") != "built"
    ):
        raise ValueError(
            "native backend has no completed build receipt: " + str(receipt)
        )
    signature = _configuration(
        cann_root,
        vendor_root,
        mpi_root,
        cmake=cmake,
        abi=abi,
        extra_includes=extra_includes,
        extra_libraries=extra_libraries,
        hccl_root=hccl_root,
        profile=profile,
        query_source=query_source,
        query_library=query_library,
        profiling=profiling,
        query_only=query_only,
    )
    if signature != manifest.get("signature"):
        raise ValueError(
            "native backend build inputs changed; run without --skip-build to rebuild"
        )
    output = build / (QUERY_BACKEND_NAME if query_only else BACKEND_NAME)
    if output.is_symlink() or manifest.get("backend") != _file(
        output, "native backend executable", executable=True
    ):
        raise ValueError(
            "native backend digest does not match its build receipt; rebuild"
        )
    return output
