# mcpserver-irix

A native, local-first MCP (Model Context Protocol) server for vintage SGI IRIX systems.

Gives modern AI coding agents (Claude Code, Codex) safe, bounded access to an IRIX workspace — reading files, writing within policy, searching source trees, and running constrained inspection commands — directly on real or emulated IRIX hardware over SSH.

No public HTTP listener. No OAuth. No cloud service. Transport security is SSH.

**Status: v0.3.0 — fully functional on IRIX 6.5, 6.2, and 5.3. 23 MCP tools including build/compile/run. Man pages included.**

---

## Quick Start — Setting Up on a Fresh System

This guide assumes you are a newcomer to this software. Follow each step in order.

### What You Need Before Starting

- An SGI workstation or server running **IRIX 6.5**, powered on and connected to your local network
- Your IRIX machine's **IP address or hostname** (e.g., `192.168.1.50` or `octane.local`)
- A **Windows 10/11 or macOS** workstation on the same local network
- **Claude Code** (or another MCP-capable AI client such as Codex) installed on your Windows/Mac workstation
- The `mcpserver-0.1.0-irix65.tardist` file (download from the [GitHub releases page](https://github.com/lookoutforchris/mcpserver-irix/releases))
- SSH access to the IRIX machine (password login is fine for setup; we will configure key-based login below)

---

### Step 1 — Set Up SSH Key Access (Passwordless Login)

Claude Code connects to your IRIX machine by running `ssh` in the background. It cannot enter a password interactively, so you must configure key-based authentication first.

**On your Windows workstation (PowerShell or Windows Terminal):**

```powershell
# Generate an SSH key pair (press Enter to accept all defaults)
ssh-keygen -t ed25519 -C "claude-irix"

# Display your public key — you will need this in a moment
cat $env:USERPROFILE\.ssh\id_ed25519.pub
```

**On macOS (Terminal):**

```sh
# Generate an SSH key pair (press Enter to accept all defaults)
ssh-keygen -t ed25519 -C "claude-irix"

# Display your public key
cat ~/.ssh/id_ed25519.pub
```

The output looks something like:
```
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAA... claude-irix
```
Copy this entire line to your clipboard.

**Now SSH into your IRIX machine with your password:**

```sh
ssh root@192.168.1.50
```

Replace `192.168.1.50` with your actual IRIX IP address or hostname. Once logged in to the IRIX machine:

```sh
# Create the SSH directory if it does not exist
mkdir -p /root/.ssh
chmod 700 /root/.ssh

# Add your public key (paste the line you copied above inside the quotes)
echo "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAA... claude-irix" >> /root/.ssh/authorized_keys
chmod 600 /root/.ssh/authorized_keys
```

**Test that passwordless login works:**

```sh
# From your Windows/Mac workstation — this should log in without asking for a password
ssh root@192.168.1.50 "echo 'SSH key login works'"
```

If you see `SSH key login works` without being asked for a password, you are ready for the next step.

> **Note:** If your IRIX SSH daemon is configured to deny root logins, you may need to edit `/etc/ssh/sshd_config` and set `PermitRootLogin yes`, then restart sshd with `killall -HUP sshd`. Alternatively, set up the key for a non-root user and adjust the install paths accordingly.

---

### Step 2 — Install the Tardist on Your IRIX Machine

Copy the tardist file to your IRIX machine:

```sh
# From your Windows/Mac workstation
scp mcpserver-0.1.0-irix65.tardist root@192.168.1.50:/tmp/
```

SSH into your IRIX machine and install:

```sh
ssh root@192.168.1.50

# Install using IRIX inst (the standard IRIX package installer)
inst -f /tmp/mcpserver-0.1.0-irix65.tardist

# At the Inst> prompt, type:
go

# When prompted about saving the distribution, type 2 to remove it
# When inst finishes you should see:
#   MCP Server for IRIX 6.5 installed.
#   Configure: mcpserver add <name> <root> --ro
#   Apply:     mcpserver apply
#   Enable:    mcpserver enable

quit
```

The installer places binaries at `/usr/sbin/mcpserverd` and `/usr/bin/mcpserver`, installs the init script at `/etc/init.d/mcpserverd`, and creates the rc symlinks for automatic startup.

> **Alternatively**, you can drag the `.tardist` file into the IRIX Software Manager application if you have a graphical session running.

---

### Step 3 — Configure Your First Project

A "project" tells the server which directory on your IRIX filesystem the AI agent is allowed to access, and whether it may write files. You need at least one project before starting the daemon.

```sh
# SSH into your IRIX machine
ssh root@192.168.1.50

# Add a project — replace the path with your actual source directory
# --ro means read-only (safe starting point); use --rw to allow file writes
mcpserver add myproject /home/chris/src/myproject --ro

# Preview what the generated policy will look like (optional)
mcpserver preview

# Write the policy file that the daemon reads
mcpserver apply

# Confirm the project is registered
mcpserver show
```

Example output of `mcpserver show`:
```
myproject             read-only   /home/chris/src/myproject
```

---

### Step 4 — Enable and Start the Daemon

```sh
# Enable at boot and start now (one command does both)
mcpserver enable

# Verify it is running
mcpserver status
```

Expected output:
```
Boot:   enabled
Daemon: running (pid 1234)
```

The daemon will now start automatically every time the IRIX machine boots. You can also start and stop it manually with `mcpserver start` and `mcpserver stop`.

---

### Step 5 — Configure Claude Code on Windows or Mac

Create a file called `.mcp.json` in your project directory on your Windows or Mac workstation (the directory you open in Claude Code). Add the following content, replacing the hostname and username with your own:

```json
{
  "mcpServers": {
    "irix-octane2": {
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

Replace `root@192.168.1.50` with your IRIX username and IP address. The name `"irix-octane2"` is a label you choose — it appears in Claude Code's tool list.

> **For Codex users:** The same `.mcp.json` format works with OpenAI Codex in VS Code. Place the file at the root of your workspace.

---

### Step 6 — Test the Connection

In Claude Code, open the directory containing your `.mcp.json` file and ask Claude a question that requires reading from the IRIX machine, such as:

> *"List the files in my IRIX project directory"*

Claude Code will automatically connect to the daemon over SSH and use the MCP tools. If the connection works, you will see Claude listing files from your IRIX filesystem.

You can also verify manually from the command line:

```sh
# On your Windows/Mac workstation — send a ping to the IRIX daemon
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1"}}}' | ssh root@192.168.1.50 /usr/bin/mcpserver stdio
```

A successful response looks like:
```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"irix-mcpserver","version":"0.1.0"}}}
```

---

### Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| SSH asks for a password | Key not set up or not authorized | Repeat Step 1 |
| `mcpserver: command not found` | Tardist not installed or PATH issue | Verify `/usr/bin/mcpserver` exists; log out and back in |
| `Daemon: stopped` | Daemon not started | Run `mcpserver enable` or `mcpserver start` |
| `allowed: false` for all paths | No projects configured | Run `mcpserver add` and `mcpserver apply` |
| Claude can connect but not read files | Project configured as `--ro` and path wrong | Check `mcpserver show` and verify the root path |

For operational logs, run `mcpserver logs` or check `/var/adm/SYSLOG` on the IRIX machine.

---

## What the AI Agent Can Do

Once connected, the AI agent has access to 15 MCP tools organized into three categories:

**Read / Inspect (always available):**
`ping` · `path_exists` · `stat_path` · `list_directory` · `read_text_file` · `tail_text_file` · `search_text` · `read_text_around_pattern` · `safe_json_preview` · `run_inspect_command`

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
ssh root@192.168.1.50 "cd /home/chris/src/mcpserver-irix && git pull && make"

# Install
ssh root@192.168.1.50 "cd /home/chris/src/mcpserver-irix && make install"
mcpserver enable

# Build a tardist for distribution
ssh root@192.168.1.50 "cd /home/chris/src/mcpserver-irix && make tardist"
```

See [`docs/PORTABILITY_MATRIX.md`](docs/PORTABILITY_MATRIX.md) for IRIX 6.2 and 5.3 build targets.

---

## Target Platforms

| Platform | ABI | ISA | Compiler | Status |
|---|---|---|---|---|
| IRIX 6.5 | N32 | MIPS-IV | MIPSpro 7.4 | **Shipping (v0.2.0)** |
| IRIX 6.2 | N32 | MIPS-III | MIPSpro | Planned |
| IRIX 5.3 | O32 | MIPS-II | IDO ucode `cc` | **Shipping (v0.2.0)** |

---

## Documentation

| Document | Purpose |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Component design, IPC, install paths, service integration |
| [`docs/TOOL_CONTRACT.md`](docs/TOOL_CONTRACT.md) | All 15 MCP tool specifications and return formats |
| [`docs/CONFIG_SCHEMA.md`](docs/CONFIG_SCHEMA.md) | `projects.json` and `boundaries.json` schemas |
| [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) | Path policy, write policy, command policy |
| [`docs/PORTABILITY_MATRIX.md`](docs/PORTABILITY_MATRIX.md) | Per-target compiler/ABI/syscall facts |
| [`AGENTS.md`](AGENTS.md) | Rules for AI coding agents working in this repo |

---

## License

MIT — see [LICENSE](LICENSE) for details.
