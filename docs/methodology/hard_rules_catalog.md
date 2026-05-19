# Hard rules catalog — HR1..HR51

The canonical list of operational hard rules accumulated across SDK-5360
fu7-N sessions. Each rule has an ID, a name, a one-line rule statement,
a one-sentence rationale, and the fu7-N session that introduced it.

Future sessions add HR50+. Do not renumber retroactively — IDs are
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
| HR15 | No ≥5% throughput regression | Top-2 mean / median throughput delta vs the previous fu7-N must be ≤5% on critical cells (SLU, T1, IP). | The throttle storm noise floor is ~5%; anything above is real signal. | fu7-7 |
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
| HR45 | getrusage RSS+CPU snapshot | `SdkTest::TearDown` captures `getrusage(RUSAGE_SELF)` delta; bench cells emit it into the JSON. | RSS+CPU axes are needed for comparison with develop; without per-test snapshots the data is unsignalled. | fu7-15 |
| HR46 | B9 taskset stress | B9 cell additionally runs under `taskset -c 0 nice -n 19`. | Simulates macOS-style single-CPU scheduling that surfaces the Distress race fu7-14 missed. | fu7-15 |
| HR47 | Agent wait budget ≥90 min | Long-running tests (MN n=5, full SLU n=15, full WS-other sweep) get ≥90 min agent wait budget. | fu7-15's 60-min budget killed MN iter 4 mid-run; HR47 prevents the batch-stitch fallback. | fu7-16 |
| HR48 | Defer only with measured wall | Defer rationale must include a measured wall estimate (not "TBD"). | Unmeasured defers accumulate into vague backlog items. | fu7-17 |
| HR49 | Jenkins green before push | All Jenkins MR stages (Linux/macOS/Windows + bench + tsan when triggered) must be green before user push. | Push-then-revert cycles waste user time; the gate is the MR build, not local-only. | fu7-17 |
| HR50 | Shell-detachment mitigation | Long-running test children launched by agents MUST use `setsid` (or `nohup`) with explicit `SID == PID` + `PPID == 1` verification before declaring the run "started." | Subshell reap (timeout, session reset) sends SIGHUP to non-detached children → silent termination, no crash markers. | fu7-17-2 (Failure 2 RCA, pid_996793) |
| HR51 | Jenkins-MR mirror gate | Before declaring `TECHQA_READY` for any fu7-N close, run the `test_integration` binary with empty `--gtest_filter` (3 sub-runs to mitigate long-process SIGSEGV) on Linux. The agent's coverage floor must structurally include every cell Jenkins MR runs by default. | HR49 catches Jenkins failures **after** push; HR51 catches them **before** push. fu7-17/fu7-17-2 had `SdkWsUploadTest.StopStartSameEngineDuringTransfer` + `SyncTest.TwoWay_Highlevel_Symmetries` caught by HR49 post-push, forcing user-side iteration. | fu7-18 |

## Reading the catalog

- **Hot path** (every session uses): HR1, HR14, HR15, HR21, HR23, HR24,
  HR33, HR34, HR40, HR42, HR43, HR47.
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

## Adding HR50+

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
