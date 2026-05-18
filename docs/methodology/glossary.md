# Glossary — SDK-5360 acronyms and shorthands

A glossary of the internal vocabulary used in SDK-5360 fu7-N sessions,
verdicts, and memory entries. Helps a new contributor or a future
agent parse historical artefacts.

## Test cells (canonical short names)

| Short | Full cell name | What it exercises |
|---|---|---|
| **SLU** | `SdkBenchmarkTest.SingleLargeUpload` | Single ~1-10 GiB upload; primary throughput benchmark. |
| **T1** | `SdkWsUploadTest.ActivePoolUsesParallelConnections` | Pool fan-out across 8 worker threads; timing-assertion cell. |
| **IP** | `SdkWsUploadTest.InvalidPinnedSessionFallsBackToFreshSession` | Session-URL fallback when the pinned USC session becomes invalid; timing cell. |
| **B9** | `SdkWsUploadTest.B9ClosedThrottleReconnectPacing` | Throttle reconnect pacing assert (`delta12Ms >= CONNRETRYINTERVAL`); timing-assertion cell needing `--gtest_repeat=15` + taskset. |
| **MN** | `SyncTest.BasicSync_MassNotifyFromLocalFolderTree` | Sync-upload livelock regression test; 16k × ~17B files. |
| **Overquota** | `SdkWsUploadTest.OverquotaDuringTransfer` | Mid-transfer overquota injection via `onWsChunkSendOverquota` hook. |
| **Resume** | `SdkResumableTrasfers` (SdkTest) | Resumable uploads + throttle pacing (note the typo `Trasfers` is in the upstream cell name). |
| **RepeatedPauseResume** | `SdkWsUploadTest.RepeatedPauseResumeMixedPools` | Cross-pool fairness under pause/resume cycles. |
| **MultiplePauseResume** | `SdkWsUploadTest.MultiplePauseResumeCycles` | Similar; multiple cycles. |
| **TransferStats** | `SdkTest.SdkTestTransferStats` | Transfer statistics aggregation; known Windows flake (HR38). |
| **MultiUpload** | `SdkTest.SdkTestMultipleUploads` | 2 parallel transfers (large + small). |
| **MultiUploadExpanded** | `SdkTest.SdkTestMultipleUploadsExpanded` | One file per USC size class. |
| **5x160** | `SdkTest.SdkTestUploads` | 5 × 160 MB sequential uploads. |

## WS upload internals

| Term | Definition |
|---|---|
| **WS upload** | WebSocket-based upload path; the v2 replacement for the legacy CurlHttpIO chunk path. |
| **USC** | "Upload Session" / Upload-Storage Connection; the API command class that allocates a pool URL triple. |
| **Pool** | A `WsPool` instance — one per (size class, account); owns the worker thread + connection set. |
| **PoolMgr** | `WsPoolMgr` — owns the pool set, vends USC URLs, handles refresh / fallback. |
| **WsConn** | Single libcurl connection in WS mode; owned by a pool worker. |
| **UploadEngine** | The top-level facade; owns `Impl` which owns the PoolMgr and WS state. |
| **Impl** | `UploadEngine::Impl` — the actual implementation; lives in `wsupload_engine.h` since fu7-16 Step 0. |
| **Pinned session** | A USC session URL the pool retains across transfers; "pinned" because the API discourages frequent re-allocation. |
| **Fresh pool** | A pool with a newly-allocated USC URL; replaces a retired pool. |
| **Distress** | Server event=5 indicating the storage host wants the client to disconnect / re-allocate. |
| **Throttle (event=6)** | Server-side rate-limiting acknowledgement; pacing assert in B9 is built around it. |
| **ChunkIngested (event=1)** | Server acknowledgement that a chunk was accepted. |

## Sessions and verdicts

| Term | Definition |
|---|---|
| **fu7-N** | "followup7-N" — the N-th sub-session in the followup7 series (`pathToVictory2` cycle). fu7-2 through fu7-17 inclusive. |
| **followupN** | The general session-naming pattern across the `pathToVictory` cycles. |
| **FollowupRequest.md** | Input file for a session — defines goals, hard rules, stop conditions. |
| **FollowupVerdict.md** | Closing file for a session — per-Goal status, final sweep numbers, verdict label. |
| **VALIDATED_TECHQA_READY** | Verdict label: all Goals DONE, sweep ≥80% PASS, no NEW TSAN. |
| **PARTIAL_DEFER** | Verdict label: one or more Goals deferred with explicit HR21-compatible justification. |
| **REGRESSION_DETECTED** | Verdict label: a fix introduced a regression; revert + escalate. |
| **BLOCKED_ON_USER** | Verdict label: decision requires user input. |
| **techQA** | The post-feature-complete QA pass; gates merge to develop. |
| **MR** | Merge request (GitLab) — the SDK-side equivalent of a GitHub PR. |

## Hard rules

| Term | Definition |
|---|---|
| **HR<N>** | A hard rule. See [hard_rules_catalog.md](hard_rules_catalog.md). |
| **RCA** | Root-cause analysis. Used for performance / regression investigations. |
| **HEAD** | The current git commit; cited by hash in verdicts. |
| **post-resign HEAD** | The hash after the user re-signs commits (different from pre-resign HEAD). |

## Bench + TSAN

| Term | Definition |
|---|---|
| **bench JSON** | `bench_report_<PID>.json` emitted by `SdkBenchmarkTest.*` cells; schema versioned. |
| **top-2 mean** | Mean of the top-2 measurements in n=3 (or top-3 in n=5); the canonical bench comparison statistic. |
| **fast-mode** | A bench iteration with no observed throttle storm; the upper envelope of throughput. |
| **slow-mode** | A throttle-stormed iteration; high variance, dominant in long sweeps. |
| **TSAN** | ThreadSanitizer; the `-fsanitize=thread` build (`dev-unix-tsan` preset). |
| **v2-surface** | The 4-cell TSAN filter (T1, B9, IP, Overquota) that covers the WS pool + worker thread + URL fallback paths. |
| **NEW race** | A TSAN race present on fu7-N but absent on develop; HR23 says fix. |
| **carry-over race** | A race present on both develop and fu7-N; pre-existing, not this session's responsibility. |

## Infrastructure

| Term | Definition |
|---|---|
| **USERAGENT** | The `--USERAGENT:JenkinsCanSpam-SDK` flag; required for `scopedToPro` (HR-feedback). |
| **scopedToPro** | RAII helper that upgrades the test account to PRO_I via `setAccountLevel`; gated on USERAGENT. |
| **environment2.txt** | The integration-test env vars (account credentials, API URLs); sourced before running. |
| **pid_<PID>** | The per-process test artefact directory under `${HOME}/mega_tests/`. |
| **PID dir** | Short for `pid_<PID>`. |

## CMake presets

| Preset | OS | Distinguishing feature |
|---|---|---|
| `dev-unix-wsupload` | Linux | The standard fu7-N workhorse build. |
| `dev-unix-wsupload-benchOn` | Linux | + `MEGA_BENCH_FRAMEWORK_ENABLED=ON`. |
| `dev-unix-tsan` | Linux | + `-fsanitize=thread`. |
| `dev-unix-strict` | Linux | + `-Wmismatched-tags` hard error (HR42). |
| `dev-unix-hooks-off` | Linux | + Release / NDEBUG; hooks compile-gate (HR44). |
| `dev-macos-*` | macOS | Counterparts (post-fu7-17). |
| `dev-windows-*` | Windows | Counterparts (post-fu7-17). |
