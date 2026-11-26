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

"""AICPU package deployment migrated from hccl_api_test.

Source: main@493be569dcaa2d7a4c2cb8892e950c03588c7243,
scripts/benchmark_deploy.py. The process identity, guarded lock reclamation,
backup, serialized SSH agent and content verification protocol are retained.
This adaptation installs only asccomm_api_bench operator packages: it removes the
HCCL comparison path and legacy runtime scripts, installs the primary package
inside the same protocol, and validates runtime-algorithm package markers.
"""

import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import uuid


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def save(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)


def command(args, timeout=120):
    result = subprocess.run(
        args,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
    )
    if result.returncode:
        raise RuntimeError(
            f"{shlex.join(map(str, args))}: exit {result.returncode}\n{result.stdout}"
        )
    return result.stdout


def dependencies(binary):
    output = command(["ldd", str(binary)])
    if "not found" in output:
        raise RuntimeError(f"unresolved dependency for {binary}: {output}")
    paths = set()
    for line in output.splitlines():
        value = line.partition("=>")[2].strip() if "=>" in line else line.strip()
        value = value.split(" (", 1)[0].strip()
        if value.startswith("/"):
            paths.add(value)
    if not paths:
        raise RuntimeError(
            f"cannot identify dynamic dependencies for {binary}: {output}"
        )
    return {p: digest(p) for p in sorted(paths)}


SOURCE_COMMIT = "493be569dcaa2d7a4c2cb8892e950c03588c7243"
PACKAGE_MARKER = "asccomm_api_bench_package.info"
CCU_PACKAGE_MARKER = "asccomm_api_bench_ccu_package.info"
CCU_MARKER_FIELDS = {
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


def package_profile(config):
    profile = config.get("package_profile", "aicpu")
    if profile not in ("aicpu", "ccu"):
        raise ValueError("unsupported package deployment profile: " + str(profile))
    if profile == "ccu" and config["measurement_mode"] != "full":
        raise ValueError("CCU deployment requires measurement_mode full")
    return profile


def package_marker(config):
    return CCU_PACKAGE_MARKER if package_profile(config) == "ccu" else PACKAGE_MARKER


def roots(config):
    # The only shared mutable destination is the operator vendor directory.
    # Keep the original lock name so the old and new tools cannot change the
    # same vendor concurrently.
    return [Path(config["vendor"]).parent / ".benchmark-custom-op.lock"]


def host_identity():
    """Linux identity; unsupported/inaccessible identity is never considered dead."""
    try:
        return {
            "host": platform.node(),
            "machine": Path("/etc/machine-id").read_text().strip(),
            "boot": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
            "pidns": os.readlink("/proc/self/ns/pid"),
        }
    except OSError:
        return {"host": platform.node()}


def read_process(pid):
    # comm may contain spaces and parentheses. Fields after its final ')' start at state.
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return {"state": fields[0], "pgid": int(fields[2]), "start": fields[19]}


def process_identity(pid, endpoint):
    identity = dict(host_identity(), pid=pid, endpoint=endpoint)
    try:
        identity.update(read_process(pid))
    except (OSError, ValueError, IndexError):
        pass
    return identity


def process_table_visible():
    # hidepid can silently omit other users' processes, making an empty group inconclusive.
    try:
        for line in Path("/proc/mounts").read_text().splitlines():
            fields = line.split()
            if len(fields) >= 4 and fields[1] == "/proc":
                return not any(
                    option.startswith("hidepid=")
                    and option not in ("hidepid=0", "hidepid=off")
                    for option in fields[3].split(",")
                )
    except OSError:
        pass
    return False


def local_process_status(identity):
    try:
        here = host_identity()
        if any(
            not identity.get(k) or identity[k] != here.get(k)
            for k in ("host", "machine")
        ):
            return "unknown"
        if not identity.get("boot") or not here.get("boot"):
            return "unknown"
        if identity["boot"] != here["boot"]:
            return "dead"  # same machine, previous boot
        if identity.get("pidns") != here.get("pidns") or not identity.get("start"):
            return "unknown"
        try:
            process = read_process(identity["pid"])
        except FileNotFoundError:
            process = None
        if (
            process
            and process["start"] == identity["start"]
            and process["state"] not in ("Z", "X")
        ):
            return "alive"
        if not process_table_visible():
            return "unknown"
        # A dead parent may leave compilers, installers or MPI children running.
        # A reused group ID is conservatively treated as busy as well.
        for entry in Path("/proc").iterdir():
            if not entry.name.isdecimal():
                continue
            try:
                child = read_process(int(entry.name))
            except FileNotFoundError:
                continue
            if child["pgid"] == identity["pgid"] and child["state"] not in ("Z", "X"):
                return "busy"
        return "dead"
    except (OSError, ValueError, KeyError, IndexError):
        return "unknown"


def process_status(identity):
    if not isinstance(identity, dict):
        return "unknown"
    if identity.get("host") == platform.node():
        return local_process_status(identity)
    endpoint = identity.get("endpoint")
    if not endpoint or endpoint.startswith("-"):
        return "unknown"
    # Query the original host, never interpret its PID in another machine's /proc.
    source = globals().get("AGENT_SOURCE") or Path(__file__).read_text()
    remote = "python3 -c " + shlex.quote(source) + " --process-status"
    try:
        result = subprocess.run(
            ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", endpoint, remote],
            input=json.dumps(identity),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=15,
        )
        if result.returncode == 0:
            for line in reversed(result.stdout.splitlines()):
                if line.startswith("PROCESS_STATUS="):
                    status = line.partition("=")[2]
                    return status if status in ("alive", "busy", "dead") else "unknown"
    except (OSError, subprocess.TimeoutExpired):
        pass
    return "unknown"


@contextmanager
def lock_guard(lock):
    # This inode must never be deleted: concurrent reclaimers must flock the same file.
    lock.parent.mkdir(parents=True, exist_ok=True)
    with lock.with_name(lock.name + ".guard").open("a") as guard:
        fcntl.flock(guard.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(guard.fileno(), fcntl.LOCK_UN)


def read_owner(lock):
    try:
        owner = json.loads((lock / "owner").read_text())
        if isinstance(owner, dict) and owner.get("version") == 2:
            return owner
    except (OSError, ValueError):
        pass
    return None


def inactive_markers(lock):
    for active in lock.glob("active-*"):
        try:
            identity = json.loads(active.read_text())
        except (OSError, ValueError):
            return False
        if process_status(identity) != "dead":
            return False
    return True


def acquire_one(config, lock):
    # Caller holds the persistent guard across inspection, reclaim and creation.
    controller = config.get("controller")
    if controller and process_status(controller) in ("dead", "busy"):
        raise RuntimeError(
            "deployment controller has exited; refusing late lock acquisition"
        )
    if lock.exists():
        owner = read_owner(lock)
        if owner and owner["token"] == config["token"]:
            return
        if (
            not owner
            or process_status(owner.get("controller")) != "dead"
            or not inactive_markers(lock)
        ):
            raise RuntimeError(
                f"deployment lock is held: {lock}; owner alive or unknown "
                "(legacy locks require manual inspection)"
            )
        archived = lock.with_name(lock.name + ".stale-" + uuid.uuid4().hex)
        lock.rename(archived)
        print(
            f"[deploy] reclaimed inactive lock {lock}; retained metadata={archived}",
            flush=True,
        )
    pending = lock.with_name(lock.name + ".pending-" + uuid.uuid4().hex)
    pending.mkdir()
    try:
        save(
            pending / "owner",
            {
                "version": 2,
                "token": config["token"],
                "controller": controller,
                "run": config["run"],
            },
        )
        pending.rename(
            lock
        )  # Publish a complete owner record, even if killed during creation.
    finally:
        if pending.exists():
            shutil.rmtree(pending)


def acquire(config):
    for lock in roots(config):
        with lock_guard(lock):
            acquire_one(config, lock)


def release(config):
    for lock in roots(config):
        with lock_guard(lock):
            owner = read_owner(lock)
            if not owner or owner["token"] != config["token"]:
                continue
            if not inactive_markers(lock):
                raise RuntimeError(
                    f"active or interrupted deployment at {lock}; owner alive or unknown"
                )
            for active in lock.glob("active-*"):
                active.unlink()
            (lock / "owner").unlink()
            lock.rmdir()


def make_config(
    *,
    run_dir,
    hosts,
    ssh_user,
    cann_root,
    vendor_root,
    installer_path=None,
    measurement_mode="full",
    run_id,
    binaries=(),
    package_profile="aicpu",
):
    """Create a serializable deployment configuration; host zero is local.

    Hosts must have the same absolute CANN, vendor and output paths. This is
    the original benchmark's explicit-environment contract, not an SDK sync.
    ``binaries`` optionally checks already deployed executables with ldd.
    """
    import re

    names = [getattr(host, "name", host) for host in hosts]
    if not names or any(
        not isinstance(host, str)
        or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", host)
        for host in names
    ):
        raise ValueError("hosts must contain plain hostnames or IPv4 addresses")
    if len(set(host.rstrip(".").casefold() for host in names)) != len(names):
        raise ValueError("duplicate deployment host")
    if not isinstance(ssh_user, str) or not re.fullmatch(
        r"[A-Za-z_][A-Za-z0-9_.-]*\$?", ssh_user
    ):
        raise ValueError("invalid SSH user")
    if not isinstance(run_id, str) or not re.fullmatch(
        r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}", run_id
    ):
        raise ValueError("invalid deployment run_id")
    if measurement_mode not in ("full", "comm_only"):
        raise ValueError("measurement_mode must be full or comm_only")
    config = {
        "run": str(Path(run_dir).absolute()),
        "hosts": names,
        "user": ssh_user,
        "cann": str(Path(cann_root).absolute()),
        "vendor": str(Path(vendor_root).absolute()),
        "installer": str(Path(installer_path).absolute()) if installer_path else None,
        "measurement_mode": measurement_mode,
        "token": run_id,
        "binaries": [str(Path(binary).absolute()) for binary in binaries],
    }
    if package_profile != "aicpu":
        config["package_profile"] = package_profile
    install_root(config)
    for key in ("cann", "vendor"):
        if any(character.isspace() or character in "*?[]" for character in config[key]):
            raise ValueError(
                "migrated installer requires CANN/vendor installation paths "
                "without whitespace or glob characters: " + config[key]
            )
    config["controller"] = process_identity(os.getpid(), ssh_user + "@" + names[0])
    return config


def install_root(config):
    vendor = Path(config["vendor"])
    name = "asccomm_api_bench_ccu" if package_profile(config) == "ccu" else "customize"
    if not vendor.is_absolute() or vendor.parts[-2:] != ("vendors", name):
        raise RuntimeError(
            "custom-op vendor must end with /vendors/" + name + ": " + str(vendor)
        )
    return vendor.parent.parent


def marker_values(path, measurement_mode, profile="aicpu", *, allow_archived_ccu=False):
    marker = Path(path)
    if marker.is_symlink() or not marker.is_file():
        raise RuntimeError(f"missing regular package marker: {marker}")
    values = {}
    for line in marker.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if not separator or not key or key in values:
            raise RuntimeError("invalid or duplicate package marker field: " + line)
        values[key] = value
    expected = (
        CCU_MARKER_FIELDS
        if profile == "ccu"
        else {
            "package": "asccomm_api_bench",
            "mode": "aicpu_ts",
            "measurement_mode": measurement_mode,
            "algorithm": "runtime",
            "operator": "all",
            "runtime_algorithm_attr": "1",
            "kernel_iterations": "1",
            "schema": "1",
            "source_commit": SOURCE_COMMIT,
            "commit_delay_nops": "5000000" if measurement_mode == "comm_only" else "0",
        }
    )
    if profile == "ccu" and allow_archived_ccu and values.get("schema") == "2":
        # Offline analysis may read an old verified run; deployment stays strict.
        expected = {
            **CCU_MARKER_FIELDS,
            "schema": "2",
            "report_layout": "magic,status,wait_begin_cycle,wait_end_cycle",
        }
    if any(values.get(key) != value for key, value in expected.items()):
        raise RuntimeError(
            f"installed package marker mismatch: {values}, expected {expected}"
        )
    return values


def artifact_snapshot(config):
    vendor = Path(config["vendor"])
    if not vendor.is_dir() or vendor.is_symlink():
        raise RuntimeError(f"expected a real vendor directory: {vendor}")
    profile = package_profile(config)
    marker_values(vendor / package_marker(config), config["measurement_mode"], profile)
    paths = sorted(path for path in vendor.rglob("*") if path.is_file())
    library = (
        "libasccomm_api_bench_ccu_opapi.so" if profile == "ccu" else "libcust_opapi.so"
    )
    if not any(path.name == library for path in paths):
        raise RuntimeError("installed package has no " + library)
    # Compare the entire installed package, not just the marker or installer.
    return {str(path.relative_to(vendor)): digest(path) for path in paths}


def artifact_info(config):
    """Return installed content plus the original marker bytes for offline audit."""
    snapshot = artifact_snapshot(config)
    marker_name = package_marker(config)
    marker_path = Path(config["vendor"]) / marker_name
    marker_bytes = marker_path.read_bytes()
    if hashlib.sha256(marker_bytes).hexdigest() != snapshot[marker_name]:
        raise RuntimeError("installed package marker changed during snapshot")
    marker_text = marker_bytes.decode("utf-8")
    return {
        "vendor_files": snapshot,
        "marker": dict(line.split("=", 1) for line in marker_text.splitlines()),
        "marker_text": marker_text,
    }


def receipt_path(config):
    return Path(config["vendor"]).parent / (
        ".asccomm-api-bench-installed-" + config["token"]
    )


def agent(request):
    config = request["config"]
    if request["action"] not in ("install", "backup"):
        return agent_operation(request)
    markers = []
    identity = process_identity(os.getpid(), config.get("agent_endpoint", ""))
    try:
        for lock in roots(config):
            with lock_guard(lock):
                acquire_one(config, lock)
                active = lock / ("active-" + uuid.uuid4().hex)
                save(active, identity)
                markers.append((lock, active))
        return agent_operation(request)
    finally:
        for lock, active in markers:
            with lock_guard(lock):
                active.unlink(missing_ok=True)


def agent_operation(request):
    config, action = request["config"], request["action"]
    stage = Path(config["run"]) / "deploy/staging/api"
    if action == "probe":
        tools = []
        if not request.get("verify_existing") and len(config["hosts"]) > 1:
            tools.append("rsync")
        if config["binaries"]:
            tools.append("ldd")
        for tool in tools:
            if not shutil.which(tool):
                raise RuntimeError(f"missing {tool}")
        for path in (Path(config["cann"]), install_root(config)):
            if not path.is_dir():
                raise RuntimeError(f"CANN directory not found: {path}")
        output = Path(config["run"]).parent
        output.mkdir(parents=True, exist_ok=True)
        if not os.access(output, os.W_OK):
            raise RuntimeError(f"not a writable directory: {output}")
        existing = Path(config["vendor"]).parent
        while not existing.exists():
            existing = existing.parent
        if not os.access(existing, os.W_OK):
            raise RuntimeError(f"not a writable vendor parent: {existing}")
        free = shutil.disk_usage(output).free
        if free < 1024**3:
            raise RuntimeError("less than 1 GiB free for deployment")
        return {"free_bytes": free}
    if action == "release":
        release(config)
        return {"released": True}
    if action == "lock":
        acquire(config)
        stage.mkdir(parents=True, exist_ok=True)
        return {"locked": True}
    if action == "backup":
        acquire(config)
        target = Path(config["vendor"])
        if target.is_symlink():
            raise RuntimeError(f"refusing to replace symlink target: {target}")
        # The backup token lives on the destination FS: shared vendors get one backup.
        backup = target.parent / (
            "." + target.name + ".benchmark-backup-" + config["token"]
        )
        if backup.exists():
            if (
                not (backup / "complete").is_file()
                or (backup / "complete").read_text() != config["token"]
            ):
                raise RuntimeError(f"incomplete backup: {backup}")
            return {"backups": []}
        target.parent.mkdir(parents=True, exist_ok=True)
        backup.mkdir()
        if target.is_dir():
            needed = sum(
                path.stat().st_size for path in target.rglob("*") if path.is_file()
            )
            if shutil.disk_usage(target.parent).free < needed * 2 + 1024**3:
                raise RuntimeError(f"insufficient backup/install space for {target}")
            shutil.copytree(target, backup / "original", symlinks=True)
        elif target.exists():
            raise RuntimeError(f"expected a vendor directory: {target}")
        (backup / "complete").write_text(config["token"])
        return {"backups": [str(backup)]}
    if action == "check_staging_space":
        if shutil.disk_usage(stage).free < request.get("payload_bytes", 0) + 1024**3:
            raise RuntimeError(
                "insufficient staging space for artifacts plus 1 GiB reserve"
            )
        return {"space_available": True}
    if action == "snapshot":
        return artifact_info(config)
    if action in ("install", "verify"):
        manifest = request["manifest"]
        if action == "install":
            acquire(config)
            for item in manifest["files"]:
                if digest(stage / item["payload"]) != item["sha256"]:
                    raise RuntimeError(
                        f"transferred artifact checksum mismatch: {item['payload']}"
                    )
            installer_hash = manifest["installer_sha256"]
            receipt = receipt_path(config)
            if receipt.exists():
                installed = json.loads(receipt.read_text())
                if (
                    installed.get("token") != config["token"]
                    or installed.get("installer_sha256") != installer_hash
                ):
                    raise RuntimeError("shared vendor installation receipt mismatch")
                if artifact_snapshot(config) != installed.get("vendor_files"):
                    raise RuntimeError(
                        "shared vendor content differs from installation receipt"
                    )
            else:
                installer = stage / "custom.run"
                installer.chmod(0o755)
                if package_profile(config) == "ccu":
                    # The isolated installer deliberately refuses to overwrite
                    # an existing vendor. Only the guarded deployment may move
                    # it aside, after preserving a completed backup.
                    target = Path(config["vendor"])
                    backup = target.parent / (
                        "." + target.name + ".benchmark-backup-" + config["token"]
                    )
                    if target.exists() or target.is_symlink():
                        if target.is_symlink() or not target.is_dir():
                            raise RuntimeError("refusing a non-directory CCU vendor")
                        if (
                            not (backup / "complete").is_file()
                            or (backup / "complete").read_text() != config["token"]
                        ):
                            raise RuntimeError(
                                "CCU replacement requires a completed guarded backup"
                            )
                        replaced = backup / "replaced"
                        if replaced.exists() or replaced.is_symlink():
                            raise RuntimeError(
                                "interrupted CCU replacement requires inspection: "
                                + str(replaced)
                            )
                        target.rename(replaced)
                print(
                    command(
                        [str(installer), "--install-path=" + str(install_root(config))],
                        timeout=1800,
                    ),
                    flush=True,
                )
                snapshot = artifact_snapshot(config)
                if (
                    manifest.get("vendor_files") is not None
                    and snapshot != manifest["vendor_files"]
                ):
                    raise RuntimeError(
                        "installed vendor files differ from primary node"
                    )
                save(
                    receipt,
                    {
                        "token": config["token"],
                        "installer_sha256": installer_hash,
                        "vendor_files": snapshot,
                    },
                )
        info = artifact_info(config)
        snapshot = info["vendor_files"]
        if (
            manifest.get("vendor_files") is not None
            and snapshot != manifest["vendor_files"]
        ):
            raise RuntimeError("installed vendor content mismatch")
        for binary in config["binaries"]:
            if not os.access(binary, os.X_OK):
                raise RuntimeError(f"not executable: {binary}")
            dependencies(binary)
        return {"verified": "api", **info}
    raise ValueError(f"unknown agent action {action}")


def invoke(config, index, action, log_name, **kwargs):
    host = config["hosts"][index]
    log = Path(config["run"]) / "deploy" / f"node_{index}" / log_name
    log.parent.mkdir(parents=True, exist_ok=True)
    agent_config = dict(config, agent_endpoint=config["user"] + "@" + host)
    request = {"config": agent_config, "action": action, **kwargs}
    source = Path(__file__).read_text()
    request["source"] = source
    # Cleanup remains available even when an interrupted environment setup fails.
    environment = (
        ""
        if action == "release"
        else f"""if [ -f {shlex.quote(config["cann"] + "/set_env.sh")} ]; then
    source {shlex.quote(config["cann"] + "/set_env.sh")} </dev/null
elif [ -f {shlex.quote(str(Path(config["cann"]).parent / "set_env.sh"))} ]; then
    source {shlex.quote(str(Path(config["cann"]).parent / "set_env.sh"))} </dev/null
else
    echo 'CANN set_env.sh not found' >&2; exit 2
fi
export ASCEND_HOME_PATH={shlex.quote(config["cann"])}
export ASCEND_CANN_PACKAGE_PATH={shlex.quote(config["cann"])}
export ASCEND_OPP_PATH={shlex.quote(config["cann"] + "/opp")}
export ASCEND_CUSTOM_OPP_PATH={shlex.quote(config["vendor"])}
"""
    )
    env_script = f"set -e\n{environment}exec python3 -c {shlex.quote(source)} --agent\n"
    args = (
        ["bash", "-c", env_script]
        if index == 0
        else [
            "ssh",
            "-o",
            "BatchMode=yes",
            "-o",
            "ConnectTimeout=10",
            config["user"] + "@" + host,
            "bash -c " + shlex.quote(env_script),
        ]
    )
    print(f"[deploy] node={host} starting {action}; log={log}", flush=True)
    with log.open("a") as out:
        try:
            result = subprocess.run(
                args,
                input=json.dumps(request),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=2100,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            out.write(f"{action} transport error: {exc}\n")
            out.flush()
            raise RuntimeError(
                f"{host} {action} transport failed; remote state must be checked: {log}"
            ) from exc
        out.write(result.stdout)
    print(
        f"[deploy] node={host} action={action} exit={result.returncode} log={log}",
        flush=True,
    )
    if result.returncode:
        raise RuntimeError(
            f"{host} {action} failed; see {log}: {result.stdout[-2000:]}"
        )
    line = next(
        (
            line
            for line in reversed(result.stdout.splitlines())
            if line.startswith("DEPLOY_RESULT=")
        ),
        None,
    )
    if line is None:
        raise RuntimeError(f"{host}: missing deployment result")
    data = json.loads(line.partition("=")[2])
    for backup in data.get("backups", ()):
        print(f"[deploy] node={host} retained backup={backup}", flush=True)
    return data


def prepare(config, *, verify_existing=False, on_progress=None):
    """Probe every node before acquiring locks or preserving existing vendors."""
    install_root(config)

    def step(index, action, status, **kwargs):
        try:
            invoke(config, index, action, "preflight.log", **kwargs)
        except BaseException as exc:
            if on_progress:
                on_progress(index, "failed: " + str(exc))
            raise
        if on_progress:
            on_progress(index, status)

    for index in range(len(config["hosts"])):
        step(index, "probe", "preflight_ok", verify_existing=verify_existing)
    for index in range(len(config["hosts"])):
        step(index, "lock", "locked")
    if not verify_existing:
        for index in range(len(config["hosts"])):
            step(index, "backup", "backed_up")


def release_all(config):
    errors = []
    for index in range(len(config["hosts"])):
        try:
            invoke(config, index, "release", "preflight.log")
        except Exception as exc:
            errors.append(str(exc))
    if errors:
        raise RuntimeError("; ".join(errors))


class Deployment:
    """Verify all nodes before launch; keep installation locks until exit.

    __enter__ probes, locks and backs up all nodes before the caller builds.
    Call install(installer_path) or verify_existing() before starting MPI.
    Backups and failure evidence are retained; interrupted installer state is
    never silently rolled back, and live/unknown locks are never removed.
    """

    def __init__(self, config, *, verify_existing=False):
        self.config = config
        self.existing_only = verify_existing
        self.prepared = False
        self.started = False
        self.path = Path(config["run"]) / "deploy/manifest.json"
        self.manifest = {
            "schema": 1,
            "source_commit": SOURCE_COMMIT,
            "run_id": config["token"],
            "path_type": "api",
            "measurement_mode": config["measurement_mode"],
            "mode": "verify_existing" if verify_existing else "install",
            "status": "pending",
            "mpi_allowed": False,
            "files": [],
            "nodes": {host: "pending" for host in config["hosts"]},
            "node_markers": {},
        }

    def __enter__(self):
        if self.started:
            raise RuntimeError("deployment context cannot be entered twice")
        self.started = True
        save(self.path, self.manifest)
        save(self.path.with_name("state.json"), self.config)
        try:
            prepare(
                self.config,
                verify_existing=self.existing_only,
                on_progress=self._node_status,
            )
            self.prepared = True
            self.manifest["status"] = "prepared"
            save(self.path, self.manifest)
            return self
        except BaseException as exc:
            self.manifest.update(status="failed", mpi_allowed=False, error=str(exc))
            try:
                release_all(self.config)
                self.manifest["locks_released"] = True
            except Exception as release_error:
                self.manifest["release_error"] = str(release_error)
                self.manifest["locks_released"] = False
            save(self.path, self.manifest)
            raise

    def __exit__(self, exc_type, exc_value, traceback):
        if exc_type is not None:
            if not self.manifest["mpi_allowed"]:
                self.manifest.update(status="failed", error=str(exc_value))
            else:
                self.manifest["run_error"] = str(exc_value)
        try:
            release_all(self.config)
            self.manifest["locks_released"] = True
        except Exception as release_error:
            self.manifest["locks_released"] = False
            self.manifest["release_error"] = str(release_error)
            if exc_type is None:
                raise
        finally:
            self.prepared = False
            save(self.path, self.manifest)
        return False

    def _node_status(self, index, status):
        self.manifest["nodes"][self.config["hosts"][index]] = status
        save(self.path, self.manifest)

    def _installed_info(self, index, info):
        manifest = self.manifest
        if index == 0:
            manifest.update(
                {key: info[key] for key in ("vendor_files", "marker", "marker_text")}
            )
        elif any(
            info.get(key) != manifest.get(key)
            for key in ("vendor_files", "marker", "marker_text")
        ):
            raise RuntimeError(
                "installed vendor package or marker differs from primary node"
            )
        manifest["node_markers"][self.config["hosts"][index]] = {
            "sha256": info["vendor_files"][package_marker(self.config)],
            "marker": info["marker"],
        }

    def install(self, installer_path=None):
        """Install the built package on all nodes, then permit the MPI phase."""
        if self.existing_only:
            raise RuntimeError("verify-existing deployment cannot install a package")
        if installer_path is not None:
            self.config["installer"] = str(Path(installer_path).absolute())
            save(self.path.with_name("state.json"), self.config)
        return self._finish(False)

    def verify_existing(self):
        """Compare installed marker and full content without running an installer."""
        return self._finish(True)

    def _finish(self, verify_existing):
        if not self.prepared or self.manifest["status"] != "prepared":
            raise RuntimeError(
                "deployment must be entered and can be verified only once"
            )
        self.manifest["mode"] = "verify_existing" if verify_existing else "install"
        try:
            self._distribute(verify_existing)
            self.manifest.update(status="verified", mpi_allowed=True)
            return self.manifest
        except BaseException as exc:
            self.manifest.update(status="failed", mpi_allowed=False, error=str(exc))
            raise
        finally:
            save(self.path, self.manifest)

    def _transfer(self, index, payload):
        config = self.config
        remote_stage = Path(config["run"]) / "deploy/staging/api"
        args = [
            "rsync",
            "-a",
            "--partial",
            "--protect-args",
            "-e",
            "ssh -o BatchMode=yes -o ConnectTimeout=10",
            str(payload) + "/",
            f"{config['user']}@{config['hosts'][index]}:{remote_stage}/",
        ]
        log = Path(config["run"]) / "deploy" / f"node_{index}" / "transfer.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        try:
            log.write_text(command(args, timeout=600) or "transfer completed\n")
        except Exception as exc:
            log.write_text(str(exc) + "\n")
            raise

    def _distribute(self, verify_existing):
        config, manifest = self.config, self.manifest
        payload = Path(config["run"]) / "deploy/payload/api"
        if not verify_existing:
            installer = Path(config["installer"] or "")
            if not installer.is_file() or installer.is_symlink():
                raise RuntimeError(
                    f"expected a regular custom-op installer: {installer}"
                )
            payload.mkdir(parents=True, exist_ok=True)
            shutil.copy2(installer, payload / "custom.run")
            checksum = digest(payload / "custom.run")
            manifest["installer_sha256"] = checksum
            manifest["files"] = [{"payload": "custom.run", "sha256": checksum}]
            shutil.copytree(
                payload, Path(config["run"]) / "deploy/staging/api", dirs_exist_ok=True
            )
            manifest["vendor_files"] = None
        else:
            try:
                self._installed_info(0, invoke(config, 0, "snapshot", "verify.log"))
            except BaseException as exc:
                self._node_status(0, "failed: " + str(exc))
                raise
        save(self.path, manifest)
        for index, host in enumerate(config["hosts"]):
            try:
                if not verify_existing:
                    if index:
                        invoke(
                            config,
                            index,
                            "check_staging_space",
                            "verify.log",
                            payload_bytes=(payload / "custom.run").stat().st_size,
                        )
                        self._transfer(index, payload)
                    result = invoke(
                        config, index, "install", "install.log", manifest=manifest
                    )
                    if index == 0:
                        self._installed_info(index, result)
                verified = invoke(
                    config, index, "verify", "verify.log", manifest=manifest
                )
                self._installed_info(index, verified)
                manifest["nodes"][host] = "verified"
            except Exception as exc:
                manifest["nodes"][host] = "failed: " + str(exc)
                raise
            finally:
                save(self.path, manifest)


def deploy(config, *, verify_existing=False):
    """Install/verify and release locks; use Deployment to protect a later MPI run."""
    with Deployment(config, verify_existing=verify_existing) as deployment:
        return deployment.verify_existing() if verify_existing else deployment.install()


def main():
    if sys.argv[1:] == ["--process-status"]:
        print("PROCESS_STATUS=" + local_process_status(json.load(sys.stdin)))
        return
    if sys.argv[1:] == ["--agent"]:
        request = json.load(sys.stdin)
        globals()["AGENT_SOURCE"] = request.get("source")
        print("DEPLOY_RESULT=" + json.dumps(agent(request)))
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config", required=True, help="JSON configuration from make_config"
    )
    parser.add_argument("--verify-existing", action="store_true")
    args = parser.parse_args()
    deploy(
        json.loads(Path(args.config).read_text()), verify_existing=args.verify_existing
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"[deploy] ERROR: {error}", file=sys.stderr)
        sys.exit(1)
