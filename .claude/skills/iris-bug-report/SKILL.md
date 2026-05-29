---
name: iris-bug-report
description: Author a structured upstream bug report for the IRIS emulator (techomancer/iris) or other IRIX-adjacent tooling, formatted to match the project's existing bug reports in docs/iris-bugs/. Use when investigating a newly-discovered emulator or toolchain bug, when the user says "write a bug report" or "file this with IRIS". Takes a one-line description of the bug as argument.
---

# IRIS Upstream Bug Report

The IRIS emulator is third-party software (https://github.com/techomancer/iris). We have filed three detailed bug reports already, all in `docs/iris-bugs/`. They follow a consistent structure that is both useful to the maintainer and serves as a record of investigation for the project. New reports should match this structure.

## When to use this skill

A bug report should be written when:
- A reproducible defect in IRIS, unfsd, or related host-side tooling is identified
- The bug blocks or workarounds a documented project workflow
- The investigation is complete enough that an outsider could reproduce it

It should NOT be used for:
- Bugs in this project's own code (write a regular commit + test instead)
- Vague guesses ("maybe IRIS is broken because X feels slow")
- Issues that are workflow problems on our side

## Existing reports as templates

Before writing, read at least one of:
- `docs/iris-bugs/IRIS_TCP_PORT_FORWARD_BUG.md` — empirical bug, evidence from monitor counters
- `docs/iris-bugs/IRIS_NFS_PORT_REMAP_BUG.md` — root-caused bug with proposed code fix
- `docs/iris-bugs/IRIS_MONITOR_NEWLINE_BUG.md` — minor bug with packet-capture evidence

Match the structure of whichever is closest to the new bug.

## Required sections

1. **Title** — `# Bug Report: <short imperative description>`

2. **Metadata block** — Repository, Component, File (if known), IRIS version, Host OS, Guest OS

3. **Summary** — one paragraph: what's broken, in what direction, what's the symptom

4. **Configuration** — relevant `iris53.toml` excerpt, guest networking facts, exact commands run

5. **Reproduction Steps** — numbered, deterministic, no inferred state

6. **Evidence** — the actual proof. Pick the strongest available:
   - IRIS monitor output (`telnet localhost 8888`, `net status tcp`, `log net on`)
   - Packet captures with byte counts
   - syslog entries from the IRIX guest
   - Direct RPC test results (for NFS-style bugs)
   - Console screenshots only as last resort

7. **Analysis or Root Cause** — explain why this happens. If you've found it in the Rust source, cite the file and line and quote the relevant code. If it's empirical without code access, explain the data flow and where it breaks.

8. **Impact** — who/what is affected, severity, how it manifests to a user

9. **Workaround** — if any exists from the user side (often: "none" for protocol-level bugs, or a partial mitigation like `nfs-proxy.py`)

10. **Proposed Fix** (when possible) — concrete code change with before/after, or a behavioral specification of what the fix should accomplish

## Filename convention

`docs/iris-bugs/IRIS_<SHORT_NAME>_BUG.md` — e.g., `IRIS_TCP_PORT_FORWARD_BUG.md`. Uppercase, underscore-separated, `_BUG.md` suffix.

## Style rules

- **Quote, don't paraphrase, evidence.** A packet trace, monitor output, or code snippet is worth more than a description of it.
- **Be specific about IRIS version.** Note the build date or commit if possible. Bugs may be fixed by the time someone reads the report.
- **Don't speculate beyond evidence.** If you don't know the root cause, say "the failure point is observed at X; the cause beyond that has not been determined."
- **Use tables for multi-field data** (monitor slot output, port mappings, version comparisons).
- **Keep "What works" vs "What fails" sections** — this is the most useful framing for an upstream maintainer to localize the bug.

## After writing

- Add an entry to `MEMORY.md` if there's a generalizable lesson (e.g., "IRIS guest→host TCP is broken — use serial console workaround")
- Update memory file `project_iris_bug_reports.md` to include the new report
- Optionally suggest the user file it as a GitHub issue at https://github.com/techomancer/iris
- Mention the bug + workaround in the relevant operational doc (usually `docs/IRIS_EMULATOR_SETUP.md` "Known Bugs" section)
