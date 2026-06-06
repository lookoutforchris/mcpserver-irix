# mcpserver-irix

A native, local-first MCP (Model Context Protocol) server for vintage SGI IRIX systems.

Gives modern AI coding agents (Claude Code, Codex) safe, bounded access to an IRIX workspace — reading files, writing within policy, searching source trees, and running constrained inspection commands — directly on real or emulated IRIX hardware.

No public HTTP listener. No OAuth. No cloud service. Transport: SSH for IRIX 6.5/6.2; inetd/TCP for IRIX 5.3.

**Status: v0.3.2 — fully functional on IRIX 6.5, 6.2, and 5.3. 17 MCP tools including build/compile/run. Man pages included.**

---

## Quick Start

> **Which IRIX version do you have?**
> - **IRIX 6.5 or 6.2** — follow [Quick Start for IRIX 6.5 / 6.2](#quick-start-for-irix-65--62) below (SSH transport)
> - **IRIX 5.3** — skip to [Quick Start for IRIX 5.3](#quick-start-for-irix-53) (inetd/TCP transport — no SSH required)

---

## Quick Start for IRIX 6.5 / 6.2

### What You Need

- An SGI workstation or server running **IRIX 6.5** or **6.2**, powered on and connected to your local network
- Your IRIX machine's **IP address or hostname** (e.g., `192.168.1.50` or `octane.local`)
- A **Windows, macOS, or Linux** workstation on the same local network
- **Claude Code** (or another MCP-capable AI client such as Codex) installed on your workstation
- The `mcpserver-0.3.2-irix65.tardist` (or `irix62`) file — download from the [GitHub releases page](https://github.com/lookoutforchris/mcpserver-irix/releases)
- SSH access to the IRIX machine (password login is fine for initial setup)

---

### Step 1 — Set Up SSH Key Access

Claude Code connects by running `ssh` in the background and cannot enter a password interactively. Configure key-based authentication first.

**On your Windows workstation (PowerShell):**

```powershell
ssh-keygen -t ed25519 -C "claude-irix"
cat $env:USERPROFILE\.ssh\id_ed25519.pub
```

**On macOS or Linux:**

```sh
ssh-keygen -t ed25519 -C "claude-irix"
cat ~/.ssh/id_ed25519.pub
```

Copy the output line (starts with `ssh-ed25519 ...`). Then SSH into your IRIX machine with your password and add the key:

```sh
mkdir -p /root/.ssh
chmod 700 /root/.ssh
echo "ssh-ed25519 AAAAC3... claude-irix" >> /root/.ssh/authorized_keys
chmod 600 /root/.ssh/authorized_keys
```

Test that passwordless login works:

```sh
ssh root@192.168.1.50 "echo 'SSH key login works'"
```

> **Note:** If sshd denies root logins, edit `/etc/ssh/sshd_config`, set `PermitRootLogin yes`, and run `killall -HUP sshd`.

---

### Step 2 — Install the Tardist

```sh
# Copy to the IRIX machine
scp mcpserver-0.3.2-irix65.tardist root@192.168.1.50:/tmp/

# SSH in and install
ssh root@192.168.1.50
inst -f /tmp/mcpserver-0.3.2-irix65.tardist
# At the Inst> prompt: go
# When prompted about saving the distribution: 2
quit
```

The installer places binaries at `/usr/sbin/mcpserverd` and `/usr/bin/mcpserver`, installs the init script, and creates rc symlinks for automatic startup.

---

### Step 3 — Configure Your First Project

```sh
ssh root@192.168.1.50

# Add a project (--ro = read-only; use --rw to allow writes)
mcpserver add myproject /home/chris/src/myproject --ro
mcpserver apply
mcpserver show
```

---

### Step 4 — Enable and Start the Daemon

```sh
mcpserver enable    # enables at boot and starts now
mcpserver status    # should show: Daemon: running
```

---

### Step 5 — Configure Claude Code

Create `.mcp.json` in your project directory on your workstation:

```json
{
  "mcpServers": {
    "irix-machine": {
      "type": "stdio",
      "command": "ssh",
      "args": [
        "-T",
        "-o", "BatchMode=yes",
        "root@192.168.1.50",
        "/usr/bin/mcpserver",
        "stdio"
      ]
    }
  }
}
```

Replace `root@192.168.1.50` with your IRIX username and IP address. The label `"irix-machine"` is your choice.

> **Codex users:** The same `.mcp.json` works with OpenAI Codex in VS Code.

---

### Step 6 — Test the Connection

Open Claude Code in the directory containing `.mcp.json` and ask:

> *"List the files in my IRIX project directory"*

To verify manually:

```sh
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1"}}}' | ssh root@192.168.1.50 /usr/bin/mcpserver stdio
```

Expected response:
```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"irix-mcpserver","version":"0.3.2"}}}
```

---

### Troubleshooting (IRIX 6.5 / 6.2)

| Symptom | Likely cause | Fix |
|---|---|---|
| SSH asks for a password | Key not set up | Repeat Step 1 |
| `mcpserver: command not found` | Tardist not installed or PATH issue | Verify `/usr/bin/mcpserver` exists; log out and back in |
| `Daemon: stopped` | Daemon not started | Run `mcpserver enable` or `mcpserver start` |
| `allowed: false` for all paths | No projects configured | Run `mcpserver add` and `mcpserver apply` |
| Claude connects but can't read files | Wrong path or `--ro` project | Check `mcpserver show` and verify the root path |

For logs: `mcpserver logs` or `/var/adm/SYSLOG` on the IRIX machine.

---

## Quick Start for IRIX 5.3

IRIX 5.3 does not include `sshd`. The MCP server is reached via inetd over a direct TCP connection — no SSH key setup required. Access is controlled by TCP Wrappers, which ships with base IRIX.

> **Using the IRIS emulator?** The inetd/TCP transport is already configured on the emulator image. See [`docs/IRIS_EMULATOR_SETUP.md`](docs/IRIS_EMULATOR_SETUP.md) for the emulator-specific workflow.

### What You Need

- An SGI workstation running **IRIX 5.3**, powered on and connected to your local network
- A way to copy files to the IRIX machine (NFS, FTP, `rcp`, or removable media)
- A **Windows, macOS, or Linux** workstation on the same local network
- **Claude Code** (or another MCP-capable AI client such as Codex) installed on your workstation
- `mcpserver-0.3.2-irix53.tardist` and `tcp-bridge.ps1` (Windows only) — download both from the [GitHub releases page](https://github.com/lookoutforchris/mcpserver-irix/releases)

---

### Step 1 — Install the Tardist

Copy `mcpserver-0.3.2-irix53.tardist` to your IRIX 5.3 machine by NFS, FTP, `rcp`, or removable media.

> **Important:** IRIX 5.3 `inst` cannot install directly from a `.tardist` file — it reports "bad product". Unpack to a directory first:

```sh
mkdir /tmp/mcpinst
cd /tmp/mcpinst
tar xf /path/to/mcpserver-0.3.2-irix53.tardist
inst -f /tmp/mcpinst
# At the Inst> prompt: install all → go → quit
```

If `mcpserver version` still shows an old version after install, copy the binaries manually:

```sh
cp /tmp/mcpinst/mcpserverd /usr/sbin/mcpserverd
cp /tmp/mcpinst/mcpserver  /usr/bin/mcpserver
```

---

### Step 2 — Configure Your First Project

```sh
mcpserver add myproject /home/chris/src/myproject --ro
mcpserver apply
mcpserver show
```

---

### Step 3 — Enable and Start the Daemon

```sh
/sbin/chkconfig -f mcpserver on
/etc/init.d/mcpserverd start
mcpserver status    # should show: Daemon: running
```

---

### Step 4 — Configure inetd

inetd listens on port 8753 and spawns `mcpserver stdio` for each incoming connection.

```sh
# Add the service port (check /etc/services first — add only if not present)
echo "mcpmcp  8753/tcp" >> /etc/services

# Add the inetd handler
echo "mcpmcp  stream  tcp  nowait  root  /usr/bin/mcpserver  mcpserver stdio" >> /etc/inetd.conf

# Reload inetd
killall -HUP inetd
```

---

### Step 5 — Restrict Access with TCP Wrappers

TCP Wrappers limits which hosts can reach the MCP port. Verify `tcpd` exists at `/usr/etc/tcpd` before proceeding.

```sh
# Deny all connections by default
echo "ALL: ALL" >> /etc/hosts.deny

# Allow only your Claude Code workstation
echo "mcpmcp: 192.168.1.50" >> /etc/hosts.allow
```

Replace `192.168.1.50` with the IP address of your Windows, Linux, or macOS workstation.

---

### Step 6 — Configure Claude Code

Create `.mcp.json` in your project directory on your workstation. The entry differs by host platform.

**Linux or macOS** — `nc` is built in:

```json
{
  "mcpServers": {
    "irix-53": {
      "type": "stdio",
      "command": "nc",
      "args": ["192.168.1.50", "8753"]
    }
  }
}
```

**Windows** — uses `tcp-bridge.ps1` (downloaded from the releases page in the prerequisite step):

```json
{
  "mcpServers": {
    "irix-53": {
      "type": "stdio",
      "command": "powershell",
      "args": [
        "-NonInteractive", "-File",
        "C:/path/to/tcp-bridge.ps1",
        "-RemoteHost", "192.168.1.50",
        "-Port", "8753"
      ]
    }
  }
}
```

Replace `C:/path/to/tcp-bridge.ps1` with where you saved the script, and `192.168.1.50` with your IRIX machine's IP.

> **Codex users:** The same `.mcp.json` works with OpenAI Codex in VS Code.

---

### Step 7 — Test the Connection

Open Claude Code in the directory containing `.mcp.json` and ask:

> *"List the files in my IRIX project directory"*

To verify manually (Linux/macOS):

```sh
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1"}}}' | nc 192.168.1.50 8753
```

Expected response:
```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"irix-mcpserver","version":"0.3.2"}}}
```

---

### Troubleshooting (IRIX 5.3)

| Symptom | Likely cause | Fix |
|---|---|---|
| `nc` hangs with no output | inetd not configured or not reloaded | Verify `/etc/inetd.conf` entry; run `killall -HUP inetd` |
| Connection refused on port 8753 | inetd not listening | Check `netstat -a` on IRIX for port 8753 |
| `mcpserver: command not found` | Tardist not installed | Verify `/usr/bin/mcpserver` exists; repeat Step 1 |
| `Daemon: stopped` | Daemon not started | Run `/etc/init.d/mcpserverd start` |
| `allowed: false` for all paths | No projects configured | Run `mcpserver add` and `mcpserver apply` |
| Connection from host rejected | TCP Wrappers blocking | Check `/etc/hosts.allow` — verify host IP is listed |

For logs: `mcpserver logs` or `/var/adm/SYSLOG` on the IRIX machine.

---

## What the AI Agent Can Do

Once connected, the AI agent has access to 17 MCP tools organized into three categories:

**Read / Inspect (always available):**
`ping` · `path_exists` · `stat_path` · `list_directory` · `read_text_file` · `tail_text_file` · `search_text` · `read_text_around_pattern` · `safe_json_preview`

**Execution (advertised always; full profile required to invoke):**
`run_inspect_command` (37 whitelisted system tools including `ls`, `grep`, `hinv`, `man`) · `run_build_command` (`cc`, `make`, `ar`, `tar`, `gendist`, `inst`, etc.) · `run_program` (run a compiled binary inside a policy root)

**Write (read-write projects only):**
`create_text_file` · `replace_text_file` · `make_directory` · `delete_text_file` · `rename_path`

**Access control:** All operations are checked against the configured project roots and deny patterns before any filesystem or process operation occurs. The AI cannot access paths outside configured roots, cannot execute arbitrary shell commands, and cannot read credential files (`.env`, `*.key`, `*.pem`, etc.) regardless of policy.

See [`docs/TOOL_CONTRACT.md`](docs/TOOL_CONTRACT.md) and [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) for the full specification.

---

## Architecture

```
AI Client (Claude Code / Codex) on Windows or Mac
  │
  │  SSH (key auth, stdin/stdout)
  │
  ▼
mcpserver stdio  ← launched by SSH on IRIX
  │
  │  UNIX domain socket (/var/run/mcpserverd.sock)
  │
  ▼
mcpserverd  ← long-running daemon on IRIX
  │
  ├─ reads /etc/mcpserver/boundaries.json  (access policy)
  ├─ logs to syslog
  └─ forks a child for each client connection (multiple sessions supported)
```

Three components ship as a single package:

- **`mcpserverd`** — daemon at `/usr/sbin/mcpserverd`
- **`mcpserver`** — operator CLI at `/usr/bin/mcpserver`
- **`mcpserver stdio`** — stdio bridge mode invoked by the SSH command

---

## Operator Reference

```sh
mcpserver status                        # show daemon state and boot config
mcpserver start / stop / restart        # manual daemon control
mcpserver enable / disable              # boot-time chkconfig control

mcpserver add <name> <root> --rw|--ro [--deny <pattern>...]
mcpserver remove <name>
mcpserver show                          # list configured projects
mcpserver preview                       # preview generated boundaries.json
mcpserver apply                         # write boundaries.json, reload daemon
mcpserver validate                      # validate boundaries.json
mcpserver logs [N]                      # show last N syslog lines (default 80)
```

After any `add` or `remove`, run `mcpserver apply` to put the change into effect.

---

## Building from Source

```sh
# Clone the repository on your Windows workstation
git clone https://github.com/lookoutforchris/mcpserver-irix.git

# Pull and build on the IRIX machine (IRIX 6.5 / MIPSpro 7.4)
ssh root@192.168.1.50 "cd /home/work/projects/mcpserver-irix && git pull && make"

# Install
ssh root@192.168.1.50 "cd /home/work/projects/mcpserver-irix && make install"
mcpserver enable

# Build a tardist for distribution
ssh root@192.168.1.50 "cd /home/work/projects/mcpserver-irix && make tardist"
```

See [`docs/PORTABILITY_MATRIX.md`](docs/PORTABILITY_MATRIX.md) for IRIX 6.2 and 5.3 build targets.

---

## Target Platforms

| Platform | ABI | ISA | Compiler | Status |
|---|---|---|---|---|
| IRIX 6.5 | N32 | MIPS-IV | MIPSpro 7.4 | **Shipping (v0.3.2)** |
| IRIX 6.2 | N32 | MIPS-III | MIPSpro 7.4 (cross-compiled on 6.5) | **Shipping (v0.3.2)** |
| IRIX 5.3 | O32 | MIPS-II | IDO ucode `cc` | **Shipping (v0.3.2)** ¹ |

¹ **IRIX 5.3 remote transport limitation:** IRIX 5.3 does not include `sshd` in the base OS. The SSH-based Quick Start (Steps 1–6) applies to IRIX 6.5 and 6.2 only, where SGUG-RSE provides OpenSSH. For real IRIX 5.3 hardware accessed remotely, a different transport is required. See [`docs/FUTURE.md`](docs/FUTURE.md) — *Remote transport for real IRIX machines* — for the options under consideration. The IRIS emulator setup (which runs IRIX 5.3 locally on the same machine as Claude Code) is unaffected.

---

## Documentation

| Document | Purpose |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Component design, IPC, install paths, service integration |
| [`docs/TOOL_CONTRACT.md`](docs/TOOL_CONTRACT.md) | All 17 MCP tool specifications and return formats |
| [`docs/CONFIG_SCHEMA.md`](docs/CONFIG_SCHEMA.md) | `projects.json` and `boundaries.json` schemas |
| [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) | Path policy, write policy, command policy |
| [`docs/PORTABILITY_MATRIX.md`](docs/PORTABILITY_MATRIX.md) | Per-target compiler/ABI/syscall facts |
| [`AGENTS.md`](AGENTS.md) | Rules for AI coding agents working in this repo |

---

## License

MIT — see [LICENSE](LICENSE) for details.
