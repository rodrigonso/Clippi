# Core SDK

The core contains the agent loop, permissions, compaction, JSON, memory and
durable session repositories, filesystem helpers, and platform primitives.
It uses only the C runtime and OS APIs: **no libcurl, PCRE2,
JavaScript runtime, or dependency downloads**. Providers and tools are supplied
by the embedding application. The bundled provider, tools, terminal UI, and
application settings are CLI features, not part of this SDK.

```powershell
.\tools\build.ps1 -CoreOnly -Examples -Test -InstallPrefix .\out\sdk
```

```sh
bash tools/build.sh --core-only --examples --test --install-prefix "$PWD/out/sdk"
```

Core-only builds default to `out/core`; use a separate `--build-dir`/`-BuildDir`
when sharing a checkout between operating systems. Both modes use the same
`clippi_tests` runner; a core-only build registers just its 14 core suites.
`--examples`/`-Examples` also runs the offline provider-injection example.
`clippi_tests --benchmark` measures agent-loop latency and peak RSS on a
scripted workload.

Installation supplies `include/clippi` and a single static core archive under
`lib`: `clippi_core.lib` on Windows, `libclippi_core.a` on macOS. Debug archives
have a `d` suffix so Debug and Release can coexist. Consumers use their own build
system; no generated project or package-manager integration is required.

[`examples/core/main.cpp`](../examples/core/main.cpp) creates an in-memory session,
injects a local `Provider::Stream`, runs the harness, and reads its resulting
transcript without making any network requests. To compile it against the
installed SDK, use an x64 Visual Studio Developer Command Prompt. The manual
command uses `/std:c++latest` for MSVC's C++23 support:

```bat
cl /nologo /std:c++latest /MT /GR- /EHs-c- /Iout\sdk\include\clippi ^
  examples\core\main.cpp /Foout\consumer.obj /Feout\consumer.exe ^
  /link out\sdk\lib\clippi_core.lib psapi.lib bcrypt.lib
out\consumer.exe
```

Windows SDK consumers must match the archive's CRT: `/MT` for Release,
RelWithDebInfo, and MinSizeRel, or `/MDd` with `clippi_cored.lib` for Debug.

On macOS:

```sh
c++ -std=c++23 -fno-exceptions -fno-rtti -Iout/sdk/include/clippi \
  -c examples/core/main.cpp -o out/consumer.o
cc out/consumer.o out/sdk/lib/libclippi_core.a -pthread -lm -o out/consumer
./out/consumer
```

The provider owns its context; returned messages must use the arena passed to
`Stream`, and streaming slices need only remain valid during the callback.
Keep configuration backing storage alive for the harness lifetime. Close the
harness before its repository and release caller-owned arenas afterward.
Concurrent repository operations require separate caller arenas; shared
repository allocation is serialized. Join outstanding operations before closing
their logs or repository. Harness metadata changes and forks reserve mutation
admission through completion, just like runs.
Embedding defaults differ intentionally from the CLI: turn limits and compaction
are disabled, and tools are admitted in Yolo mode unless configured otherwise.
The example selects Ask explicitly and registers no tools.

This is an early C++ source API, not a stable ABI. Rebuild the SDK and consumers
together with compatible compiler/runtime settings when upgrading.

## Snapshots and event watches

Choose the smallest read API needed:

- `AgentHarness_Snapshot` returns a deep-owned `SessionReduction` for metadata
  and tree consumers.
- `AgentHarness_SnapshotTranscript` returns an `AgentTranscriptSnapshot` with
  the active, compaction-aware transcript, head ID, event cursor, running/faulted
  flags, and `HasEarlierMessages`. Set `maxMessages` to zero for the whole
  transcript or a positive count for its exact chronological suffix.
- `AgentHarness_Watch(harness, capacity, watch)` subscribes to future events
  without allocating or copying history.
- `AgentHarness_WatchTranscript` atomically snapshots the transcript and
  registers a watch at the same cursor. Use this instead of taking a snapshot
  and subscribing separately, which can miss intervening events.

Snapshot payloads, including nested message content, belong entirely to the
supplied arena; they do not borrow harness storage. Hydrate or consume them,
then release that arena. A watch owns its queue until
`AgentHarness_WatchClose`; each `AgentHarness_WatchNext` event is deep-owned by
the arena supplied to that call.

Presentation watches may coalesce or drop transient events and report lag.
`AgentHarnessConfig.Events` instead observes every event synchronously, in
publication order, without copying or retaining payloads. Its callback must
not reenter the harness or wait for another harness operation; borrowed content
is read-only and valid only for the callback. A blocking callback applies
backpressure. JSON output uses this observer rather than a presentation watch.

The plain REPL and text print mode watch without history. TUI startup, session
switches, and lag recovery use an atomic whole-transcript watch, hydrate before
delivering live events, and immediately release a dedicated snapshot arena.
Idle TUI memory compaction requests only the last 400 messages rather than
cloning the whole history and slicing it afterward. This is a message-count
limit, not a byte bound: individual messages can be large, whole-transcript
hydration still scales with history, and branch-projection scratch still scales
with the active branch.

## Transient memory ownership

Run-message bookkeeping lasts for the run, but context and model overrides
have replaceable owners: installing a new override releases the superseded
storage without resetting the accumulated message list. Hook construction
buffers are temporary, and the harness copies accepted overrides before that
scratch is reclaimed. Automatic compaction discards the context override while
preserving independent model/profile overrides.

Mutable hook inputs are detached down to nested content and configuration
views, so edits cannot mutate stored history or retained overrides. Borrowed
replacements are detached before subsequent mutable callbacks; retries reuse
already-owned turn storage. Runs without mutable hooks keep the borrowing fast
path.

The TUI reclaims consumed display batches during active runs, including when
newer events remain queued. Delayed text reveal keeps its payload alive until
the line model has consumed it. Memory scales with durable history, retained
transcript content, current overrides, and pending display output; there is no
total-session memory cap.
