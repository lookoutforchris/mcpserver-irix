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

---

## Remote transport for real IRIX machines

**Status:** Decided 2026-06-06. Option F (platform-native transport) + TCP Wrappers implemented. Option C documented as future upgrade path.

### The problem

The MCP connection model uses SSH as a transport wrapper for all real IRIX machines:

```json
"command": "ssh",
"args": ["-T", "-o", "BatchMode=yes", "root@192.168.1.50", "/usr/bin/mcpserver", "stdio"]
```

This works for IRIX 6.5 and 6.2 because SGUG-RSE ships OpenSSH for those platforms. It does **not** work for IRIX 5.3 — IRIX 5.3 predates OpenSSH and ships only with the BSD r-commands (rsh/rlogin) and telnetd. There is no sshd in the base OS.

Note: the IRIS emulator sidesteps this entirely because the IRIX 5.3 guest runs on the same Windows machine as Claude Code. The inetd+`tcp-bridge.ps1` workaround (loopback TCP, no auth) works because there is no real network boundary. A real remote IRIX 5.3 machine over a LAN would be a different situation.

### SSH packages that exist for IRIX 5.3

Researched 2026-06-06. Three candidate packages were investigated:

| Package | Source | IRIX 5.3 compatible? | Notes |
|---|---|---|---|
| openssh-3.7.1p2 | SGI Freeware CD3 | **No** | N32/MIPS3 binary; targets IRIX 6.5 only |
| openssh-6.2p1 | Nekoware | **No** | Targets IRIX 6.5.21+ only; N32/MIPS3 ABI |
| openssh-6.8p1 | tgcware | **Yes** | MIPS1/O32; built on real IRIX 5.3 Indy hardware |

**tgcware** (`jupiterrise.com/tgcware/`) is the only documented precompiled SSH for IRIX 5.3. Key facts:
- Maintained by Tom Christensen; built on an Indy R4600PC/133 running actual IRIX 5.3
- MIPS1/O32 ABI — correct for IRIX 5.3
- Installs to `/usr/tgcware/sbin/sshd`
- Supports Ed25519 host keys (6.8p1 > 6.5 threshold where ed25519 was added)
- **Requires `prngd`** as a dependency — IRIX 5.3 has no `/dev/urandom` or `/dev/random`; PRNGD provides the entropy source OpenSSH requires
- Also requires `openssl1` and `zlib` from tgcware
- Project discontinued May 2015; 6.8p1 is the final version
- Modern clients (OpenSSH 8.8+) disable `ssh-rsa` (SHA-1) by default; use Ed25519 host keys to avoid the compatibility issue

### Broader question: drop SSH for all IRIX versions?

Even if tgcware makes SSH possible on IRIX 5.3, there is a stronger argument for dropping SSH as a transport requirement across all three IRIX versions.

**The honest security picture for a home lab:**
- IRIX 6.5 (last patched ~2006), 6.2, and 5.3 all carry decades of unpatched CVEs at the OS level — NFS, rpcbind, sendmail, the kernel itself
- SSH on top of that is a locked front door on a house with no walls
- The MCP server's own policy engine (path restrictions, read/write limits, command whitelist) is where the real access control lives — independent of transport
- A home lab vintage computer on a private LAN does not require network-layer authentication to the same standard as a production system

**Benefits of dropping SSH:**
- Eliminates the SGUG-RSE dependency on IRIX 6.5/6.2
- Eliminates the tgcware dependency (and its `prngd`/`openssl1`/`zlib` chain) on IRIX 5.3
- Everything works with base IRIX — inetd has been in every IRIX release
- Consistent setup story across all three IRIX versions
- Matches what already works successfully in the IRIS emulator setup

### Transport options

**Option A — RSH**

Use `rsh` instead of `ssh`. IRIX 5.3 ships `rshd`; `.rhosts` provides IP-based trust. The `.mcp.json` pattern is identical to SSH:

```json
"command": "rsh",
"args": ["192.168.1.50", "/usr/bin/mcpserver", "stdio"]
```

- Linux/Mac: `rsh` client available natively or via one-line package install
- Windows: `rsh.exe` was removed from modern Windows; requires MSYS2 or similar
- No encryption; acceptable on an isolated lab LAN
- No new code or scripts required on any side

**Option B — Netcat**

Use `nc` to connect stdio to an inetd-managed port on IRIX:

```json
"command": "nc",
"args": ["192.168.1.50", "8753"]
```

- Linux/Mac: `nc` is built in on both platforms
- Windows: not built in; see sub-options below
- Different `nc` implementations behave differently on EOF/session close — needs validation for MCP session lifecycle
- inetd on IRIX handles the connection identically to the IRIS emulator case

*Getting nc/ncat on Windows — investigated options:*

| Source | Type | Dependencies | License | Notes |
|---|---|---|---|---|
| MSYS2 `pacman -S nmap` | MSYS2 binary | Requires MSYS2 runtime DLLs | GPLv2 | Not usable standalone; MSYS2 must be installed |
| Nmap standalone `ncat.exe` | Native Win32 | Ships with `libssl-*.dll` + `libcrypto-*.dll` alongside it | GPLv2 | Redistributable with GPL compliance (source link + license); 3-4 file bundle, not a single exe |
| Hobbit's netcat 1.11 | Native Win32 | None — single static `.exe` | Freely distributable | Ancient (2004); no TLS; plain TCP works; SmartScreen flags it; no longer maintained |
| WSL `nc` | Linux binary via WSL | WSL must be installed | N/A | Zero extra install if WSL already present; WSL is not installed by default and requires enabling a Windows feature |

None of these is a clean zero-install single-binary solution for a general Windows user.

**Option C — Compiled cross-platform bridge binary**

Write a small binary (`mcp-tcp-bridge`) in Go that connects its stdin/stdout to a TCP socket. Compile for Windows, Linux, and macOS. The `.mcp.json` pattern is identical on all platforms:

```json
"command": "mcp-tcp-bridge",
"args": ["192.168.1.50", "8753"]
```

Go produces a truly static Windows executable — no DLL dependencies beyond `kernel32.dll` and `ws2_32.dll` which every Windows install has. ~3MB binary. We own the code and can get the MCP session lifecycle (EOF handling, clean close) exactly right.

*Downsides and signing reality:*

- Adds a cross-platform build and release pipeline (cross-compile Windows/Linux/macOS binaries from CI)
- macOS Gatekeeper blocks unsigned binaries from the internet. Workaround: `xattr -d com.apple.quarantine mcp-tcp-bridge` once. Full fix: Apple Developer Program ($99/year) + notarize with `xcrun notarytool` — automatable in CI but non-trivial setup
- Windows SmartScreen warns on unsigned executables. Workaround: "More info → Run anyway" once. Full fix requires either an OV code-signing cert (~$200-400/year, SmartScreen still warns until binary builds download reputation over weeks) or an EV cert (~$400-700/year, bypasses SmartScreen immediately but requires a physical hardware security token — makes CI signing difficult)
- For this project's audience (vintage computing hobbyists comfortable with a terminal) the one-time click/command workarounds are probably acceptable; paying $300-800/year for signing is not justified

*Redundancy with Linux/Mac:* On Linux and macOS `nc` is already built in and does the same job. Option C adds meaningful value only for Windows users — the uniformity is aesthetic, not functional, for the other platforms.

**Option D — Python script**

Replace `tcp-bridge.ps1` with `tcp-bridge.py`. Python3 is available on Linux/Mac by default and on most developer Windows machines.

```json
"command": "python3",
"args": ["tcp-bridge.py", "192.168.1.50", "8753"]
```

- More cross-platform than PowerShell, but Python is still a dependency not guaranteed on Windows
- No meaningful improvement for Linux/Mac users who already have `nc`
- Same maintenance burden as the PS1

**Option E — tgcware OpenSSH on IRIX 5.3**

Install tgcware's openssh-6.8p1 on IRIX 5.3. Keep the SSH-based transport as-is for all versions.

- Correct answer if the machine is on a less-trusted network where raw TCP is unacceptable
- Adds tgcware + prngd + openssl1 + zlib as install prerequisites on IRIX 5.3
- tgcware discontinued May 2015; packages still available but no future security updates
- Modern clients (OpenSSH 8.8+) disable `ssh-rsa` (SHA-1) by default — use Ed25519 host keys to avoid the compatibility issue
- Does not simplify IRIX 6.5/6.2 (still requires SGUG-RSE on those versions)

**Option F — Platform-native tools (the pragmatic approach)**

Accept that the host-side tool differs per platform and document each explicitly. All three platforms have a zero-install solution:

| Host platform | Tool | Install required | `.mcp.json` command |
|---|---|---|---|
| Linux | `nc` | No — built in | `"nc"` |
| macOS | `nc` | No — built in | `"nc"` |
| Windows | `tcp-bridge.ps1` | No — PowerShell built in, script ships in repo | `"powershell"` |
| Windows (alt) | WSL `nc` | Only if WSL already present | `"wsl"` with `"nc"` arg |

`tcp-bridge.ps1` already exists, works, and handles the MCP session lifecycle correctly. The only downside is that the `.mcp.json` entry looks different on Windows vs. Linux/Mac. This is a documentation burden, not a technical one.

### Authentication and access control

All TCP-based options (B, C, F) carry no cryptographic authentication. Any device that can reach the inetd port can connect and use the MCP server within policy. Two mitigations are available without adding code to the server.

**TCP Wrappers — recommended, base IRIX on all versions**

IRIX ships `tcpd` (TCP Wrappers). Restrict the inetd MCP port to a specific host:

```sh
# /etc/hosts.deny
ALL: ALL

# /etc/hosts.allow
mcpmcp: 192.168.1.50    # only your Claude Code host IP
```

This reduces exposure from "any LAN device" to "one specific machine." It is IP-based, not cryptographic, but it is meaningful, requires no new packages, and works identically on IRIX 5.3, 6.2, and 6.5. The MCP policy engine (path restrictions, command whitelist, deny patterns) provides the second layer — limiting what any connected client can actually do.

**Bind to a specific interface**

Configure inetd to listen only on the IRIX machine's LAN IP rather than `0.0.0.0`. Combined with TCP Wrappers this provides two independent controls.

**Why not telnet?**

Telnet was considered as an alternative that provides login-based authentication without encryption. It is not suitable as an MCP transport for two reasons:

1. The Telnet protocol injects binary `IAC` (0xFF) option-negotiation sequences into the byte stream immediately on connect. These bytes corrupt the MCP JSON-RPC stream before any real data is exchanged.
2. Telnet has no non-interactive command execution mode. SSH can run `ssh host /usr/bin/mcpserver stdio` and pipe that command's raw stdio to the local process. Telnet always produces an interactive login → shell session with prompts, which the MCP client cannot navigate.

RSH (`rsh host /usr/bin/mcpserver stdio`) does have the non-interactive execution model and is Option A above, but requires `.rhosts` setup and has no Windows client without MSYS2.

### Trade-off summary

| Option | Windows | Linux | macOS | IRIX packages needed | New code |
|---|---|---|---|---|---|
| A — RSH | Needs MSYS2 | Native | Native | None (rshd in base IRIX) | No |
| B — Netcat | No clean standalone option | Native | Native | None (inetd in base IRIX) | No |
| C — Go bridge binary | Ships with project (SmartScreen warning) | Ships with project | Ships with project (Gatekeeper warning) | None (inetd in base IRIX) | ~100 lines Go + CI pipeline |
| D — Python | Python must be installed | Native | Native | None (inetd in base IRIX) | Script |
| E — tgcware SSH | Native OpenSSH | Native | Native | tgcware + prngd + openssl1 + zlib | No |
| F — Platform-native | PowerShell built in | nc built in | nc built in | None (inetd in base IRIX) | No (PS1 exists) |

All TCP options should be paired with TCP Wrappers on the IRIX side.

### Decision (2026-06-06)

**Option F + TCP Wrappers — implemented.**

Platform-native transport is in place: `tcp-bridge.ps1` for Windows (ships in repo and on the releases page), `nc` for Linux and macOS. inetd on IRIX handles all three IRIX versions identically. TCP Wrappers (`hosts.allow`/`hosts.deny`) provides IP-based access control on the IRIX side. See `README.md` Quick Start §5 for the per-platform `.mcp.json` configuration.

**Option C (Go bridge binary) — future upgrade path.**

If the project gains a broader user base where a single uniform setup experience matters more than the build pipeline cost, Option C is the natural next step. It would replace `tcp-bridge.ps1`, consolidate the IRIS loopback and real-machine cases into one binary, and give all three host platforms an identical `.mcp.json` entry. The implementation is ~100 lines of Go plus a cross-compile CI step. The main friction is one-time Gatekeeper/SmartScreen bypass on first run (signing costs are not justified for this project). When that work is done, retire `tcp-bridge.ps1`.

**Option E (tgcware SSH)** remains the documented path for anyone whose IRIX machine is on a network where IP-based access control is insufficient.
