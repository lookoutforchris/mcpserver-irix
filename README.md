# mcpserver-irix

A native, local-first MCP (Model Context Protocol) server for vintage SGI IRIX systems.

Gives modern AI coding agents (Claude Code, Codex) safe, bounded access to an IRIX workspace — reading files, writing within policy, searching source trees, and running constrained inspection commands — directly on real or emulated IRIX hardware.

**Status:** Early development. Specification complete; implementation in progress.

---

## Target Platforms

| Platform | ABI | Compiler |
|---|---|---|
| IRIX 5.3 | O32 / MIPS2 | IDO ucode `cc` |
| IRIX 6.2 | O32 or N32 | MIPSpro |
| IRIX 6.5 | N32 / MIPS3 | MIPSpro 7.4 |

Primary development hardware: SGI Octane2 running IRIX 6.5.30.
IRIX 5.3 testing: IRIS emulator (SGI Indy / R4400).

---

## Architecture

Three components:

- **`mcpserverd`** — long-running daemon on IRIX, listens on a UNIX-domain socket
- **`mcpserver`** — operator CLI (`start`, `stop`, `add`, `remove`, `validate`, `apply`, ...)
- **`mcpserver stdio`** — stdio MCP bridge; AI clients connect through SSH:

```
Claude Code / Codex (Windows)
  └─ ssh irix-host mcpserver stdio
       └─ UNIX socket → mcpserverd
```

No public HTTP listener. No OAuth. Transport security is SSH.

---

## Documentation

| Document | Purpose |
|---|---|
| [`docs/PROJECT_PLAN.md`](docs/PROJECT_PLAN.md) | Goals, milestones, workstreams |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Component design and IPC |
| [`docs/TOOL_CONTRACT.md`](docs/TOOL_CONTRACT.md) | All v1 MCP tool specifications |
| [`docs/CONFIG_SCHEMA.md`](docs/CONFIG_SCHEMA.md) | `projects.json` and `boundaries.json` schemas |
| [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) | Path policy, write policy, command policy |
| [`docs/PORTABILITY_MATRIX.md`](docs/PORTABILITY_MATRIX.md) | Per-target compiler/ABI/syscall facts |
| [`AGENTS.md`](AGENTS.md) | Rules for AI coding agents working in this repo |

---

## Building

> Build infrastructure is being developed. The following is the intended workflow.

Code is written on a Windows workstation and built on the target IRIX system:

```sh
# On the Octane2 (IRIX 6.5, N32)
git pull
make

# IRIX 5.3-compatible binary (O32, static)
make TARGET=irix53
```

SGUG-RSE on IRIX 6.5 provides `git`, `make`, and `gcc` for development tooling.
The final distribution uses the MIPSpro compiler; SGUG-RSE is a development-only dependency.

---

## Installation

Packages will be distributed as native IRIX tardist archives installable through `inst` or Software Manager:

```sh
inst -a mcpserver.irix65.tardist
```

Post-install, enable the daemon with:

```sh
mcpserver enable
mcpserver start
```

---

## License

TBD — intended to be open source. See project plan for community goals.
