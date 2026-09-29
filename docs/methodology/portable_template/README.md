# Portable methodology template

A starter scaffold for adopting the SDK-5360 followup methodology in
another repository (SDK or non-SDK).

## What you get

| File | Purpose |
|---|---|
| `CLAUDE.template.md` | Minimal `CLAUDE.md` scaffold (project overview / build / test / style / git workflow). |
| `BENCHMARKS.template.md` | Minimal benchmark catalog scaffold. |
| `hard_rules_catalog.template.md` | HR-table scaffold with no project-specific rules; ready to grow as the team encounters its own incidents. |
| `Jenkinsfile_snippets.md` | The trigger-phrase parsing block and the `--bench` / `--tsan` stage shape from the SDK Linux Jenkinsfile, ready to copy. |

## Adoption recipe

From the SDK repo root:

```bash
TARGET_REPO=/path/to/your-other-repo

# Copy the portable template
mkdir -p "${TARGET_REPO}/docs/methodology"
cp -r docs/methodology/portable_template/. "${TARGET_REPO}/docs/methodology/"

# Optional: also copy the canonical methodology docs as starting reference
cp docs/methodology/{followup_workflow,memory_discipline,regression_sweep_cadence,benchmark_discipline,tsan_discipline,fu7_N_session_template,glossary}.md \
   "${TARGET_REPO}/docs/methodology/"
```

Then in the target repo:

1. **Rename the templates** (drop the `.template` suffix):
   ```bash
   cd "${TARGET_REPO}"
   mv docs/methodology/CLAUDE.template.md CLAUDE.md          # if no existing CLAUDE.md
   mv docs/methodology/BENCHMARKS.template.md tests/integration/BENCHMARKS.md  # or wherever
   mv docs/methodology/hard_rules_catalog.template.md docs/methodology/hard_rules_catalog.md
   ```

2. **Customise the per-project bits**:
   - In `CLAUDE.md`, replace `<project-name>` / `<build command>` / etc.
     with your project's specifics.
   - In `BENCHMARKS.md`, add your project's bench cells and known-bad
     list.
   - In `hard_rules_catalog.md`, the table starts empty — add rules as
     your team encounters incidents that motivate them.
   - In `Jenkinsfile_snippets.md`, copy the relevant trigger-parsing
     block into your project's Jenkinsfile and customise the stage
     names.

3. **Set the working pattern**:
   - Create an `~/investigationTests/<your-project>/` (or equivalent)
     directory for session reports.
   - Decide on the followup-naming convention (e.g. `followup<n>/`).
   - Wire up the auto-memory directory if your tooling supports it
     (Claude harnesses use `~/.claude/projects/<repo-slug>/memory/`).

## What this template does NOT include

- **Build system specifics**: The SDK uses CMake + VCPKG. Replace these
  with your project's actual build commands.
- **Test framework specifics**: GoogleTest. Replace with your framework
  (pytest, vitest, JUnit, etc.).
- **GPG signing protocol** (HR1): Carry over only if your team uses
  signed commits.
- **Specific HRs from SDK-5360**: The HR catalog template is empty;
  your project will accumulate its own rules.

## When this template is overkill

If your project does NOT have:

- Long-running feature branches (months of work)
- Multi-agent / multi-session AI-assisted development
- Performance-sensitive code paths needing benchmark regression gates
- Concurrency code needing TSAN-style discipline

...then a single `CONTRIBUTING.md` and per-PR review are likely
sufficient. The full methodology is justified by SDK-5360's specific
mix of (a) high-throughput threaded code, (b) production API
constraints, (c) multi-OS support, (d) months of iteration on a
feature branch before merge.

## Inspiration / further reading

- Toyota Production System: "Stop the line" → HR23 / HR40 spirit.
- "Beyond Software Architecture" (Hohmann): release engineering as
  an explicit discipline.
- The methodology directory in the SDK repo itself
  (`docs/methodology/`) is the live working version this template
  was derived from.
