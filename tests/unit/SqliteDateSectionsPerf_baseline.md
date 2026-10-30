# groupAllNodesByDate performance baseline (SDK-6582)

> Reference numbers for same-machine comparison only; nothing here is asserted.

## Method

- Desktop Release build, `SqliteNodesPerfGroupByDateTest` in `SqliteDateSectionsPerf_test.cpp`.
- Datasets: pseudo-random node handles; committed after populating (as after fetchnodes),
  so queries read the main DB file, not a WAL. The log prints the db/wal sizes.
- Folder tree: root → top folder → sub-folder → files (3 levels), plus 5 extra levels in
  the depth-8 shape. Every 100th sub-folder is sensitive.
- Per cell: the first execution (possibly including the statement prepare), then the
  median of 3. The OS page cache is not dropped.
- "Before" = the parent of the SDK-6582 commit (pre-SDK-6582 SQL and indexes) with this
  suite's `SqliteDateSectionsPerf_test.cpp` taken from the SDK-6582 commit; "after" = the
  SDK-6582 commit. Equal checksums per cell mean equal sections.

Variance: the pre-SDK-6582 SQL reads random table pages, so its time depends on what the
OS still caches. The identical cell measured twice agreed within ~20%, but cells run later
in one run can be 10x+ faster than the first heavy one. The per-parent SQL varied ~20%.

## Prototype results (before this suite existed)

Measured in a scratch worktree during design, with the same method, single executions:

| `ORDER_MEDIATS_DESC` | 1M before | 1M after | 2M before | 2M after |
|---|---|---|---|---|
| 5K folders, `ALL_VISUAL_MEDIA`, root | 6.2 s | 0.53 s | 115.9 s | 0.85 s |
| 5K folders, `VIDEO`, root | 1.2 s | 0.20 s | 21.5 s | 0.29 s |
| 200K folders, `ALL_VISUAL_MEDIA`, root | 79.0 s | 2.1 s | 140.4 s | 2.7 s |
| 200K folders, VISUAL + exclude + hide sensitive | 91.4 s | 2.5 s | 159.4 s | 2.8 s |

| `ORDER_MODIFICATION_DESC` | 1M before | 1M after | 2M before | 2M after |
|---|---|---|---|---|
| 5K folders, `ALL_VISUAL_MEDIA`, root | 62.6 s | 0.44 s | 147.7 s | 0.86 s |
| 5K folders, `VIDEO`, root | 11.8 s | 0.28 s | 22.4 s | 0.27 s |
| 200K folders, `ALL_VISUAL_MEDIA`, root | 10.0 s | 2.1 s | 28.7 s | 3.1 s |
| 200K folders, VISUAL + exclude + hide sensitive | 18.5 s | 2.6 s | 29.7 s | 3.3 s |

Costs of the widened indexes (prototype values from the design phase): re-fetchnodes on an
existing DB (per-node counter/flags UPDATE) +29% / +35%; DB +~20 B/row; SQLite heap peak
during the query up to +10 MB at 2M / 200K folders. The one-time migration on the first open
after upgrading was re-measured with the final code; see "Index migration" below.

## Earlier attempts (do not repeat)

| Attempt | Outcome |
|---|---|
| opt-1: grouped mime as one `IN (...)` instead of UNION ALL of per-mime CTEs | kept (simpler, perf-neutral) |
| opt-2: `ANALYZE nodes` | reverted: production never runs ANALYZE |
| opt-3: integer day buckets instead of `strftime` | reverted: no measurable gain |
| opt-4: materialise all descendants, filter `nodehandle IN subtree` | reverted: fixed O(subtree) cost, VIDEO ~3x slower |
| folders-only descendant walk, filter `parenthandle IN folders` | rejected: 4–28x slower with many folders and few candidates (200K folders, 0.1% video: 185 ms → 5.3 s) |
| per-parent up-walk + widened indexes (SDK-6582) | adopted |
| two passes: distinct parents of the candidates (`SELECT DISTINCT parenthandle`), their up-walks, then one `GROUP BY gid` pass filtered by `parenthandle IN (scoped parents)` | rejected: 2.5–3.9x slower with 200K folders (2M, VISUAL, root, Month: 3.2 s → 10.3 s); faster only at 5K folders / Day (1.73 s → 1.26 s) |
| same, with the parents from `GROUP BY parenthandle` (sorted) | rejected: removes the 200K-folder slowdown, but the second index scan costs more than it saves: Day 4–13% faster, Month up to 40% slower (2M / 5K, VISUAL, root: 1.04 s → 1.46 s) |

opt-1..opt-4 were measured (SDK-6171) on a fixture that never committed, i.e. against an
uncommitted WAL; their absolute numbers are not comparable with the tables above.

The two two-pass variants differ only in the order in which the parents are walked, and that
alone made the 3x difference at 200K folders. The unsorted `DISTINCT` emits the parents in
index-scan (timestamp) order, so every up-walk hits the primary-key index at random. The
adopted SQL takes its parents from `candidates`, which leaves `GROUP BY ph, gid` in
parent-handle order, so the walks are likely just as local. This is inferred from the
comparison and was not measured separately. Keep the walks in parent order when changing
this query.

## Reproducible run (this suite)

Machine: 13th Gen Intel(R) Core(TM) i9-13900H, otherwise idle (load average ~1.2 at the
start of the "after" run; the "before" run started at load 7.5 right after an unrelated
integration-test build finished, with nothing else running once it settled). Date: 2026-09-28.

- Before: the SDK-6582 commit's parent, with only `tests/unit/SqliteDateSectionsPerf_test.cpp`
  checked out from the SDK-6582 commit (the suite needs no other change to build on the old
  SQL):

  ```bash
  git checkout <sdk-6582>^
  git checkout <sdk-6582> -- tests/unit/SqliteDateSectionsPerf_test.cpp
  ```
- After: the SDK-6582 commit.

`checksum mismatches: none` — all 48 Month-granularity cells produced identical sections
(group id, start, end, count) before and after. (Their log lines predate the `gran=` field.)
Every dense cell (all shapes except `1M-200Kfolders-video0.1pct`) is at least 3x faster by
median, minimum observed ~7.5x; no cell, including the sparse shape, is slower.

| Cell | Before first / median | After first / median | Sections | Matched |
|---|---|---|---|---|
| shape=1M-5Kfolders, mime=VIDEO, order=MTIME_DESC, scope=root | 11.83 / 10.97 s | 0.21 / 0.15 s | 121 | 100000 |
| shape=1M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=root | 59.30 / 29.78 s | 0.39 / 0.39 s | 121 | 600000 |
| shape=1M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=sub-folder | 28.34 / 27.21 s | 0.38 / 0.38 s | 7 | 120 |
| shape=1M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=exclude+hide-sensitive | 26.09 / 25.98 s | 0.40 / 0.52 s | 121 | 582120 |
| shape=1M-5Kfolders, mime=VIDEO, order=MEDIATS_DESC, scope=root | 9.04 / 9.07 s | 0.18 / 0.18 s | 121 | 100000 |
| shape=1M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root | 25.69 / 24.97 s | 0.50 / 0.49 s | 121 | 600000 |
| shape=1M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=sub-folder | 24.00 / 23.59 s | 0.48 / 0.48 s | 7 | 120 |
| shape=1M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=exclude+hide-sensitive | 22.95 / 23.65 s | 0.51 / 0.51 s | 121 | 582120 |
| shape=1M-200Kfolders, mime=VIDEO, order=MTIME_DESC, scope=root | 13.01 / 13.18 s | 0.96 / 1.01 s | 121 | 100000 |
| shape=1M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=root | 35.04 / 38.13 s | 2.48 / 2.39 s | 121 | 600000 |
| shape=1M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=sub-folder | 34.48 / 38.97 s | 2.29 / 2.22 s | 1 | 5 |
| shape=1M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=exclude+hide-sensitive | 35.82 / 35.88 s | 2.44 / 2.45 s | 121 | 587050 |
| shape=1M-200Kfolders, mime=VIDEO, order=MEDIATS_DESC, scope=root | 13.39 / 15.06 s | 1.00 / 0.99 s | 121 | 100000 |
| shape=1M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root | 39.36 / 38.73 s | 2.37 / 2.39 s | 121 | 600000 |
| shape=1M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=sub-folder | 37.12 / 37.51 s | 2.28 / 2.27 s | 1 | 5 |
| shape=1M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=exclude+hide-sensitive | 37.78 / 34.68 s | 2.46 / 2.45 s | 121 | 587050 |
| shape=2M-5Kfolders, mime=VIDEO, order=MTIME_DESC, scope=root | 20.38 / 20.46 s | 0.39 / 0.34 s | 121 | 200000 |
| shape=2M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=root | 50.43 / 50.51 s | 1.03 / 0.98 s | 121 | 1200000 |
| shape=2M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=sub-folder | 46.48 / 46.82 s | 0.82 / 0.82 s | 14 | 240 |
| shape=2M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=exclude+hide-sensitive | 47.54 / 50.06 s | 0.87 / 0.88 s | 121 | 1164240 |
| shape=2M-5Kfolders, mime=VIDEO, order=MEDIATS_DESC, scope=root | 20.18 / 20.18 s | 0.29 / 0.28 s | 121 | 200000 |
| shape=2M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root | 45.20 / 47.49 s | 0.87 / 0.96 s | 121 | 1200000 |
| shape=2M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=sub-folder | 45.55 / 45.56 s | 1.00 / 1.01 s | 14 | 240 |
| shape=2M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=exclude+hide-sensitive | 46.13 / 46.35 s | 1.06 / 1.06 s | 121 | 1164240 |
| shape=2M-200Kfolders, mime=VIDEO, order=MTIME_DESC, scope=root | 27.85 / 27.42 s | 2.69 / 2.50 s | 121 | 200000 |
| shape=2M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=root | 147.56 / 62.64 s | 3.17 / 3.17 s | 121 | 1200000 |
| shape=2M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=sub-folder | 59.25 / 57.89 s | 3.01 / 2.71 s | 1 | 6 |
| shape=2M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=exclude+hide-sensitive | 57.99 / 61.70 s | 2.73 / 2.72 s | 121 | 1182060 |
| shape=2M-200Kfolders, mime=VIDEO, order=MEDIATS_DESC, scope=root | 23.56 / 23.45 s | 2.09 / 2.12 s | 121 | 200000 |
| shape=2M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root | 60.69 / 61.45 s | 2.69 / 2.67 s | 121 | 1200000 |
| shape=2M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=sub-folder | 55.06 / 55.31 s | 2.59 / 2.59 s | 1 | 6 |
| shape=2M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=exclude+hide-sensitive | 56.66 / 55.84 s | 2.76 / 2.75 s | 121 | 1182060 |
| shape=2M-200Kfolders-depth8, mime=VIDEO, order=MTIME_DESC, scope=root | 22.39 / 24.04 s | 2.76 / 2.60 s | 121 | 200000 |
| shape=2M-200Kfolders-depth8, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=root | 65.12 / 63.66 s | 3.16 / 3.16 s | 121 | 1200000 |
| shape=2M-200Kfolders-depth8, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=sub-folder | 70.19 / 63.76 s | 3.11 / 3.08 s | 1 | 6 |
| shape=2M-200Kfolders-depth8, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=exclude+hide-sensitive | 65.53 / 65.63 s | 3.28 / 3.46 s | 121 | 1182060 |
| shape=2M-200Kfolders-depth8, mime=VIDEO, order=MEDIATS_DESC, scope=root | 21.31 / 22.90 s | 3.07 / 3.05 s | 121 | 200000 |
| shape=2M-200Kfolders-depth8, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root | 60.51 / 60.52 s | 3.74 / 3.70 s | 121 | 1200000 |
| shape=2M-200Kfolders-depth8, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=sub-folder | 61.56 / 56.35 s | 3.59 / 3.58 s | 1 | 6 |
| shape=2M-200Kfolders-depth8, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=exclude+hide-sensitive | 55.48 / 56.47 s | 3.85 / 3.88 s | 121 | 1182060 |
| shape=1M-200Kfolders-video0.1pct, mime=VIDEO, order=MTIME_DESC, scope=root | 0.15 / 0.10 s | 0.08 / 0.02 s | 121 | 997 |
| shape=1M-200Kfolders-video0.1pct, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=root | 0.17 / 0.17 s | 0.05 / 0.04 s | 121 | 997 |
| shape=1M-200Kfolders-video0.1pct, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=sub-folder | 0.17 / 0.17 s | 0.04 / 0.04 s | 1 | 1 |
| shape=1M-200Kfolders-video0.1pct, mime=ALL_VISUAL_MEDIA, order=MTIME_DESC, scope=exclude+hide-sensitive | 0.17 / 0.17 s | 0.04 / 0.04 s | 121 | 985 |
| shape=1M-200Kfolders-video0.1pct, mime=VIDEO, order=MEDIATS_DESC, scope=root | 0.10 / 0.10 s | 0.01 / 0.01 s | 121 | 997 |
| shape=1M-200Kfolders-video0.1pct, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root | 0.16 / 0.14 s | 0.04 / 0.04 s | 121 | 997 |
| shape=1M-200Kfolders-video0.1pct, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=sub-folder | 0.15 / 0.16 s | 0.04 / 0.04 s | 1 | 1 |
| shape=1M-200Kfolders-video0.1pct, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=exclude+hide-sensitive | 0.17 / 0.17 s | 0.04 / 0.04 s | 121 | 985 |

### Day granularity

The six Day cells (one per shape) were added later and measured on 2026-09-29, before and
after, with the same method: `checksum mismatches: none`, and no cell is slower.

| Cell | Before first / median | After first / median | Sections | Matched |
|---|---|---|---|---|
| shape=1M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root, gran=Day | 68.70 / 44.62 s | 0.96 / 1.11 s | 3654 | 600000 |
| shape=1M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root, gran=Day | 117.08 / 111.90 s | 2.61 / 2.75 s | 3654 | 600000 |
| shape=2M-5Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root, gran=Day | 54.00 / 53.77 s | 2.67 / 3.76 s | 3654 | 1200000 |
| shape=2M-200Kfolders, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root, gran=Day | 74.22 / 67.00 s | 4.14 / 3.44 s | 3654 | 1200000 |
| shape=2M-200Kfolders-depth8, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root, gran=Day | 72.36 / 78.30 s | 5.36 / 4.60 s | 3654 | 1200000 |
| shape=1M-200Kfolders-video0.1pct, mime=ALL_VISUAL_MEDIA, order=MEDIATS_DESC, scope=root, gran=Day | 0.24 / 0.18 s | 0.09 / 0.04 s | 959 | 997 |

The machine was not fully idle during the "after" run: load average ~2–4 from desktop
applications. A second "after" run moved most medians by 10–30%, and 2M-5Kfolders by ~2x:

| Shape | First "after" median | Second "after" median |
|---|---|---|
| 1M-5Kfolders | 1.11 s | 0.75 s |
| 1M-200Kfolders | 2.75 s | 3.05 s |
| 2M-5Kfolders | 3.76 s | 1.92 s |
| 2M-200Kfolders | 3.44 s | 4.00 s |
| 2M-200Kfolders-depth8 | 4.60 s | 4.43 s |
| 1M-200Kfolders-video0.1pct | 0.04 s | 0.05 s |

Day is slower than Month when a folder holds many files. `candidates` has one row per
(parent, bucket), and at Day most files get a bucket of their own. At 2M / 5K folders that is
an estimated 1.2M rows instead of ~0.5M (derived from the fixture parameters, not counted).
That shape costs ~2x Month (~2 s vs ~1 s). With 200K folders each folder holds only a few
files, so Day costs ~1.2–1.3x Month. Both two-pass variants above were tried against this and
rejected.

### Index migration

Measured on 2026-09-29 with the final code, as one run on each 5K-folder shape. The steps
were: build the dataset, swap the two scope indexes back to the narrow pre-change pair, then
time `createIndexes` plus the commit (drop narrow, build both scope indexes):

| Nodes | Migration on the first open after upgrading | `createIndexes` with nothing to migrate |
|---|---|---|
| 1M | 3.1 s | 0 ms |
| 2M | 6.4 s | 0 ms |

The re-fetchnodes cost and the SQLite heap peak are not re-measured by this suite. The "Costs
of the widened indexes" paragraph above still holds their prototype values.
