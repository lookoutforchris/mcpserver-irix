# AGENTS.md — IRIX MCP Server

This file governs AI coding agent behavior (Codex, Claude Code) working in this repository. Read it completely before taking any action.

---

## 1. What This Project Is

A native, local-first MCP server for vintage SGI IRIX systems, written in portable C. It allows modern AI coding agents to safely inspect, read, write, and run constrained commands on IRIX workstations running IRIX 5.3, 6.2, or 6.5.

The project is intentionally built to last: clean C, portable across three IRIX generations, no unnecessary runtime dependencies, proper IRIX packaging.

---

## 2. Non-Negotiable Constraints

These decisions are locked. Do not reopen them, work around them, or implement alternatives.

1. **Implementation language: ANSI C (C89) only.** No C++, no Python, no shell for core logic.
2. **No `//` comments anywhere.** Not valid in C89/K&R. Use `/* */` exclusively.
3. **No C99 features.** No VLAs, no designated initializers, no `_Bool`, no `<stdint.h>`, no `restrict`. Use `src/compat/compat.h` for integer type definitions.
4. **No MIPSpro-only pragmas** in code that must compile on the IRIX 5.3 ucode compiler.
5. **No public network listener.** The daemon binds only to `/var/run/mcpserverd.sock` (UNIX domain socket). No TCP.
6. **No OAuth, Auth0, or any remote authentication.** Transport security is SSH.
7. **No SGUG-RSE runtime dependencies.** SGUG-RSE may be used during development for tooling but must not appear in any linked binary or install dependency.
8. **No modification of the Galaxy MCP server** in `reference-local/existing_mcpserver/`. That directory is read-only reference material.
9. **Separate binaries per IRIX target.** Do not assume one binary runs on 5.3, 6.2, and 6.5. The portability matrix defines the build matrix.
10. **Static linking preferred** for IRIX 5.3 targets. Eliminates libc version dependency.

---

## 3. Architecture Decisions (Locked)

Refer to `docs/ARCHITECTURE.md` for full detail. Summary:

- **Three components**: `mcpserverd` (daemon), `mcpserver` (CLI), `mcpserver stdio` (bridge mode of CLI binary)
- **IPC**: UNIX domain socket at `/var/run/mcpserverd.sock`, SOCK_STREAM, newline-delimited JSON-RPC 2.0
- **Install paths**: `/usr/sbin/mcpserverd`, `/usr/bin/mcpserver`
- **Config paths**: `/etc/mcpserver/projects.json`, `/etc/mcpserver/boundaries.json`
- **Service management**: `/etc/init.d/mcpserverd`, chkconfig flag at `/var/config/mcpserver`
- **v1 connection model**: One bridge connection at a time (no multiplexing)

---

## 4. Coding Standards

### 4.1 Required

- ANSI C89 throughout. Every file must compile with `cc -ansi -fullwarn` on IRIX.
- All functions must have prototypes declared before use.
- All return values from system calls must be checked.
- All dynamic allocations must be freed; all file descriptors must be closed.
- `sigaction()` for signal handling, not `signal()`.
- `execv()` for subprocess execution, not `system()` or `popen()`.
- Path authorization via `policy_is_path_allowed()` before any filesystem operation.
- Use `src/compat/compat.h` integer types (`int32_t`, `uint32_t`, etc.) throughout.

### 4.2 Prohibited

- `//` single-line comments
- `system()`, `popen()`, `exec*` with shell interpretation
- `gets()`, `sprintf()` without length bounds
- `strcpy()`, `strcat()` without explicit length checks
- Assuming `sizeof(long) == 4` without `_MIPS_SZLONG` guard
- Calling `realpath(3)` or `fnmatch(3)` directly — always use the compat wrappers
- Allocating memory speculatively (IRIX virtual swap note)
- Writing to stdout from the daemon (reserved for the stdio bridge)

### 4.3 Style

- Small, single-purpose functions. No function longer than ~100 lines.
- Explicit error paths, not nested conditionals.
- Return codes for error signaling (not errno alone, not exceptions).
- One module per source file pair (`.c` / `.h`). No cross-module internal coupling.
- No comments explaining what the code does. Comments only for non-obvious WHY.

---

## 5. Portability Rules

These apply to all code in `src/core/`, `src/daemon/`, and `src/cli/`:

| Rule | Reason |
|---|---|
| Use `FNDELAY` for non-blocking sockets, not `O_NONBLOCK` | IRIX documentation specifies `FNDELAY` |
| Always `unlink()` socket path before `bind()` | IRIX socket files persist after crash |
| Use `strlen(path) + sizeof(addr.sun_family)` for `bind()` length | IRIX docs: null bytes not counted |
| Include `<sys/types.h>` before any socket headers | IRIX 5.3 requirement |
| Use `LOG_DAEMON` facility for all syslog calls | Correct facility for a system daemon |
| Handle `EWOULDBLOCK` as identical to `EAGAIN` | Same value on IRIX; old releases used EWOULDBLOCK |
| Keep socket path ≤ 104 bytes | Conservative `sun_path` limit across all targets |

Portability questions that cannot be answered from documentation go into `docs/OPEN_QUESTIONS.md`, not silently into the code.

---

## 6. Tool Contract

Refer to `docs/TOOL_CONTRACT.md` for the full v1 tool specification. Key rules:

- Tool names, parameter names, and return field names must match the contract exactly.
- `allowed: false` returns must never reveal denied path existence.
- All text content is capped at 20000 characters before returning.
- Search results are capped at 200 matches.
- `run_inspect_command` uses direct `execv()` with hardcoded binary paths — never `PATH` resolution.
- Write tools only operate on files whose extension or name is in the allowlist.

---

## 7. Security Rules

Refer to `docs/SECURITY_MODEL.md` for the full model. Key rules:

- **Every path operation goes through `policy_is_path_allowed()`** before the filesystem call. No exceptions.
- **Path canonicalization uses `compat_realpath()`** — never raw string prefix matching.
- **Command arguments are validated character-by-character** before execution. Any argument containing `| & ; > < \n \r \x00 $ \` ( )` is rejected.
- **Command binaries are resolved from a hardcoded table** — never from `PATH`.
- The hardcoded global deny patterns (`**/.env`, `**/*.secret`, etc.) cannot be removed by configuration.

---

## 8. Workflow

Before starting any implementation task:

1. Read `AGENTS.md` (this file), `docs/ARCHITECTURE.md`, and `docs/TOOL_CONTRACT.md`.
2. Confirm the task is within v1 scope. Anything in the "Out of Scope" section of TOOL_CONTRACT.md is not to be implemented.
3. Implement only the scoped task. Do not refactor surrounding code opportunistically.
4. After implementation, run or update tests in `tests/unit/` and `tests/protocol/`.
5. Summarize what changed.
6. Record any unresolved portability assumptions in `docs/OPEN_QUESTIONS.md`.

When uncertain about IRIX behavior, check `docs/PORTABILITY_MATRIX.md` first. If not answered there, add to `docs/OPEN_QUESTIONS.md` and flag for human review rather than guessing.

---

## 9. Reference Material

| Resource | Location | Purpose |
|---|---|---|
| Galaxy MCP server (reference only) | `reference-local/existing_mcpserver/` | Behavioral reference for tool semantics and operator UX |
| SGI documentation | `reference-local/documentation/` | Primary source for all IRIX technical decisions |
| Project plan | `docs/PROJECT_PLAN.md` | Goals, milestones, workstreams |
| Architecture | `docs/ARCHITECTURE.md` | Component design and IPC |
| Tool contract | `docs/TOOL_CONTRACT.md` | All v1 tool specifications |
| Config schema | `docs/CONFIG_SCHEMA.md` | projects.json and boundaries.json schemas |
| Security model | `docs/SECURITY_MODEL.md` | Path policy, write policy, command policy |
| Portability matrix | `docs/PORTABILITY_MATRIX.md` | Per-target compiler/ABI/syscall facts |
