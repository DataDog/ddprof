#!/usr/bin/env python3

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

import argparse
from collections import defaultdict
import re
import subprocess
import sys

unsafe_functions = [
    {"name": "pthread_mutex_init"},
    {"name": "pthread_attr_init"},
    {"name": "pthread_mutexattr_init"},
    {"name": "pthread_condattr_init"},
    {"name": "pthread_barrierattr_init"},
    {"name": "mtx_init"},
    {
        "name": "pthread_getattr_np",
        "exceptions": ["_Z23pthread_getattr_np_safemP14pthread_attr_t"],
    },
]

func_start_re = re.compile("^<([a-zA-Z0-9@_.*+]+)>:$")
call_instr_re = re.compile("^\t(?:bl|call)\t<([a-zA-Z0-9@_.*+]+)>$")


def extract_calls(lib_path):
    res = subprocess.run(
        ["objdump", "-d", "--no-show-raw-insn", "--no-addresses", lib_path],
        check=True,
        capture_output=True,
        text=True,
    )
    callers = defaultdict(list)
    cur_func = None
    for line in res.stdout.splitlines():
        m = func_start_re.match(line)
        if m:
            cur_func = m.group(1)
        else:
            m = call_instr_re.match(line)
            if m:
                callers[m.group(1)].append(cur_func)

    return callers


def check_functions(callers, unsafe_functions):
    ok = True
    for func in unsafe_functions:
        name = func["name"] + "@plt"
        for caller in callers[name]:
            if caller not in func.get("exceptions", []):
                print(f'Unsafe function {func["name"]} called by {caller}')
                ok = False
    return ok


def main():
    parser = argparse.ArgumentParser(
        description="Check that no unsafe libc function (that are not ABI compatible between musl and glibc) is used in injected lib."
    )
    parser.add_argument("lib", help="shared library to check")
    args = parser.parse_args()

    callers = extract_calls(args.lib)
    if not check_functions(callers, unsafe_functions):
        return 1


sys.exit(main())
