#!/bin/bash
set -o pipefail

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <test-command> [args...]" >&2
    exit 2
fi

output_file=$(mktemp)
trap 'rm -f "${output_file}"' EXIT

"$@" 2>&1 | tee "${output_file}"
test_status=${PIPESTATUS[0]}

if [[ ${test_status} -ne 0 ]]; then
    exit "${test_status}"
fi

# Keep the output check as a fallback for runners that incorrectly return 0
# after printing a GTest failure summary.
if grep -Eq '\[[[:space:]]*FAILED[[:space:]]*\]|FAILED TESTS' "${output_file}"; then
    exit 1
fi

exit 0
