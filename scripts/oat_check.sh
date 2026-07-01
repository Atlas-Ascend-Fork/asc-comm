#!/bin/sh
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
# OAT Pre-commit Check Script — Python Edition (oat-py)
# Replaces the Java-binary-based oat_check.sh.
#
# Requires: Python 3.7+  (pip install oat-py>=1.0.1)
# Works on: Linux / macOS / Windows (Git Bash / MSYS2)
#
# Self-healing: strip Windows CRLF if present
_SCRIPT="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
if sed 's/\r//' "$_SCRIPT" 2>/dev/null | diff -q - "$_SCRIPT" >/dev/null 2>&1; then
    :
else
    echo "[OAT] CRLF detected, auto-fixing and re-running..."
    _TMP="${_SCRIPT}.lf"
    sed 's/\r//' "$_SCRIPT" > "$_TMP" && mv "$_TMP" "$_SCRIPT"
    exec sh "$_SCRIPT" "$@"
fi

set -e

_oat_verbose() {
    if [ "${OAT_VERBOSE:-0}" = "1" ]; then
        echo "$@"
    fi
}

# ---------------------------------------------------------------------------
# 0. Locate Python interpreter
# ---------------------------------------------------------------------------
_PYTHON=""
for _candidate in python3 python py; do
    if command -v "$_candidate" >/dev/null 2>&1; then
        _VER=$("$_candidate" -c "import sys; print(sys.version_info >= (3,7))" 2>/dev/null || echo "False")
        if [ "$_VER" = "True" ]; then
            _PYTHON="$_candidate"
            break
        fi
    fi
done

if [ -z "$_PYTHON" ]; then
    echo "[OAT] [WARNING] Python 3.7+ is required but not found. Please install Python 3.7 or later."
    echo "[OAT] Skipping OAT check, continuing commit..."
    exit 0
fi

# ---------------------------------------------------------------------------
# 1. Ensure oat-py is installed
# ---------------------------------------------------------------------------
_OAT_OK=$("$_PYTHON" -c "import importlib.util; print('ok' if importlib.util.find_spec('oat') else 'missing')" 2>/dev/null || echo "missing")
if [ "$_OAT_OK" != "ok" ]; then
    _oat_verbose "[OAT] oat-py not found. Installing oat-py>=1.0.1 ..."
    if ! PIP_ROOT_USER_ACTION=ignore "$_PYTHON" -m pip install --quiet --disable-pip-version-check "oat-py>=1.0.1" >/dev/null 2>&1; then
        echo "[OAT] [WARNING] Failed to install oat-py. Please run: pip install oat-py>=1.0.1"
        echo "[OAT] Skipping OAT check, continuing commit..."
        exit 0
    fi
    _OAT_OK=$("$_PYTHON" -c "import importlib.util; print('ok' if importlib.util.find_spec('oat') else 'missing')" 2>/dev/null || echo "missing")
    if [ "$_OAT_OK" != "ok" ]; then
        echo "[OAT] [WARNING] Failed to install oat-py. Please run: pip install oat-py>=1.0.1"
        echo "[OAT] Skipping OAT check, continuing commit..."
        exit 0
    fi
    _oat_verbose "[OAT] oat-py installed successfully."
fi

# ---------------------------------------------------------------------------
# 2. Determine repo root and name
# ---------------------------------------------------------------------------
REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || pwd)
REPO_NAME=$(basename "$REPO_ROOT")
OAT_REPORT_DIR="$REPO_ROOT/oat_reports"
RUN_REPORT_DIR=""

_oat_verbose "[OAT] Running OAT scan (Python Edition) — INCREMENTAL MODE"
_oat_verbose "[OAT] Project: $REPO_NAME"

# ---------------------------------------------------------------------------
# 3. Collect staged files
# ---------------------------------------------------------------------------
if [ $# -gt 0 ]; then
    # Called directly with file arguments (e.g. manual test)
    FILE_COUNT=$#
    FILE_LIST=""
    for _f in "$@"; do
        # Convert relative paths to absolute
        case "$_f" in
            /*) _abs="$_f" ;;
            *)  _abs="$REPO_ROOT/$_f" ;;
        esac
        [ -f "$_abs" ] || continue
        if [ -z "$FILE_LIST" ]; then
            FILE_LIST="$_abs"
        else
            FILE_LIST="$FILE_LIST,$_abs"
        fi
    done
else
    _STAGED=$(git diff --cached --name-only --diff-filter=ACM 2>/dev/null || true)
    if [ -z "$_STAGED" ]; then
        echo "[OAT] PASS: no staged files to check."
        exit 0
    fi
    FILE_COUNT=$(echo "$_STAGED" | wc -l | tr -d ' ')
    FILE_LIST=""
    for _f in $_STAGED; do
        case "$_f" in
            /*) _abs="$_f" ;;
            *)  _abs="$REPO_ROOT/$_f" ;;
        esac
        [ -f "$_abs" ] || continue
        if [ -z "$FILE_LIST" ]; then
            FILE_LIST="$_abs"
        else
            FILE_LIST="$FILE_LIST,$_abs"
        fi
    done
fi

if [ -z "$FILE_LIST" ]; then
    echo "[OAT] PASS: no existing staged files to check."
    exit 0
fi

_oat_verbose "[OAT] Checking $FILE_COUNT input file(s)..."

# ---------------------------------------------------------------------------
# 4. Ensure oat_reports/ exists and is in .gitignore
# ---------------------------------------------------------------------------
mkdir -p "$OAT_REPORT_DIR"
RUN_REPORT_DIR=$(mktemp -d "$OAT_REPORT_DIR/run.XXXXXX")

_GITIGNORE="$REPO_ROOT/.gitignore"
for _entry in "oat_reports" "log"; do
    if ! grep -qE "^${_entry}/?$" "$_GITIGNORE" 2>/dev/null; then
        printf "\n%s/\n" "$_entry" >> "$_GITIGNORE" 2>/dev/null || true
        _oat_verbose "[OAT] Added ${_entry}/ to .gitignore"
    fi
done

# ---------------------------------------------------------------------------
# 5. Run oat scan
# ---------------------------------------------------------------------------
_oat_verbose ""
_oat_verbose "[OAT] Running compliance scan..."

_OAT_XML="$REPO_ROOT/OAT.xml"
_OAT_LOG="$RUN_REPORT_DIR/oat.log"
set +e
if [ -f "$_OAT_XML" ]; then
    "$_PYTHON" -m oat -mode s -s "$REPO_ROOT" -r "$RUN_REPORT_DIR" -n "$REPO_NAME" -w 1 -f "$FILE_LIST" -oatconfig "$_OAT_XML" >"$_OAT_LOG" 2>&1
else
    "$_PYTHON" -m oat -mode s -s "$REPO_ROOT" -r "$RUN_REPORT_DIR" -n "$REPO_NAME" -w 1 -f "$FILE_LIST" >"$_OAT_LOG" 2>&1
fi
_OAT_RC=$?
set -e

if [ "$_OAT_RC" -ne 0 ] && [ "$_OAT_RC" -ne 1 ]; then
    echo ""
    echo "[OAT] [ERROR] oat exited with unexpected code $_OAT_RC."
    echo "[OAT] Log:"
    sed -n '1,120p' "$_OAT_LOG" 2>/dev/null || true
    echo "[OAT] Commit blocked to avoid a false pass."
    exit 1
fi

# ---------------------------------------------------------------------------
# 6. Parse report and write result.txt
#    Only: Invalid File Type + License Header Invalid + Copyright Header Invalid
# ---------------------------------------------------------------------------
REPORT_FILE="$RUN_REPORT_DIR/PlainReport_${REPO_NAME}.txt"
RESULT_FILE="$RUN_REPORT_DIR/result.txt"
LATEST_RESULT_FILE="$OAT_REPORT_DIR/result.txt"

# Section headers used as stop-boundaries when extracting sections
_ALL_HEADERS="Invalid File Type Total Count:|License Not Compatible Total Count:|License Header Invalid Total Count:|Copyright Header Invalid Total Count:|No License File Total Count:|No Readme.OpenSource Total Count:|No Readme Total Count:|Import Invalid Total Count:|Redundant License File Total Count:|Third Party Software Info Total Count:"

# Extract one section from the report (header line + detail Name: lines)
# Usage: _extract_section <file> <start_marker>
_extract_section() {
    _file="$1"
    _marker="$2"
    awk -v marker="$_marker" -v boundaries="$_ALL_HEADERS" '
        BEGIN { capturing=0 }
        !capturing {
            if (index($0, marker) == 1) { capturing=1; print; next }
            next
        }
        capturing {
            # Check if this line starts a different section header
            n = split(boundaries, hdrs, "|")
            for (i=1; i<=n; i++) {
                if (hdrs[i] != marker && index($0, hdrs[i]) == 1) { exit }
            }
            print
        }
    ' "$_file" | awk '
        /^[[:space:]]*$/ {
            pending_blank=1
            next
        }
        {
            if (pending_blank && seen) {
                print ""
            }
            pending_blank=0
            seen=1
            print
        }
    '
}

_read_count() {
    _file="$1"
    _marker="$2"
    grep "^$_marker" "$_file" 2>/dev/null | grep -oE '[0-9]+' | head -1
}

_append_section_details() {
    _file="$1"
    _marker="$2"
    _extract_section "$_file" "$_marker" | awk 'NR > 1 && /^Name:/ { print }'
}

_release_result_lock() {
    if [ -n "$_RESULT_LOCK_DIR" ] && [ -d "$_RESULT_LOCK_DIR" ]; then
        rmdir "$_RESULT_LOCK_DIR" 2>/dev/null || true
    fi
    trap - EXIT HUP INT TERM
}

_acquire_result_lock() {
    _RESULT_LOCK_DIR="$OAT_REPORT_DIR/.result.lock"
    while ! mkdir "$_RESULT_LOCK_DIR" 2>/dev/null; do
        sleep 0.1
    done
    trap '_release_result_lock' EXIT HUP INT TERM
}

_result_session_id() {
    if [ -n "$OAT_REPORT_SESSION_ID" ]; then
        printf '%s\n' "$OAT_REPORT_SESSION_ID"
    elif [ "$PRE_COMMIT" = "1" ]; then
        printf 'precommit-%s\n' "$PPID"
    else
        printf 'manual-%s\n' "$$"
    fi
}

_write_aggregate_result() {
    _manifest_file="$1"
    _target_file="$2"
    _detail_type="$RUN_REPORT_DIR/aggregate_invalid_type.details"
    _detail_license="$RUN_REPORT_DIR/aggregate_license.details"
    _detail_copyright="$RUN_REPORT_DIR/aggregate_copyright.details"

    : > "$_detail_type"
    : > "$_detail_license"
    : > "$_detail_copyright"

    _AGG_FILES=0
    _AGG_RUNS=0
    _AGG_INVALID_TYPE=0
    _AGG_LICENSE_INVALID=0
    _AGG_COPYRIGHT_INVALID=0

    while IFS= read -r _result_file; do
        [ -f "$_result_file" ] || continue
        _AGG_RUNS=$(( _AGG_RUNS + 1 ))

        _files_checked=$(_read_count "$_result_file" "Files Checked:" || true)
        _invalid_type=$(_read_count "$_result_file" "Invalid File Type Total Count:" || true)
        _license_invalid=$(_read_count "$_result_file" "License Header Invalid Total Count:" || true)
        _copyright_invalid=$(_read_count "$_result_file" "Copyright Header Invalid Total Count:" || true)

        _files_checked=${_files_checked:-0}
        _invalid_type=${_invalid_type:-0}
        _license_invalid=${_license_invalid:-0}
        _copyright_invalid=${_copyright_invalid:-0}

        _AGG_FILES=$(( _AGG_FILES + _files_checked ))
        _AGG_INVALID_TYPE=$(( _AGG_INVALID_TYPE + _invalid_type ))
        _AGG_LICENSE_INVALID=$(( _AGG_LICENSE_INVALID + _license_invalid ))
        _AGG_COPYRIGHT_INVALID=$(( _AGG_COPYRIGHT_INVALID + _copyright_invalid ))

        _append_section_details "$_result_file" "Invalid File Type Total Count:" >> "$_detail_type"
        _append_section_details "$_result_file" "License Header Invalid Total Count:" >> "$_detail_license"
        _append_section_details "$_result_file" "Copyright Header Invalid Total Count:" >> "$_detail_copyright"
    done < "$_manifest_file"

    _AGG_TOTAL_ISSUES=$(( _AGG_INVALID_TYPE + _AGG_LICENSE_INVALID + _AGG_COPYRIGHT_INVALID ))

    {
        echo "==================================="
        echo "OAT Scan Result Summary"
        echo "==================================="
        printf "Scan Time: %s\n" "$(date '+%Y-%m-%d %H:%M:%S')"
        echo "Project: $REPO_NAME"
        echo "Files Checked: $_AGG_FILES"
        echo "Runs Aggregated: $_AGG_RUNS"
        echo ""
        echo "-----------------------------------"
        echo "Invalid File Type Total Count: $_AGG_INVALID_TYPE"
        [ ! -s "$_detail_type" ] || cat "$_detail_type"
        echo ""
        echo "-----------------------------------"
        echo "License Header Invalid Total Count: $_AGG_LICENSE_INVALID"
        [ ! -s "$_detail_license" ] || cat "$_detail_license"
        echo ""
        echo "-----------------------------------"
        echo "Copyright Header Invalid Total Count: $_AGG_COPYRIGHT_INVALID"
        [ ! -s "$_detail_copyright" ] || cat "$_detail_copyright"
        echo "==================================="
    } > "$_target_file"
}

_register_result() {
    _current_session=$(_result_session_id)
    _session_file="$OAT_REPORT_DIR/.result_session"
    _manifest_file="$OAT_REPORT_DIR/.result_manifest"

    _acquire_result_lock

    _previous_session=""
    [ ! -f "$_session_file" ] || _previous_session=$(cat "$_session_file" 2>/dev/null || true)
    if [ "$_previous_session" != "$_current_session" ]; then
        printf '%s\n' "$_current_session" > "$_session_file"
        : > "$_manifest_file"
    fi

    printf '%s\n' "$RESULT_FILE" >> "$_manifest_file"
    _write_aggregate_result "$_manifest_file" "$LATEST_RESULT_FILE"

    _release_result_lock
}

_print_issue_section() {
    _summary_file="$1"
    _marker="$2"
    _label="$3"
    _count="$4"

    [ "$_count" -gt 0 ] || return 0

    printf "[OAT] %s: %s\n" "$_label" "$_count"
    _details=$(_extract_section "$_summary_file" "$_marker" | awk 'NR > 1 && /^Name:/ { sub(/^Name:[[:space:]]*/, "  - "); print }')
    if [ -n "$_details" ]; then
        printf "%s\n" "$_details"
    else
        printf "  - See report for details: %s\n" "$_summary_file"
    fi
}

_print_issue_summary() {
    _summary_file="$1"
    _invalid_type="$2"
    _license_invalid="$3"
    _copyright_invalid="$4"
    _total=$(( _invalid_type + _license_invalid + _copyright_invalid ))

    printf "[OAT] FAIL: %s issue(s) found.\n" "$_total"
    _print_issue_section "$_summary_file" "Invalid File Type Total Count:" "Invalid File Type" "$_invalid_type"
    _print_issue_section "$_summary_file" "License Header Invalid Total Count:" "License Header Invalid" "$_license_invalid"
    _print_issue_section "$_summary_file" "Copyright Header Invalid Total Count:" "Copyright Header Invalid" "$_copyright_invalid"
    printf "[OAT] Report: %s\n" "$_summary_file"
}

if [ ! -f "$REPORT_FILE" ]; then
    if [ "$_OAT_RC" -ne 0 ]; then
        echo ""
        echo "[OAT] [ERROR] oat exited with code $_OAT_RC but did not generate a report."
        echo "[OAT] Log:"
        sed -n '1,120p' "$_OAT_LOG" 2>/dev/null || true
        echo "[OAT] Commit blocked to avoid a false pass."
        exit 1
    fi

    echo "[OAT] [OK] No report generated; all reportable checks passed or supplied files were filtered ($FILE_COUNT file(s) checked)."
    {
        echo "==================================="
        echo "OAT Scan Result Summary"
        echo "==================================="
        printf "Scan Time: %s\n" "$(date '+%Y-%m-%d %H:%M:%S')"
        echo "Project: $REPO_NAME"
        echo "Files Checked: $FILE_COUNT"
        echo ""
        echo "-----------------------------------"
        echo "Invalid File Type Total Count: 0"
        echo ""
        echo "-----------------------------------"
        echo "License Header Invalid Total Count: 0"
        echo ""
        echo "-----------------------------------"
        echo "Copyright Header Invalid Total Count: 0"
        echo "==================================="
    } > "$RESULT_FILE"
    _register_result
    echo "[OAT] PASS: no OAT issues found ($FILE_COUNT file(s) checked)."
    exit 0
fi

# Parse counts
_INVALID_TYPE=$(grep "^Invalid File Type Total Count:" "$REPORT_FILE" | grep -oE '[0-9]+' | head -1)
_LICENSE_INVALID=$(grep "^License Header Invalid Total Count:" "$REPORT_FILE" | grep -oE '[0-9]+' | head -1)
_COPYRIGHT_INVALID=$(grep "^Copyright Header Invalid Total Count:" "$REPORT_FILE" | grep -oE '[0-9]+' | head -1)
_INVALID_TYPE=${_INVALID_TYPE:-0}
_LICENSE_INVALID=${_LICENSE_INVALID:-0}
_COPYRIGHT_INVALID=${_COPYRIGHT_INVALID:-0}

# Extract detail sections
_SECTION_TYPE=$(_extract_section "$REPORT_FILE" "Invalid File Type Total Count:")
_SECTION_LIC=$(_extract_section "$REPORT_FILE" "License Header Invalid Total Count:")
_SECTION_COPYRIGHT=$(_extract_section "$REPORT_FILE" "Copyright Header Invalid Total Count:")

# Fallback to bare count line if section is empty
[ -n "$_SECTION_TYPE" ] || _SECTION_TYPE="Invalid File Type Total Count: $_INVALID_TYPE"
[ -n "$_SECTION_LIC"  ] || _SECTION_LIC="License Header Invalid Total Count: $_LICENSE_INVALID"
[ -n "$_SECTION_COPYRIGHT" ] || _SECTION_COPYRIGHT="Copyright Header Invalid Total Count: $_COPYRIGHT_INVALID"

# Write result.txt
{
    echo "==================================="
    echo "OAT Scan Result Summary"
    echo "==================================="
    printf "Scan Time: %s\n" "$(date '+%Y-%m-%d %H:%M:%S')"
    echo "Project: $REPO_NAME"
    echo "Files Checked: $FILE_COUNT"
    echo ""
    echo "-----------------------------------"
    echo "$_SECTION_TYPE"
    echo ""
    echo "-----------------------------------"
    echo "$_SECTION_LIC"
    echo ""
    echo "-----------------------------------"
    echo "$_SECTION_COPYRIGHT"
    echo "==================================="
} > "$RESULT_FILE"
_register_result

# Clean up full plain report (keep only result.txt)
rm -f "$REPORT_FILE"

# ---------------------------------------------------------------------------
# 7. Block commit if issues found
# ---------------------------------------------------------------------------
TOTAL_ISSUES=$(( _INVALID_TYPE + _LICENSE_INVALID + _COPYRIGHT_INVALID ))

if [ "$TOTAL_ISSUES" -gt 0 ]; then
    _print_issue_summary "$LATEST_RESULT_FILE" "${_AGG_INVALID_TYPE:-$_INVALID_TYPE}" "${_AGG_LICENSE_INVALID:-$_LICENSE_INVALID}" "${_AGG_COPYRIGHT_INVALID:-$_COPYRIGHT_INVALID}"
    exit 1
fi

if [ "${_AGG_TOTAL_ISSUES:-0}" -gt 0 ]; then
    _print_issue_summary "$LATEST_RESULT_FILE" "${_AGG_INVALID_TYPE:-0}" "${_AGG_LICENSE_INVALID:-0}" "${_AGG_COPYRIGHT_INVALID:-0}"
else
    echo "[OAT] PASS: no OAT issues found ($_AGG_FILES file(s) checked across $_AGG_RUNS run(s))."
fi
exit 0
