# IRIX MCP Server — Security Model

## 1. Design Philosophy

Security boundaries are the most important design concern in this project. The daemon has write access and command execution capability on a real IRIX system. Every tool call passes through explicit policy enforcement before any filesystem or process operation occurs.

The model is **allowlist-only**: everything is denied by default, and access is granted only through explicit configuration. There are no implicit grants.

---

## 2. Runtime Security Posture

| Layer | Mechanism |
|---|---|
| Transport | UNIX-domain socket at `/var/run/mcpserverd.sock`, mode 0600 |
| Network exposure | None. Daemon does not listen on any TCP port. |
| Remote access | SSH access control (standard IRIX user auth) |
| Filesystem access | Explicit path boundary policy (`boundaries.json`) |
| Write access | Extension/name allowlist within read-write roots only |
| Command execution | Explicit allowed-command table with per-command argument validation |
| Process privilege | Runs as root (known limitation; see §7) |

---

## 3. Path Authorization

### 3.1 Authorization check sequence

Every path-taking tool call runs this check before any filesystem operation:

```
1. Canonicalize the path (resolve . and .. components; see §3.2)
2. Check against deny_overrides (absolute paths and glob patterns) — DENY if matched
3. For reads: check against read_rules.deny_read_globs — DENY if matched
4. For writes: check against write_rules.deny_write_globs — DENY if matched
5. Check if path is under any read_write_root or read_only_root — ALLOW if matched
6. Default: DENY
```

Denial is silent: the tool returns `allowed: false` with no further information. Denied paths do not reveal whether the underlying file exists.

### 3.2 Path canonicalization

The path canonicalization step is critical for preventing traversal attacks. It must:

- Reject paths that traverse outside any allowed root after canonicalization.
- Resolve all `.` and `..` components before root membership is checked.
- Resolve symlinks that could escape the root.

On IRIX 5.3, `realpath(3)` is not available. The implementation in `src/compat/realpath.c` provides equivalent behavior using `stat()` and `readlink()` with explicit `..` tracking. **All path authorization must use the compat realpath, never raw string prefix matching.**

### 3.3 Symlink policy

Symlinks are followed during canonicalization. If a symlink's target resolves to a path outside all allowed roots, the path check fails with `allowed: false`. The daemon does not follow symlinks that escape roots.

### 3.4 Race condition awareness (TOCTOU)

There is an inherent time-of-check-to-time-of-use gap between path authorization and the actual `open()` call. The implementation must:

- Minimize the window between check and use.
- Open files using `O_NOFOLLOW` where available and appropriate.
- Accept that TOCTOU cannot be fully eliminated without kernel-level mediation.

This risk is documented as a known limitation. The primary threat model is an authorized user misconfiguring the server, not an active attacker with local access.

---

## 4. Command Execution Policy

Three tools execute external processes: `run_inspect_command`, `run_build_command`, and `run_program`. All three use `execv()` directly — no shell interpretation — and validate arguments before execution. They differ in argument-validation strictness, output limits, timeout, and which subset of programs they can invoke.

### 4.1 Tool comparison

| Tool | Program source | Path-validates arguments? | Output buffer | Timeout |
|---|---|---|---|---|
| `run_inspect_command` | Hardcoded `CMD_TABLE` (~37 commands; `src/core/tools_exec.c`) | Yes, per-command rules | 20 KB | 30 s |
| `run_build_command` | Hardcoded `BUILD_TABLE` (~26 build/packaging tools; `src/core/tools_build.c`) | No — only `work_dir` is validated | 64 KB | 300 s |
| `run_program` | Any executable inside a policy root | Program path validated; arguments not path-validated | 64 KB | 300 s |

For the current command lists and per-command flag rules, see [`TOOL_CONTRACT.md`](TOOL_CONTRACT.md) §`run_inspect_command`, §`run_build_command`, and §`run_program`. The lists are not duplicated here — that doc is the single source of truth.

### 4.2 Argument validation rules

These apply universally before any command is executed by any of the three tools:

1. The `command`/`program` must resolve via the appropriate hardcoded table (`CMD_TABLE`, `BUILD_TABLE`) or be an absolute path inside a policy root (`run_program`). No `PATH` resolution.
2. No argument may contain any of these characters: `` | & ; > < \n \r \x00 $ ` ( ) ``
3. **For `run_inspect_command`** only: path arguments are checked against policy roots, flag arguments must match an allowed-flag list per command, and numeric arguments must be reasonable positive integers.
4. **For `run_build_command` and `run_program`**: arguments are passed verbatim except for the shell-metacharacter check. Compilers and `make` legitimately reference paths outside policy roots (`/usr/include`, `/opt/MIPSpro/lib`, etc.), so per-argument path validation would block normal builds. The `work_dir` parameter IS validated.
5. Output is clipped at the per-tool buffer size. Execution continues but the response carries `truncated: true`.
6. A timeout kills the child process if it exceeds the per-tool limit; `error` is set to indicate timeout.

### 4.3 Execution model

```c
/* Pseudocode — no shell, no glob expansion */
execv("/bin/ls", args);   /* direct exec, not system() or popen() */
```

The command binary path is resolved from a hardcoded table (e.g., `ls` → `/bin/ls`), not from `PATH`. This prevents PATH-injection attacks. `run_program` accepts only absolute paths under a policy root, with an explicit `stat()` + executable-bit check before fork/exec.

### 4.4 Threat model boundaries

The relaxed argument validation in `run_build_command` and `run_program` is a deliberate trust trade-off:

- The set of *callable* programs is still bounded (hardcoded tables, or executables physically present in a policy root).
- Shell metacharacters are still rejected, so an argument cannot inject a pipeline or redirection.
- A compromised compiler invocation could write arbitrary files outside policy roots (compilers honor `-o /any/path`). This is accepted: the operator is expected to trust the AI client to the same extent they would trust any developer with shell access via SSH.

---

## 5. Write Policy

### 5.1 Extension and name allowlist

Write operations (`create_text_file`, `replace_text_file`, `delete_text_file`) are only permitted on files whose name matches the allowlist.

**Default allowlist:**

Extensions: `.md`, `.txt`, `.json`, `.c`, `.h`, `.cc`, `.cpp`, `.s`, `.S`, `.sh`, `.mk`

Exact names (no extension required): `Makefile`, `Imakefile`, `GNUmakefile`

Rationale: This is appropriate for a coding-oriented product targeting IRIX C development. The allowlist explicitly excludes: binary files, compiled objects (`.o`, `.a`, `.so`), executables (no extension), configuration files with sensitive conventions (`.conf` in system dirs), and any file type not needed for source code development.

### 5.2 Write root requirement

A write operation is allowed only if all of these hold:

1. The path is under a `read_write_root` (not merely a `read_only_root`).
2. The path passes the deny check (deny_overrides and deny_write_globs).
3. The file extension or name is in the write allowlist.

### 5.3 Hardcoded global deny patterns

These are enforced regardless of project configuration:

```
**/.env
**/.env.*
**/*.secret
**/*.key
**/*.pem
**/*.crt
**/*_rsa
**/*_dsa
```

These cannot be removed by operator configuration. A future config option to add additional global denies may be added, but global denies cannot be weakened.

---

## 6. Socket File Security

The UNIX-domain socket at `/var/run/mcpserverd.sock` is created with mode **0600** (root read/write only). This means:

- Only root or processes running as root can connect to the socket.
- The `mcpserver stdio` bridge must be run with root privileges to connect.
- On IRIX, SSH sessions that `ssh root@irix-host mcpserver stdio` satisfy this requirement.

The socket file is cleaned up (unlinked) at daemon startup (before `bind()`) and at clean shutdown. A crashed daemon leaves a stale socket file; the next startup clears it automatically.

---

## 7. Privilege Model (Current Limitation)

The daemon runs as root. This is a deliberate design simplification:

**Why root:** Avoids the complexity of creating a dedicated `mcpserver` system user during package install, handling the `chown` of config files and socket, and managing privilege drops. The boundary policy is the primary protection layer.

**Known risk:** A bug in the boundary engine or command execution could allow an authorized user to access files they should not, with root-level read/write capability.

**Mitigation:** The boundary engine is the most carefully tested component. All path authorization uses the compat `realpath` implementation, not string prefix matching. The command execution model uses direct `execv()`, not shell interpretation.

**Future work:** A dedicated `mcpserver` user with minimal privileges is a candidate for a future release. The architecture supports it — socket mode, config file ownership, and binary paths can all be adjusted. See `docs/FUTURE.md`.

---

## 8. Logging and Audit

All tool calls are logged via `syslog(LOG_DAEMON)` with at minimum:

- Tool name
- Requesting path(s)
- Authorization result (allowed/denied)
- Outcome (ok / error)
- Elapsed time in milliseconds

Denied access attempts are logged at `LOG_WARNING`. Errors are logged at `LOG_ERR`. Normal operations at `LOG_INFO`. Debug detail at `LOG_DEBUG` (off by default).

Log entries do not include file content, command output, or MCP response bodies.
