# Core logging

`lps::Logger` is a process-wide asynchronous logger designed for use by the
portable core, including audio-thread call sites. Producers copy each record
into a fixed-capacity lock-free queue. A background thread formats and writes
complete lines; producers never wait for or write to the filesystem.
If all 2,048 queue slots are occupied, new records are dropped and the count is
available through `Logger::diagnostics()` and the graph's periodic status line.
The status also reports file-write failures, which automatically fall back to
standard error.

The plugin configures the logger automatically. Other core hosts can configure
it before constructing graph objects:

```cpp
#include "core/Logger.h"

lps::Logger::configure({
    "/path/to/sequencer.log",
    lps::LogLevel::info,
    false // mirror to stderr
});
```

Call `Logger::flush()` only from a non-real-time thread when a test or orderly
shutdown needs to wait for queued records. `Logger::shutdown()` joins the
writer thread; it must never be called from the audio callback.

## Record format

Records are UTC, one per line, with stable fields followed by event-specific
`key=value` details:

```text
2026-09-14T18:42:03.123456Z level=INFO component=runtime_graph event=status playing=1 generation=1 blocks=188 work_signals=37 resolved_events=12 logger_dropped=0 logger_write_failures=0
```

Useful searches include:

```bash
grep -E 'level=(WARN|ERROR)' sequencer.log
grep 'event=trigger_dropped' sequencer.log
grep 'voice_id=7' sequencer.log
```

The plugin emits these main events:

- `config_activated`, `config_published`, and `config_adopted` for graph state.
- `transport_state` and `transport_discontinuity` for playback changes.
- `status` once per second of playback for a compact health snapshot.
- `process_overflow`, `render_rejected`, and `renderer_failed` for graph faults.
- `trigger_dropped` and `event_buffer_overflow` for silent voice failures.
- `setting_rejected`, `selection_rejected`, and `config_rejected` for invalid control input.

Repeated voice failures are reported at counts 1, 2, 4, 8, and so on, retaining
visibility without producing one log record per failed hit.
