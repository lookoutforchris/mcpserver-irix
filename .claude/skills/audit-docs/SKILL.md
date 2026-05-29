---
name: audit-docs
description: Systematically audit all markdown documentation in the repo for contradictions, stale paths, outdated version strings, broken cross-references, section-numbering bugs, and overlap between docs. Reports findings and proposes consolidation BEFORE making any changes. Use when the user says "audit docs", "review documentation", or after major workflow changes that may have invalidated written procedure.
---

# Documentation Audit

A repo with many docs accumulates contradictions silently. This skill systematically checks for them.

## Scope

By default, audit every `.md` and `.txt` file in:
- Repo root (`README.md`, `AGENTS.md`, `CLAUDE.md` if present)
- `docs/` and all its subdirectories
- `tests/` (occasional `.md` memos)

Exclude `node_modules/`, `release-assets/`, `reference-local/` (third-party), `docs/archive/` (intentionally frozen).

## Audit checklist

For each document, check:

### 1. Version-string consistency

Find every literal version like `v0.3.0`, `0.3.1`. Cross-check against:
- `src/compat/compat.h` `MCPSERVER_VERSION`
- `Makefile` `PKG_VERSION`
- The three `packaging/*/mcpserver.spec` `id` strings
- README status line

Any version that doesn't match the current authoritative version is stale.

### 2. Path correctness

Find every absolute path mentioned (e.g., `/home/work/projects/...`, `C:\dev\tools\...`, `/usr/people/shared/...`). Verify:
- Does the path exist (or is it documented as expected-to-exist)?
- Has it moved? (We renamed Octane2 project root from `/home/chris/src/` to `/home/work/projects/` mid-project)

### 3. Internal section numbering

For docs with numbered sections (`## 1.`, `## 2.`, etc.), check the sequence is contiguous and unique. Duplicate numbers (e.g., two `## 12`) are bugs.

### 4. Cross-references between docs

When doc A references doc B, verify doc B still exists at that path. Some docs were moved (`docs/iris-bugs/`, `docs/archive/`) and old references may not have been updated.

### 5. Status statements that may have changed

Look for phrases like "NOT FUNCTIONAL", "broken", "pending", "TODO", "not yet". Compare against current reality. The transport story is the biggest historical landmine here — IRIS_EMULATOR_SETUP.md previously said the MCP transport was non-functional when in fact `tcp-bridge.ps1` makes it work fine.

### 6. References to files that don't exist

ARCHITECTURE.md once listed `irix53.c`, `irix62.c`, `irix65.c` in `src/compat/`. These files were never written — the compat layer is all in `compat.h`. Verify every source file mentioned actually exists.

### 7. Overlap and redundancy

Identify content that appears in multiple docs. Some duplication is acceptable (a quick-reference and a deep-dive both mentioning the same fact), but identical procedure documented in two places will drift.

### 8. Stale operational claims

"This script is run during deployment" — is it? "We use TFTP for transfers" — do we still? Verify behavioral claims against the current memory state and the actual workflow.

### 9. Documents that describe a defunct phase of the project

`PROJECT_PLAN.md` was the pre-build planning document. Once a project ships, planning docs become misleading to first-time readers. Watch for this pattern in any forward-looking doc.

## Output format

Produce a report with two clearly separated sections:

### "Errors and corrections (will fix without further approval)"
- Concrete factual errors, stale versions, broken cross-references, section numbering, etc.
- Each item: which file, what's wrong, what the fix is.

### "Consolidation proposals (need approval)"
- Bigger structural changes — moving, deleting, merging files.
- For each: what to do, why, what's lost vs gained.
- Mark each MINOR / MODERATE / MAJOR by impact.

## After approval

Apply all approved changes. Use `git mv` for renames (preserves history), `git rm` for deletions. Then a single commit with a clear message listing what changed.

If you renumber sections, update internal cross-references (`see §12`) in the same edit.

## Things NOT to audit

- Code comments — that's a separate concern
- Documentation in `reference-local/` — third-party, not ours to fix
- License headers, contributing guides — outside scope unless the user names them
- Style/tone — only factual correctness and structural integrity
