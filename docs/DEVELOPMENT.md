# Development

## Build

`tools/build.ps1` (Windows) and `tools/build.sh` (macOS) are thin wrappers around **CMake 3.24+**.
The root `CMakeLists.txt` owns source lists, compiler settings, targets, and SDK
installation. `cmake/curl.cmake` and `cmake/pcre2.cmake` configure the CLI's
pinned source dependencies; release packaging stays in separate scripts.

Windows requires the Visual Studio Desktop development with C++ workload,
including the Windows SDK and C++ CMake tools (CMake and Ninja). The script
locates and initializes the x64 toolchain automatically; no developer prompt
is needed. macOS requires CMake, a C++23-capable toolchain, and Make or Ninja.
These are build-machine requirements, not runtime dependencies. Unsupported
hosts and targets are rejected before configuring compilers or dependencies.

C++23 is required for all Clippi targets and core SDK consumers. CMake selects
the appropriate language-mode flag for the detected compiler.

Windows Release, RelWithDebInfo, and MinSizeRel builds statically link the CRT
(`/MT`) throughout Clippi, libcurl, and PCRE2. Debug builds retain the shared
debug CRT (`/MDd`). Reconfigure and rebuild existing build directories when
changing this runtime policy.

The CLI builds SHA256-pinned static libcurl and PCRE2 10.48 (the 8-bit C regex
library) using their upstream CMake builds.
Downloaded sources and dependency build outputs stay in the selected build
directory under `_deps/`. HTTPS uses native Schannel on Windows and
SecureTransport on macOS, with the host certificate trust store.
Core-only builds never fetch or build networking or regex dependencies.

```powershell
.\tools\build.ps1                                # Release CLI
.\tools\build.ps1 -Config Debug                  # Debug CLI
.\tools\build.ps1 -Test                          # full offline sweep
.\tools\build.ps1 -Test -TestArgs session,tools  # selected suites
.\tools\build.ps1 -CoreOnly -Examples -Test      # dependency-free core
```

The script runs in Windows PowerShell 5.1 or PowerShell 7. If script execution
is disabled, run it as
`powershell -ExecutionPolicy Bypass -File tools\build.ps1 <options>`.

```sh
bash tools/build.sh              # macOS CLI
bash tools/build.sh --test       # build + offline suites
```

CMake handles incremental and parallel builds, including header dependencies.
`-Clean` / `--clean` rebuilds without deleting the build directory or downloaded
sources. `-CurlArchive` / `--curl-archive` and `-Pcre2Archive` / `--pcre2-archive`
accept offline source archives; their SHA256 values must match the pins in
`cmake/curl.cmake` and `cmake/pcre2.cmake`, respectively. These set the CMake cache
options `CLIPPI_CURL_ARCHIVE` and `CLIPPI_PCRE2_ARCHIVE`. Use separate build
directories when changing compilers or sharing a checkout across platforms.
Archive pins are checked again on every configure, even when their sources
have already been populated.

The build exposes three options: `CLIPPI_BUILD_CLI` (on by default),
`CLIPPI_BUILD_TESTS`, and `CLIPPI_BUILD_EXAMPLES` (both off by default).
The corresponding targets are `clippi`, `clippi_core`, `clippi_tests`, and
`clippi_core_example`. One private object target shares CLI code with its
tests; there is no hierarchy of component libraries or CPack configuration.

`tools\run.ps1` builds incrementally and then launches the executable in your
current directory:

```powershell
.\tools\run.ps1                       # build, then start the TUI
.\tools\run.ps1 -- -c                 # forward Clippi's own flags after --
.\tools\run.ps1 -NoBuild --help       # skip the build, run what's there
```

Put `--` before Clippi's short options: without it PowerShell binds `-c` to
`run.ps1`'s own `-Config`.

### Visual Studio

The checked-in `.vsconfig` declares the Desktop development with C++ workload
and CMake tools. Open the folder in Visual Studio to use its CMake integration,
or build with `.\tools\build.ps1 -Config Debug` and launch or attach the debugger to
`build\clippi.exe`. There are no project-specific CMake presets to maintain.

## Test

`clippi_tests` is the single test harness for the whole project: one CLI that
lets you (or an agent) pick exactly what to exercise.

```sh
./build/clippi_tests                 # every offline suite
./build/clippi_tests --list          # suite names, tags, descriptions
./build/clippi_tests json session    # only the named suites
./build/clippi_tests --filter line   # substring match: linemodel + lineedit
./build/clippi_tests --all           # include network-tagged suites
./build/clippi_tests --help          # usage
```

Suites tagged `network` (currently `http_live`, a live TLS/HTTP request) are
skipped by default, so a bare run stays
offline and deterministic. Request one by name or with `--all`; point it at a
local server with `CLIPPI_TEST_HTTP_URL`. Exit code is 0 on success, 1 on test
failure, 2 on a usage error.

Adding a suite is a new `tests/test_*.cpp` and one row in `tests/suites.txt`.
That registration supplies the source file, function declaration, and runner
entry. Mark CLI-only suites `cli`; the core build includes only `core` rows.
Unfiltered build-script runs also exercise CMake's unsupported-platform guard
for both the CLI and core SDK, before any compiler or dependency setup.

The default run is network-free and key-free: the loop runs against the
scripted provider in `tests/script_provider.h`, the Responses request
builder and SSE parser against recorded fixtures, and the TUI via pure units
(key decoder, differential-renderer diff, markdown, line-wrapping, the
multi-line editor) plus a headless controller test driving the
event→transcript mapping. Live full-screen rendering is verified manually.

## CLI distribution

The CLI statically links libcurl and PCRE2 and keeps platform TLS. The build
scripts produce a relocatable runtime-only ZIP on Windows or TGZ on macOS,
plus a companion SHA256 checksum, under the build directory's `packages` folder.
SDK headers and libraries are not included in these CLI archives.

```powershell
.\tools\build.ps1 -Package
.\tools\build.ps1 -InstallPrefix .\out\install
```

On macOS:

```sh
bash tools/build.sh --build-dir out/release --package
```

The packaging step requires the project and upstream notices and rejects Debug
configurations and unresolved or non-system
runtime dependencies. Windows Clippi binaries statically link the release CRT
and require no VC runtime DLLs; packaging rejects a Clippi binary that imports
them. Windows 10+ and its certificate store are still required. macOS uses
system frameworks; the unsigned preview targets macOS 15+ in native CI.
Native CI and interactive qualification must pass before publishing.

Each archive contains `share/clippi/DISTRIBUTION.txt` and dependency notices;
that file also marks a script install, which `clippi upgrade` requires. The
install scripts (`tools/install/`) take a local archive through
`CLIPPI_INSTALL_ARCHIVE`, which is how native CI exercises them. These
scripts do not sign, publish, or include user configuration or session data.
Missing project or upstream notices are packaging errors. The GitHub workflow
can prepare a draft release after all native jobs succeed; publishing remains
a maintainer action. See [release preparation](RELEASING.md).
