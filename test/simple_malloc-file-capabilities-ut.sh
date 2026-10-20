#!/usr/bin/env bash

# Reproduce #562: file capabilities suppress LD_PRELOAD in secure execution.
# This test asserts the current limitation, with allocation-positive controls.
set -euo pipefail

skip() {
    echo "SKIP: $*"
    exit 77
}

[[ ${EUID} -eq 0 ]] || skip "requires root for file capabilities and --switch_user"
for tool in setcap setpriv getent timeout tail; do
    command -v "${tool}" >/dev/null || skip "requires ${tool}"
done

builddir=${PWD}
scriptdir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_user=nobody
test_uid=$(getent passwd "${test_user}" | cut -d: -f3) || skip "requires user ${test_user}"
test_gid=$(getent passwd "${test_user}" | cut -d: -f4)
[[ ${test_uid} -ne 0 ]] || skip "requires a non-root test user"

# /tmp and build mounts may be nosuid, so use a private copy on the root filesystem.
tmpdir=$(mktemp -d /usr/local/bin/ddprof-filecaps.XXXXXX) || skip "cannot create test directory"
trap 'rm -rf -- "${tmpdir}"' EXIT
chmod 755 "${tmpdir}"
fixture=${tmpdir}/simple_malloc
cp "${builddir}/test/simple_malloc" "${fixture}"
chmod 755 "${fixture}"

setcap cap_net_bind_service=ep "${fixture}" || skip "cannot set file capabilities"
probe=$(setpriv --reuid="${test_uid}" --regid="${test_gid}" --clear-groups \
    "${fixture}" --show-security-context --loop 1 2>&1) || skip "cannot execute capability-marked fixture: ${probe}"
grep -q "at_secure=1" <<< "${probe}" || skip "file capabilities did not trigger AT_SECURE (nosuid or restricted capabilities)"
setcap -r "${fixture}"

export DD_PROFILING_NATIVE_USE_EMBEDDED_LIB=1
export LD_LIBRARY_PATH=${builddir}
export DD_PROFILING_NATIVE_CONFIG=${scriptdir}/configs/simple_malloc_config.toml

check() {
    local name=$1 expected_secure=$2 expected_preload=$3
    local log_file=${tmpdir}/${name}.log output=${tmpdir}/${name}.out
    local profiler_pid samples

    echo "--- ${name} ---"
    if ! timeout 30 "${builddir}/ddprof" --switch_user "${test_user}" -o "${log_file}" \
        "${fixture}" --show-security-context --malloc 1048576 --realloc 0 \
        --loop 64 --spin 1000 >"${output}" 2>&1; then
        cat "${output}" "${log_file}"
        return 1
    fi
    profiler_pid=$(grep -m1 -oP ' ddprof\[\K[0-9]+(?=\]: Starting profiler)' "${log_file}")
    timeout 30 tail --pid="${profiler_pid}" -f /dev/null

    cat "${output}"
    grep -q "^TestSecurity: uid=${test_uid} euid=${test_uid} at_secure=${expected_secure} ld_preload=${expected_preload}$" "${output}"
    samples=$(grep -c 'sample\[type=alloc-space;.*;do_lot_of_allocations' "${log_file}" || true)
    echo "Allocation samples in do_lot_of_allocations: ${samples}"
    if [[ ${expected_secure} -eq 1 ]]; then
        [[ ${samples} -eq 0 ]]
    else
        [[ ${samples} -gt 0 ]] || { cat "${log_file}"; return 1; }
    fi
}

check no-capabilities 0 set
setcap cap_net_bind_service=ep "${fixture}"
check file-capabilities 1 unset
setcap -r "${fixture}"
check capabilities-removed 0 set

echo "REPRODUCED #562: AT_SECURE strips LD_PRELOAD and allocation samples disappear."
