---
name: compile
description: Build ddprof inside the Ubuntu 24 dev Docker container. Defaults to Debug (gcc, no clang-tidy) for speed. Use when the user asks to compile, build, or rebuild the project. Reuses a running container when possible, otherwise launches one in the background. Accepts an optional mode arg (Deb/Rel/DebTidy/San/TSan/Cov).
---

# Compile ddprof (Ubuntu 24 dev container)

Builds **all targets** inside the Ubuntu 24 dev container. Default mode is
**Debug** (gcc, no clang-tidy) — fastest iteration. Override via `args` when
the user wants something else.

## Args

`args` is a single optional token: the build mode.

| `args`     | Mode      | CC/CXX    | CMake helper      | Notes                                    |
|------------|-----------|-----------|-------------------|------------------------------------------|
| (empty)    | `Deb`     | gcc       | `DebCMake`        | Default. Fastest. No clang-tidy.         |
| `rel`      | `Rel`     | gcc       | `RelCMake`        | Optimised + LTO. Slow link.              |
| `debtidy`  | `DebTidy` | clang     | `DebTidyCMake`    | Enables clang-tidy. Slowest. Lint gate.  |
| `san`      | `San`     | gcc       | `SanCMake`        | ASan + UBSan.                            |
| `tsan`     | `TSan`    | gcc       | `TSanCMake`       | ThreadSanitizer.                         |
| `cov`      | `Cov`     | gcc       | `CovCMake`        | Coverage instrumentation.                |

Match `args` case-insensitively. If the user's natural-language request
implies a mode (e.g. "do a release build", "run with sanitizers"), prefer
that over the default. Only `DebTidy` requires `CC=clang CXX=clang++`; for
the others leave `CC`/`CXX` unset so the container's gcc is used.

## Step 1 — Find a running container

```bash
CID=$(docker ps \
  --filter ancestor=base_ddprof_24_gcc \
  --filter ancestor=base_ddprof_24_clang \
  --format '{{.ID}}' | head -1)
```

If `CID` is non-empty → skip to Step 3.

## Step 2 — Start one in the background (only if nothing is running)

Confirm an image exists:

```bash
IMG=$(docker image ls --format '{{.Repository}}' \
  | grep -E '^base_ddprof_24_(gcc|clang)$' | head -1)
```

If no image is present, stop and ask the user to run
`./tools/launch_local_build.sh -u 24` once — the first-time image build is
long and interactive. Do not try to build the image automatically.

If the image exists, start a detached container with the same mounts as the
launch script (minus the interactive TTY and SSH agent):

```bash
CID=$(docker run -d --rm \
  -u "$(id -u):$(id -g)" \
  --network=host -w /app \
  --cap-add CAP_SYS_PTRACE --cap-add SYS_ADMIN \
  -v "$PWD:/app" \
  "$IMG" \
  sleep infinity)
```

Note: uses `sleep infinity` so subsequent `docker exec` calls have a host to
attach to. Let the user reclaim it; do not auto-kill.

## Step 3 — Run the build via `docker exec`

Substitute `<MODE>` and `<HELPER>` from the table above. For modes other than
`DebTidy`, omit the `CC`/`CXX` exports.

Example for the default (`Deb`):

```bash
docker exec "$CID" bash -lc '
  set -euo pipefail
  cd /app
  source ./setup_env.sh
  MkBuildDir Deb
  DebCMake ../
  make -j10
```

Example for `DebTidy`:

```bash
docker exec "$CID" bash -lc '
  set -euo pipefail
  cd /app
  export CC=clang CXX=clang++
  source ./setup_env.sh
  MkBuildDir DebTidy
  DebTidyCMake ../
  make -j10
'
```

### Backgrounding & exit codes

Long builds should run via `Bash` with `run_in_background: true` so the user
isn't blocked. **Do not pipe to `tee`** when backgrounding — `tee` swallows
the failing exit code from `make` and the build looks like it succeeded. If
you need the log on disk, redirect with `> /tmp/ddprof_compile.log 2>&1`
instead, or set `set -o pipefail` and check `${PIPESTATUS[0]}` explicitly.

After completion, check the exit code reported by the task notification.
On failure, extract the first compiler/clang-tidy errors:

```bash
grep -E "error:" /tmp/ddprof_compile.log | head -20
```

Surface those (path:line + diagnostic) to the user — do not dump the whole
log.

## Notes

- Build directory is derived from `MkBuildDir <suffix>` and the host
  libc/compiler triple, e.g. `build_gcc_unknown-linux-2.39_Deb`,
  `build_clang_unknown-linux-2.39_DebTidy`. Each mode has its own dir, so
  switching modes does not clobber the previous build.
- `-j10` matches the user's typical workflow. Bump if the box has many cores.
- For Alpine/release builds, this skill is the wrong tool — see
  `CLAUDE.md` § "Alpine (release) builds".
