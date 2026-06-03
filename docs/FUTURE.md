# Future Work and Design Notes

Design ideas considered, partly implemented, or deferred. Not a roadmap — a record of "if you ever want to do X, here's the path we considered" so the next contributor doesn't have to rediscover the design.

---

## Per-deployment command filtering

**Status:** Considered, partially implemented, code removed in 2026-05-30 cleanup.

### What it would do

Let an operator constrain the set of executable commands per deployment by editing `boundaries.json`, without recompiling the daemon. Useful for hardened or audit-only deployments that want fewer commands than the full compiled-in set.

Example: a `readonly` profile deployment for code reviewers could strip `kill`, `chkconfig`, `mount`, `umount`, `chmod`, etc. — keeping only inspection commands.

### What existed before removal

Until v0.3.1 the code carried a partial implementation of this idea:

- `mcpserver apply` wrote `shell_rules.allowed_commands` (a hardcoded list of 18 commands) into `boundaries.json`.
- `policy_load()` parsed the list into `policy->allowed_cmds[]` (max 32 entries).
- `policy_is_cmd_allowed()` checked whether a given command was in that list.
- **But `policy_is_cmd_allowed()` was never called.** The actual command authorization happened in `tools_exec.c` via `find_cmd()` consulting the hardcoded `CMD_TABLE`. The config-driven list was a no-op.

This was misleading — boundaries.json looked configurable when it was not — so the dead path was removed:
- `src/cli/mcpserver.c` no longer writes `shell_rules`
- `src/core/policy.c` no longer parses it
- `struct policy` no longer carries `allowed_cmds[]` / `cmd_count`
- `policy_is_cmd_allowed()` deleted
- `POLICY_CMDS_MAX` removed

The commit removing it: see `docs/CONFIG_SCHEMA.md` §3 note for the user-visible change.

### What restoring it would look like

If a future deployment scenario justifies the feature, the architecture is straightforward:

1. **Schema:** Re-add `shell_rules.allowed_commands` to `boundaries.json`, plus parallel `build_rules.allowed_commands` for `run_build_command` if needed. Decide whether absence of the field means "all CMD_TABLE entries allowed" (most useful default) or "no commands allowed" (more conservative).

2. **Source of truth for `mcpserver apply`:** Don't hardcode the list a second time. Either:
   - Export `CMD_TABLE` and `BUILD_TABLE` via a public symbol so `mcpserver apply` can iterate it, or
   - Have `tools_exec.c` and `tools_build.c` expose `cmd_table_names()` / `build_table_names()` accessor functions.
   
   The previous implementation's drift (mcpserver.c hardcoded 18 commands while CMD_TABLE had 37) was the bug that caused the whole feature to be silently broken — without a shared source of truth this will happen again.

3. **Enforcement point:** Call `policy_is_cmd_allowed()` from `tool_run_inspect_command()` *after* `find_cmd()` succeeds. Treat config as a runtime *subset* of the compile-time superset — config can only restrict, never extend. This is the security ratchet that makes the feature safe.

4. **`run_build_command` and `run_program`:** Apply the same pattern. `run_program` is trickier — it accepts arbitrary executables inside policy roots, not a fixed table, so the relevant config might be a deny-glob rather than an allow-list.

5. **Schema versioning:** If the change is backwards-incompatible, bump the boundaries.json `version` field and have `policy_load()` reject older versions explicitly.

### Trade-offs

- **Pro:** Real per-deployment control without recompiling. Useful for `readonly` profiles and audit deployments.
- **Con:** Two sources of truth (CMD_TABLE + config) inevitably drift unless the apply-writer uses CMD_TABLE as input. The previous implementation didn't, and it broke.
- **Con:** Admin confusion when adding an unknown command to config does nothing (because CMD_TABLE is still the ceiling). Needs a clear error message in `mcpserver validate`.

---

## Privilege drop / dedicated `mcpserver` user

**Status:** Not started. Architecture supports it.

The daemon currently runs as root. See `docs/SECURITY_MODEL.md` §7 for the trade-offs. Moving to a dedicated `mcpserver` user with minimal privileges is a natural next step.

Key changes that would be needed:
- Package install (`exitop` in the IDB) creates the `mcpserver` user
- Socket file mode/ownership changed from `0600 root:root` to something the new user can `bind()`
- Config file ownership: `mcpserver:sys` or similar
- Binaries remain root-owned but the daemon `setuid()`s to `mcpserver` after `bind()`
- Init script started as root, drops privileges before `exec`
- All places that currently assume root file access (writing pidfile, unlinking socket) get audited

The compile-time changes are minor; the packaging and init-script changes are the real work.

---

## NFS reliability on IRIX 5.3

**Status:** Workaround in place (`nfs-proxy.py`); upstream fix pending.

See `docs/iris-bugs/IRIS_NFS_PORT_REMAP_BUG.md` for the full bug report. When IRIS PR addressing this lands, the proxy becomes unnecessary and `docs/IRIS_EMULATOR_SETUP.md` §10 can be simplified.

---

## iris-ci on Windows

**Status:** Blocked by upstream (`std::os::unix::net::UnixStream` in `src/iris_ci_main.rs`).

If iris-ci becomes Windows-buildable, we could replace `tcp-bridge.ps1` with iris-ci's cleaner host-side automation, and add scripted IRIX boot/login/shutdown to the `release` skill workflow. Not blocking — current workflow works — but would be a nice tidy.
