# IRIX MCP Server — Architecture

## 1. Overview

The IRIX MCP server is a **local-first, stdio-bridged MCP server for vintage SGI IRIX systems**. It gives modern AI coding agents (Claude Code, Codex) safe, bounded access to an IRIX workspace — reading files, writing within policy, searching, and running constrained inspection commands.

The design is intentionally IRIX-native: no public HTTP listener, no OAuth, no container runtime. Transport is a UNIX-domain socket on IRIX. Remote clients reach it through SSH and a stdio bridge.

---

## 2. Components

### 2.1 `mcpserverd` — The Daemon

A long-running IRIX process that:

- Loads and validates the active boundary policy from `/etc/mcpserver/boundaries.json` at startup.
- Binds a UNIX-domain socket at `/var/run/mcpserverd.sock`.
- Accepts connections from bridge clients via `accept()`.
- **Forks a child process for each connection** so multiple Claude Code / Codex sessions connect simultaneously without blocking each other.
- Handles MCP tool requests dispatched from connected bridge processes.
- Writes operational logs via `syslog(3)`.
- Responds to `SIGTERM` for clean shutdown, `SIGHUP` to reload policy.
- Writes its PID to `/var/run/mcpserverd.pid` at startup.

### 2.2 `mcpserver` — The Operator CLI

A single binary with subcommands covering the full admin lifecycle:

```
mcpserver help
mcpserver version
mcpserver status
mcpserver start
mcpserver stop
mcpserver restart
mcpserver enable
mcpserver disable
mcpserver logs [N]
mcpserver add <name> <root> --rw|--ro [--deny <pattern>...]
mcpserver remove <name>
mcpserver validate
mcpserver show
mcpserver preview
mcpserver apply
mcpserver stdio
```

`start`, `stop`, `restart` operate the daemon process.
`enable` / `disable` toggle the chkconfig flag and optionally start/stop the daemon.
`validate` / `show` / `preview` / `apply` manage the config/policy lifecycle.
`mcpserver stdio` starts the MCP stdio bridge (see §2.3).

### 2.3 `mcpserver stdio` — The Stdio Bridge

A mode of the `mcpserver` binary that:

- Speaks the MCP protocol (JSON-RPC 2.0, newline-delimited) over **stdin/stdout**.
- Connects to `mcpserverd` via the UNIX-domain socket at `/var/run/mcpserverd.sock`.
- Proxies MCP requests from the AI client to the daemon and returns responses.
- Writes diagnostics to **stderr** only (never stdout, which is reserved for MCP traffic).
- Exits cleanly when the AI client closes its stdin.

This mode is what makes the daemon usable by Codex and Claude Code without a public network service.

---

## 3. IPC Design

### 3.1 Socket

| Property | Value |
|---|---|
| Path | `/var/run/mcpserverd.sock` |
| Type | `AF_UNIX`, `SOCK_STREAM` |
| Framing | Newline-delimited JSON (one complete JSON object per line) |
| Protocol | MCP JSON-RPC 2.0 (same as stdio MCP transport) |

The daemon **unlinks the socket path before `bind()`** at startup to clear any stale socket from a previous crash. The socket file is created with mode 0600 (root-only access).

### 3.2 Connection lifecycle

```
bridge connects → daemon accept() → bridge sends request (JSON line)
                                  → daemon dispatches tool
                                  → daemon writes response (JSON line)
                                  → bridge forwards to AI client stdout
bridge stdin closed → bridge sends no more requests → bridge closes socket
```

The daemon forks a child process for each accepted connection. The parent immediately loops back to `accept()`. Each child serves one bridge session independently. The `SIGCHLD` handler in the parent reaps finished children. This allows multiple Claude Code conversations to use the MCP server simultaneously.

---

## 4. Remote Access Pattern

From a Windows workstation, Codex or Claude Code invokes mcpserver via SSH:

```
MCP host (Windows)
  ↓ stdio
local command: ssh irix-host mcpserver stdio
  ↓ SSH tunnel (stdin/stdout)
mcpserver stdio (on IRIX)
  ↓ UNIX domain socket
mcpserverd (on IRIX)
```

From the AI client's perspective this is a standard stdio MCP server. The SSH transport is entirely transparent.

---

## 5. Install Paths

| File | Path | Mode |
|---|---|---|
| Daemon binary | `/usr/sbin/mcpserverd` | 755 root:sys |
| CLI binary | `/usr/bin/mcpserver` | 755 root:sys |
| Init script | `/etc/init.d/mcpserverd` | 755 root:sys |
| Config directory | `/etc/mcpserver/` | 755 root:sys |
| projects.json | `/etc/mcpserver/projects.json` | 644 root:sys |
| boundaries.json | `/etc/mcpserver/boundaries.json` | 644 root:sys |
| Backup directory | `/etc/mcpserver/backup/` | 755 root:sys |
| PID file | `/var/run/mcpserverd.pid` | created at runtime |
| Socket | `/var/run/mcpserverd.sock` | created at runtime |
| chkconfig flag | `/var/config/mcpserver` | created by exitop |

---

## 6. Service Integration

### 6.1 chkconfig

The chkconfig flag file lives at `/var/config/mcpserver`. It contains the string `"on"` when the service is enabled at boot, and `"off"` (or is absent) when disabled.

The tardist post-install exitop creates it disabled by default:
```sh
/sbin/chkconfig -f mcpserver off
```

The operator enables it with:
```sh
mcpserver enable
# which runs: /sbin/chkconfig -f mcpserver on
```

### 6.2 Init script

`/etc/init.d/mcpserverd` follows the standard IRIX pattern:

```sh
#!/sbin/sh
if /sbin/chkconfig mcpserver; then
    case "$1" in
    start)
        echo "Starting mcpserverd"
        /usr/sbin/mcpserverd &
        ;;
    stop)
        echo "Stopping mcpserverd"
        if [ -f /var/run/mcpserverd.pid ]; then
            kill -TERM $(cat /var/run/mcpserverd.pid)
        fi
        ;;
    esac
fi
```

Symlinks in rc directories:
- `/etc/rc2.d/S75mcpserverd` → `../init.d/mcpserverd` (start at run level 2)
- `/etc/rc0.d/K15mcpserverd` → `../init.d/mcpserverd` (kill at shutdown)

---

## 7. Daemon Startup Sequence

1. Open syslog: `openlog("mcpserverd", LOG_PID | LOG_CONS, LOG_DAEMON)`
2. Load and validate `/etc/mcpserver/boundaries.json`
3. Unlink `/var/run/mcpserverd.sock` (clear stale socket if present)
4. Create UNIX domain socket, bind, listen
5. Write PID to `/var/run/mcpserverd.pid`
6. Install signal handlers: `SIGTERM` → shutdown, `SIGHUP` → reload policy, `SIGCHLD` → reap (future)
7. Enter accept loop
8. On each connection: read JSON-RPC line, dispatch tool, write response
9. On `SIGTERM`: close socket, unlink socket file, remove PID file, exit

---

## 8. Source Layout

```
src/
  core/
    json.c / json.h          — JSON-RPC message parsing and serialization
    protocol.c / protocol.h  — MCP initialize, tools/list, tools/call dispatch
    policy.c / policy.h      — boundary policy loader and enforcement
    tools_fs.c / tools_fs.h  — filesystem read/inspect tools
    tools_text.c / tools_text.h — text search and pattern tools
    tools_write.c / tools_write.h — write/path tools
    tools_exec.c / tools_exec.h — run_inspect_command
    result.h                 — shared result struct definitions

  daemon/
    mcpserverd.c             — daemon main, socket lifecycle, accept loop
    ipc.c / ipc.h            — UNIX domain socket abstraction

  cli/
    mcpserver.c              — CLI main and subcommand dispatch
    stdio_bridge.c / stdio_bridge.h — stdio↔socket proxy

  compat/
    compat.h                 — type definitions (int32_t etc.), feature detection, version string
    realpath.c               — portable realpath() for IRIX 5.3
    fnmatch.c                — portable fnmatch() for IRIX 5.3
    snprintf.c               — portable snprintf() for IRIX 5.3 (absent from libc)

tests/
  unit/
  protocol/
  fixtures/

packaging/
  irix53/
  irix62/
  irix65/
```

---

## 9. Key Design Constraints

- **No public network service.** The daemon does not listen on any TCP port.
- **No external runtime dependencies.** Statically linked where practical. SGUG-RSE may be used during development but must not be a runtime dependency.
- **Multiple concurrent bridge connections supported.** The daemon forks a child process per accepted connection; multiple Claude Code / Codex sessions can connect simultaneously.
- **Portable C only.** No C++, no Python, no shell for core logic.
- **ANSI C89 throughout.** Required for IRIX 5.3 ucode compiler compatibility.
- **Separate binaries per IRIX target.** No single binary is expected to run across all three targets.
