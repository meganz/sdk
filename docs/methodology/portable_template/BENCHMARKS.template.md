# Integration benchmarks and stress tests

Quick reference for tests used to validate <feature area> changes.
Extend this file as new scenarios appear.

## Running

```bash
cd <build dir>/<test dir>
source <env file>  # if any
<test runner> --filter=<pattern>
```

Test logs land at: `<log location>`. To find the latest:

```bash
<command to find latest log>
```

**Never `cat` a multi-GB log.** Use `grep`, `head`, `tail`, `wc -l` only.

## Core regression targets

Tests whose failure usually indicates a real bug, not infra noise.

| Test | What it exercises | Typical wall-clock | Workload |
| --- | --- | --- | --- |
| `<TestName>` | <description> | <time> | <workload> |

## Throughput benchmarks

Use for A/B comparison when changing performance-critical paths.

| Test | Workload | Typical wall-clock |
| --- | --- | --- |
| `<BenchName>` | <workload> | <time> |

Per-transfer / per-cell metrics are logged at <log markers>.

## Broader sweeps

Use when investigating side effects beyond the immediate fix.

| Filter | Tests | Budget |
| --- | --- | --- |
| `<filter pattern>` | <count> | <time> |

## Known-bad / expected failures

Do **not** attribute these to your fix without evidence.

- `<TestName>` — <reason>. Pre-existing. Treat as expected.

## Test-infrastructure quirks

- <quirk 1>
- <quirk 2>

## Metrics worth grepping for <feature> changes

```bash
log="<log path>"

# <category 1>
grep -c '<marker>' "$log"

# <category 2>
grep -E '<marker pattern>' "$log" | head -5
```

## Diagnostic protocol when a test hangs

1. `<ps / pgrep command>` — confirm it is still alive.
2. `<stack trace command>` — fast non-destructive snapshot.
3. `<core dump command>` — persist state.
4. `<full thread dump>` — for analysis.
5. Look for contention on `<known lock / queue>`. The classic
   livelock signature is `<description>`.

## Reference commands / prior-session artifacts

- Single-test timing: `time <test runner> --filter=<test>`.
- Sequential multi-test driver: `<path to script>`.
- Example baseline / before-after reports: `<path>`.

## Bench-report JSON

When the binary is built with `<bench framework flag>`, each bench
test emits a JSON cell into `<location>`. Schema:

```json
{
  "schema_version": 1,
  "cells": [
    {
      "name": "<bench name>",
      "duration_ms": 0,
      "aggregate_throughput": 0,
      "rss_delta_kb": 0,
      "user_cpu_ms": 0,
      "sys_cpu_ms": 0
    }
  ]
}
```

Compare across versions with `jq`:

```bash
jq '.cells[] | select(.name=="<bench>") |
    {duration_ms, aggregate_throughput, rss_delta_kb}' \
   <path/to/bench_A.json> <path/to/bench_B.json>
```

## Extending this file

When a new scenario is used for benchmarking:

- Add it to the relevant section (core / throughput / sweep).
- Include workload description, typical wall-clock, and rationale.
- Document known-bad failure modes alongside.

Keep the file short and scannable — investigative context stays in
session reports, not here.
