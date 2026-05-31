# Hard rules catalog — HR1..HR56

The canonical list of operational hard rules accumulated across SDK-5360
fu7-N sessions. Each rule has an ID, a name, a one-line rule statement,
a one-sentence rationale, and the fu7-N session that introduced it.

Future sessions add HR57+. Do not renumber retroactively — IDs are
stable references in session reports and Verdicts.

## Catalog

| HR | Name | Rule | Rationale | Origin |
|---|---|---|---|---|
| HR1 | Agent does not push | The agent never executes `git push`; commits land unsigned, user re-signs (GPG pinentry) and pushes. | Pinentry doesn't work in non-interactive subprocesses; user retains the signing identity. | fu7-1 |
| HR4 | Develop-side parity build | Maintain a `~/repo/sdk-develop` worktree with transient bench instrumentation cherry-picked; remove at session close. | Develop-binary comparison needs the same getrusage/bench wrappers; the cherry-pick is transient to keep develop clean. | fu7-2 |
| HR8 | Worktree role map | `~/repo/sdk` is the canonical fu7-N working tree; develop and ship worktrees are auxiliary. | Switching the main worktree between branches risks losing heavy build artefacts and instrumentation. | fu7-3 |
| HR9 | One TU per refactor commit | Each modularisation commit moves exactly one logical unit; verify build + bench gate before chaining. | Bisecting a broken modularisation is much easier when each commit is atomic. | fu7-6 |
| HR13 | No unsolicited test relaxation | Never weaken assertions to make a test pass; fix the code instead. | Hides bugs and erodes the test-discipline contract (CLAUDE.md). | fu7-6 |
| HR14 | Per-commit bench gate | SLU n=3 + B9 n=10 + MN n=1 minimum on every wsupload-touching commit; fu7-18 amends to also include Tier S empty-filter Jenkins-MR smoke before push (see HR51). | Even cosmetic refactors can shift lock ordering / inlining and regress throughput. | fu7-7 |
| HR15 | No ≥5% throughput regression + bench-proof distribution stats | Top-2 mean / median throughput delta vs the previous fu7-N must be ≤5% on critical cells (SLU, T1, IP). **fu7-20 amendment**: every bench-proof deliverable (`Goal1_bench_proof/*.md` and any `master_summary.md`) reports full per-axis distribution (min / p25 / median / mean / p75 / p95 / max / σ / σ/μ) for every cell × baseline × axis — **no `TBD` placeholders are permitted** in any committed bench-proof markdown. Use `aggregate_bench.py` to extract from per-iter JSONL. | The throttle storm noise floor is ~5%; anything above is real signal. fu7-19's master_comparison.md was load-bearing but per-cell markdowns carried 5-11 `(TBD)` cells per file (FU-7) — the new floor closes that gap. | fu7-7 (fu7-20 amended) |
| HR16 | Push protocol | User re-signs via `git rebase -i <base> --exec 'git commit --amend -S --no-edit'`, then `git push --force-with-lease`. | Standardises the unsigned→signed transition without losing commit ordering. | fu7-7 |
| HR18 | InvalidPinned stress | IP cell runs at n≥13 in the closing sweep. | The IP transition is timing-sensitive; n=10 was insufficient for a fu7-5 race. | fu7-5 |
| HR21 | Defer with explicit justification | A goal may be deferred only with documented HR-compatible rationale (risk, wall budget, dep blocker). | Silent defers accumulate; explicit defers leave a trail the next session can act on. | fu7-9 |
| HR22 | GTEST_SKIP audit | Every `GTEST_SKIP` must have a justification recorded in `tests/integration/.../skip_inventory.md`. | Unjustified skips hide rotting tests. | fu7-9 |
| HR23 | TSAN NEW-vs-develop must be FIXED | A race present on fu7-N but absent on develop is fixed before close. | This is the only signal that the WS changes introduced a thread-safety regression. | fu7-9 |
| HR24 | megaclient.cpp diff ≤700 | The net diff vs origin/develop in `src/megaclient.cpp` stays ≤700 LOC. Historical reduction: fu7-6 1890→772, fu7-10 islands 772→663, fu7-17 G2.a wsupload_engine split absorbed ~470 LOC from header into .cpp body → current net +195 at fu7-18 HEAD. SATISFIED. | Keeps the merge surface small; large megaclient.cpp churn invites conflicts. | fu7-10 |
| HR26 | Helper-cluster relocation safety | Tests in `SdkTest_test.cpp` anonymous-namespace helper clusters relocate via gtest rename, not whole-TU move. | fu7-6/fu7-7 attempts to move whole TUs broke transitive helper deps; rename-in-place is safer. | fu7-7 |
| HR27 | Bench mandatory before commit | Apply HR14 even for refactors, renames, doc-touching-source changes. | Regressions can sneak in via subtle changes; the default is "run the gate". | fu7-7 |
| HR30 | Vmga / personal-path scrub | No hard-coded `/home/vmga`, no personal paths in committed source or BENCHMARKS.md. | Jenkins runs as a different user; hard-coded paths break the CI. | fu7-9 |
| HR31 | Multi-session bench | Comparison runs alternate fu7-N and develop iterations on the same hardware on the same day. | Throttle storm conditions vary by hour; alternating iterations keeps the comparison paired. | fu7-9 |
| HR33 | Final sweep tiers | Closing sweep tiers (all REQUIRED — see `regression_sweep_cadence.md` for full text): Tier S Jenkins-MR mirror floor (empty `--gtest_filter`, 3 sub-runs) + Tier 1 every `SdkWsUploadTest.*` cell n=15 + Tier 2 MN n=5 ONE batch + every other `SyncTest.*` n=3 + Tier 3 HR38 paired n=5 + Tier 4 bench paired (SLU n=15) + Tier 5 TSAN refresh 10 v2-surface cells n=3. | The Verdict's "PASS" claim must be statistically defensible AND mirror Jenkins' breadth. | fu7-11 (fu7-18 tiered) |
| HR34 | Inter-Goal sweep | Run HR14 gate between Goals within a session, before moving on. | Catches cross-Goal interactions that per-commit gates miss. | fu7-10 |
| HR36 | Document deferred items | Carry-forward list at session close lists each deferred item with risk + estimated wall. | The next session needs an actionable inventory. | fu7-10 |
| HR38 | TransferStats Linux/Windows split | `SdkTestTransferStats` is a known Windows flake; validate on Linux + record Windows status separately. | The Windows flake is a CI infra artifact, not an SDK defect. | fu7-12 |
| HR39 | Test-accessor wrap | `getClientForTesting()` / `executeOnThreadForTesting()` callers wrap in `WSUPLOAD_REQUIRE_TEST_HOOKS` for hooks-off builds. | Keeps hooks-off Release builds compiling without conditional `#if`. | fu7-12 |
| HR40 | MN ONE batch | `BasicSync_MassNotifyFromLocalFolderTree` runs as one `--gtest_repeat=5`, never 3+2 stitched. | Batch stitching averages out the envelope drift that one-batch runs reveal. | fu7-15 |
| HR41 | bench_report JSON artifact | The bench-stage build must emit `bench_report_*.json`; archive it as a Jenkins artifact. | The JSON is small, version-locked, diffable; preferred to log-grep. | fu7-12 |
| HR42 | dev-unix-strict pre-push | Build `--preset dev-unix-strict` before pushing; grep narrow to in-changeset files; zero hits is the bar. | Linux gcc silently accepts struct/class tag mismatches and dstime narrowing that MSVC / clang-mac reject. | fu7-14 |
| HR43 | Timing cells `--gtest_repeat=15` | Cells with timing-based asserts run at n≥15 in WS-other smoke and the final sweep; fu7-18 extends to ALL 30 `SdkWsUploadTest.*` cells (not just the enumerated 7 — `StopStartSameEngineDuringTransfer` Jenkins-MR escape motivated the structural expansion). | n=1/10 hides flakes that n=15 surfaces; cell-name semantics are not a reliable filter for timing sensitivity. | fu7-13 (fu7-18 expanded) |
| HR44 | dev-unix-hooks-off compile gate | Build `--preset dev-unix-hooks-off` to verify the hooks-off Release build still compiles after hook-ABI changes. | The hook-ABI redesign (fu7-15) requires both ON and OFF compile paths to stay green. | fu7-15 |
| HR45 | getrusage RSS+CPU snapshot, no-TBD in reports | `SdkTest::TearDown` captures `getrusage(RUSAGE_SELF)` delta; bench cells emit it into the JSON. **fu7-20 amendment**: every paired bench-proof comparison vs develop reports **real RSS and CPU numbers** (user_cpu_ms + sys_cpu_ms) per cell × baseline — `TBD` is not an acceptable placeholder. If a measurement path is missing for a baseline, document the dual-semantic and fill with the closest equivalent extracted from `[ProcessStats]` / `[BenchProcessStats]`. | RSS+CPU axes are needed for comparison with develop; without per-test snapshots the data is unsignalled. fu7-18 + fu7-19 carried RSS/CPU TBDs into the final bench-proof; the no-TBD floor closes that. | fu7-15 (fu7-20 amended) |
| HR46 | B9 taskset stress | B9 cell additionally runs under `taskset -c 0 nice -n 19`. | Simulates macOS-style single-CPU scheduling that surfaces the Distress race fu7-14 missed. | fu7-15 |
| HR47 | Agent wait budget ≥90 min | Long-running tests (MN n=5, full SLU n=15, full WS-other sweep) get ≥90 min agent wait budget. | fu7-15's 60-min budget killed MN iter 4 mid-run; HR47 prevents the batch-stitch fallback. | fu7-16 |
| HR48 | Defer only with measured wall | Defer rationale must include a measured wall estimate (not "TBD"). | Unmeasured defers accumulate into vague backlog items. | fu7-17 |
| HR49 | Jenkins green before push | All Jenkins MR stages (Linux/macOS/Windows + bench + tsan when triggered) must be green before user push. | Push-then-revert cycles waste user time; the gate is the MR build, not local-only. | fu7-17 |
| HR50 | Shell-detachment mitigation | Long-running test children launched by agents MUST use `setsid` (or `nohup`) with explicit `SID == PID` + `PPID == 1` verification before declaring the run "started." | Subshell reap (timeout, session reset) sends SIGHUP to non-detached children → silent termination, no crash markers. | fu7-17-2 (Failure 2 RCA, pid_996793) |
| HR51 | Jenkins-MR mirror gate | Before declaring `TECHQA_READY` for any fu7-N close, run the `test_integration` binary with empty `--gtest_filter` (3 sub-runs to mitigate long-process SIGSEGV) on Linux. The agent's coverage floor must structurally include every cell Jenkins MR runs by default. | HR49 catches Jenkins failures **after** push; HR51 catches them **before** push. fu7-17/fu7-17-2 had `SdkWsUploadTest.StopStartSameEngineDuringTransfer` + `SyncTest.TwoWay_Highlevel_Symmetries` caught by HR49 post-push, forcing user-side iteration. | fu7-18 |
| HR52 | Regression-sweep n-floor + no-defer | v2 / bench / timing-sensitive surfaces run at the elevated n-floor in EVERY closing sweep — Tier 1 n=15 (HR43), Tier 2.1 MN n=5 absolute (HR40), Tier 2.3 strict cells n=10, Tier 3 HR38 paired n=10 (raised from n=5), Tier 4 SLU n=15 and other bench cells n=10 paired (raised from n=3), Tier 5 TSAN refresh n=5 fu7-N + n=5 develop parity (raised from n=3). Tier 2.2 (other SyncTest cells fu7-N does not modify) stays at n=3 (HR33). **No tier may be deferred on time-budget grounds unless measured wall proves the cost** (HR48 + this rule). Audit-only Δ=0 deferrals are forbidden (HR21). | fu7-19's Jenkins MR caught `SyncTest.DetectsAndReportsSyncProblems` on macOS precisely because Jenkins applies broader-n than the agent sweep used; n=1/3 misses what n=10/15 catches. Elevated-n is the new floor for every fu7-N close. | fu7-20 |
| HR53 | Bench-procedure axis-coverage | Every cell × baseline comparison MUST report throughput Δ% + RSS Δ% + CPU Δ% with full distribution stats (min / p25 / median / mean / p75 / p95 / max / σ / σ/μ). A cell that cannot report any of those three axes vs develop is procedure-incomplete and BLOCKS the session close-gate. Mitigations in priority order: (a) backport the missing cells to develop's HR4 meas-overlay; (b) add develop-side getrusage instrumentation for RSS + CPU emission; (c) if neither is possible, FAIL the session with an explicit incomplete flag — never silently fall back to `n/a — missing field in baseline`. | fu7-20 Session 2 surfaced `n/a — missing field in baseline` for SLU + LargePlusManySmall vs develop (cells absent) and for RSS/CPU vs develop (HR4 meas-overlay emits only totalMs + aggregateKBps). A verdict tool that cannot compare an axis cannot certify it. | fu7-20 Session 3 |
| HR54 | RSS must be strictly LOWER than develop (TWO axes: median rss_delta_kb + median-of-rss_max_kb peaks) | The whole purpose of WS upload is to MINIMIZE RSS by avoiding the large in-memory buffers the legacy HTTP path carries. **Both axes are collected on every bench (HR45 amendment) and BOTH are analyzed per HR54.** Axis 1 (`rss_delta_kb` median across n=15) measures per-iter heap growth. Axis 2 (`rss_max_kb`, computed per-iter as the iter's `endMaxRssKB` from `[BenchProcessStats]`, then median across n=15) measures the iter-end peak RSS — under single-process topology this captures the v2 RSS-reduction premise directly (legacy HTTP buffers whole files → high peak; v2 streams 4 MiB chunks → low peak). Pass criteria: **Δ% < -2% AND p<0.05 vs develop on EVERY cell × axis** (both `rss_delta_kb` median AND `rss_max_kb` peak-median). Exception: smaller-file cells (e.g., ManySmall + 1kSmall with 1 MiB files) where v2's fixed pool overhead dominates over per-iter buffering may accept paired-or-worse on EITHER axis via the manifest's `rss_paired_allowed` per-cell override — but **every override REQUIRES a follow-up ticket in the verdict file to investigate whether the RSS can be reduced or must be assumed-acceptable** (HR54 amendment fu7-20 Session 3 post-Phase 1: never silently accept paired RSS — always file the follow-up). For large workloads (SLU + LargePlusManySmall, where the v2 premise is the design pay-off) override is NOT eligible — these MUST be strictly lower than develop. | fu7-20 Session 2's bench-proof discovered an apparent +14 MB/iter RSS regression that Session 3 traced to single-process plateau topology making median `rss_delta_kb` degenerate (develop median=0 on small workloads); the v2 RSS-reduction premise (peak RSS) requires `rss_max_kb` to be visible. Session 3 Phase 1 data showed v2 wins -42 %/-34 % peak vs develop on SLU/LPMS (premise confirmed) but +47 %/+23 % peak on ManySmall/1kSmall (pool-overhead amortized). Both axes carry different information; both must be checked; small-file pool-overhead is an accepted design trade-off WITH follow-up. | fu7-20 Session 3 |
| HR55 | Bench-proof verdict emits prose, not just bands | `aggregate_bench.py` and `master_summary.md` MUST emit explicit verdict prose per cell × baseline ("RSS WIN: X KB improvement vs develop, p<P, n=N" or "RSS REGRESSION: …"), not leave the band table to speak for itself. The verdict is the contract; the band table is the evidence behind it. | fu7-N sessions through fu7-19 carried per-cell `(TBD)` placeholders and let the throughput table imply a verdict; the human readers (incl. user 2026-05-28) found the band table ambiguous in isolation. Explicit prose closes that gap. | fu7-20 Session 3 |
| HR56 | Benchmarks are SEQUENTIAL, never parallel | Every benchmark cell — paired or unpaired, candidate or baseline — runs as a SINGLE process at a time. Within that process iterations run sequentially via `--gtest_repeat=N`; across processes/cells/variants the runs are scheduled SEQUENTIALLY (one finishes before the next starts). Parallel `test_integration` processes (even targeting different cells) are FORBIDDEN — they share the same upload throttle, the same MEGA test account, the same network path, and the same CPU cores. Parallel execution divides throughput across sockets, perturbs RSS measurement (kernel scheduling jitter), and invalidates throttle-storm exposure normalisation. The bench driver (`tests/integration/run_bench.sh`, `scripts/ci/find_bench_reports.sh`) and every fu7-N session driver MUST enforce this. The rule applies to BOTH the bench gate (HR14) AND every closing-sweep tier (HR33, HR52). | fu7-20 Session 3: user flagged that an ambiguous "cold-start per process × n=15" wording in an AskUserQuestion could have read as parallel; codified to prevent any future drift. The principle predates fu7-N (see memory `feedback_tests_sequential`, Sept 2024) but was never formalised as an HR — making it implicit-but-load-bearing. | fu7-20 Session 3 |

## Reading the catalog

- **Hot path** (every session uses): HR1, HR14, HR15, HR21, HR23, HR24,
  HR33, HR34, HR40, HR42, HR43, HR47, HR52.
- **Per-major-change**: HR4 (develop comparison), HR31 (multi-session
  bench), HR45 (getrusage).
- **Defensive / compile**: HR42 (strict preset), HR44 (hooks-off
  preset).
- **Hygiene / documentation**: HR16 (push protocol), HR22 (skip
  audit), HR30 (path scrub), HR36 (carry-forward), HR48 (measured
  defer), HR49 (Jenkins green).
- **Process management**: HR50 (setsid for agent-driven long-running
  children).
- **Pre-push structural floor** (added fu7-18): HR51 (Jenkins-MR mirror
  before TECHQA_READY).
- **Closing-sweep n-floor** (added fu7-20): HR52 (v2/bench/timing
  surfaces n≥10-15 in every fu7-N close; no time-budget deferral).
- **Bench-proof axis + premise + verdict prose** (added fu7-20
  Session 3): HR53 (no `n/a` axis coverage), HR54 (RSS strictly
  LOWER than develop = the v2 design premise), HR55 (verdict
  emits explicit prose per cell, not just band tables).
- **Bench-run topology floor** (added fu7-20 Session 3): HR56
  (benchmarks always sequential, never parallel processes) —
  formalises the long-implicit rule that was previously only in
  the `feedback_tests_sequential` memory entry.

## Adding HR57+

A new HR is justified when:

1. A session encountered a class of regression / mistake that a
   one-line rule would have prevented.
2. The fix is mechanical or process-level — not a code change.
3. The team agrees the rule applies to all future similar work,
   not just this session.

When adding:

- Append the row at the end of the catalog (don't insert mid-table).
- Add a memory entry (`feedback_hr<N>_<topic>.md` if rule-level;
  embedded in `project_*_results.md` if session-level).
- Reference the incident that motivated it (commit hash + session).

## Removing an HR

A rule may be retired when:

1. The underlying tooling / preset / process is permanently gone.
2. A stronger rule subsumes it (and the new rule explicitly says so).

Do not delete the row — mark with `~~strike~~` and add a note in the
"Origin" cell pointing to the superseding rule.
