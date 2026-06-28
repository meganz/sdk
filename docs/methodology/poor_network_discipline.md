# Poor-Network / Packet-Loss Test Discipline (SDK-5360 WS-uploads)

**Why this exists.** WS uploads (SDK-5360) shipped to QA with a stall under poor network (high RTT + packet
loss) and a reported crash under 100% loss. The root cause was a *test-coverage gap*: every bench/regression
ran on a clean network, so the handshake-convoy stall, the per-chunk-ack RTT sensitivity, and the
connection-instability-under-loss behaviour were never exercised. This document makes poor-network testing a
**permanent, repeatable part of the WS-upload methodology** so the gap can never silently reopen. (The boss
ask: *"ensure this will be properly tested and standardized to avoid it happening ever again."*)

## The standardized assets (committed)
- **`scripts/ci/net_profiles.env`** — the single source of truth for network profiles
  (`name="delay_ms loss_pct kbps"`):
  `clean(0/0/0)`, `poorRTT(500/0/0)`, `loss5/20/50(500/N/0)`, `loss100(500/100/0)`, **`qaexact(500/10/1000)`**
  (the exact iOS "Very Bad Network" QA profile).
- **`scripts/ci/netem_profile.sh`** — runs any command inside a network namespace whose **both** veth ends
  carry `tc netem` (delay + loss applied **each way**), with NAT + per-ns DNS so it still reaches real MEGA.
  Bandwidth is applied via the SDK's `MegaApi::setMaxUploadSpeed` (env `MEGA_NET_MAXUPLOAD_KBPS`), **not** netem
  `rate` — deterministic and the exact knob the app uses. Runs as the normal user (per-op `sudo`); the child
  runs as the user with the caller's env preserved. Idempotent teardown on EXIT/INT/TERM. Validated against
  real `wss://gfs*.userstorage.mega.co.nz`.
- **`SdkBenchmarkTest.QaExactSingleFile`** (`tests/integration/benchmark/`) — the QA tester's 4 MiB single-file
  upload, the faithful repro vehicle. Reads `MEGA_NET_MAXUPLOAD_KBPS` to cap bandwidth.
- **`scripts/ci/bench_matrix_runner.sh`** — the single **serial** Bench Runner: drains a queue of
  `LABEL|BINARY_DIR|PROFILE|GTEST_FILTER|N|TIMEOUT_S` units ONE AT A TIME, shaping each via `netem_profile.sh`,
  collecting the bench JSONL (throughput + `rss_max_kb` + `rss_delta_kb` + CPU) + the trace log. Resumable;
  fails fast on a missing `environment2.txt`.

## Hard rules (poor-network)
- **HR57 — poor-network coverage.** No WS-upload close-gate without the degraded-profile bench matrix run
  (QaExact ×{clean, poorRTT, loss5} measured; loss20/50/100 as graceful-degradation gates) on the load-bearing
  cells, paired against develop + the pre-fix baseline.
- **HR58 — three-way + full-matrix fairness.** Bench every WS-upload change against BOTH develop AND the
  pre-fix session baseline, full RSS + throughput + CPU distributions, across the network-profile × file-variant
  matrix. The RSS-optimal strategy can INVERT under loss, so clean-network numbers alone never validate a fix.
- **HR59 — graceful degradation.** 100% loss must produce a defined failure + clean teardown (never crash/hang);
  >=20% loss should at least complete-or-fail like develop (no indefinite stall).
- **HR-serial (extends HR56).** ANY resource-consuming run — bench, sweep, OR compile/`cmake --build` — runs
  strictly sequentially, never overlapping another (a build's CPU/RSS contaminates a concurrent bench). Work in
  phases: build ALL binaries (no bench running), THEN bench (no build running). Interleave DEV/BASE/FIX arms
  per (cell,profile) — separate-time matrices drift on unshaped cells.

## How to run (recipes)
**Reproduce the QA stall:**
```
cd <benchOn-build>/tests/integration; set -a; source environment2.txt; set +a
scripts/ci/netem_profile.sh --profile qaexact -- \
  ./test_integration --CI --COUT --USERAGENT:JenkinsCanSpam-SDK --gtest_filter='SdkBenchmarkTest.QaExactSingleFile'
```
**Faithful crash attempt (100% loss + ASan):** build with `ENABLE_ASAN=ON`, `LD_PRELOAD=$(gcc -print-file-name=libasan.so)`,
`ASAN_OPTIONS=detect_leaks=0`, `--profile loss100`. (Note: the curl-ws/WS-pool path is curl over TCP; a true
SIGSEGV may be iOS-platform-specific — capture the iOS `.ips`/Xcode crash log to diagnose.)
**Three-way matrix:** queue `DEV|BASE|FIX` units interleaved per (cell,profile) into `bench_matrix_runner.sh`;
develop arm = `SdkTest.SdkTestBenchmark*` cells (meas-overlay); aggregate with `scripts/ci/aggregate_bench.py`.

## CI wiring (HR49)
- **Deterministic, root-free:** add an in-SDK seeded fault hook (drop/delay at `curl_ws_send`/`curl_ws_recv`/
  handshake, gated by `MEGASDK_DEBUG_TEST_HOOKS_ENABLED`) + `SdkWsUploadTest.NetCondition*` gtest gates; run in
  the standard + TSAN Jenkins stages. (Designed; implement alongside the heavy-loss fix.)
- **netem repro (privileged):** `sudo scripts/ci/netem_profile.sh` documented as a manual/privileged operator
  procedure on a dedicated agent — NOT the default MR gate (needs root + a namespace).
