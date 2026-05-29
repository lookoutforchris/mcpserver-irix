# IRIS Emulator Setup for IRIX 5.3 Development

This document describes how to set up the IRIS emulator as a development
environment for building and testing mcpserver-irix on IRIX 5.3.

IRIS is an SGI Indy (MIPS R4400) emulator. Repository: https://github.com/techomancer/iris

---

## Status Summary

| Capability | Status |
|---|---|
| IRIX 5.3 boots and runs | Working |
| Serial console (port 8881) | Working — primary interactive method |
| Telnet console (port 2323) | Unreliable — use serial |
| Host→guest TCP port-forward | Working (Windows can connect to IRIX) |
| Guest→host TCP port-forward | **BROKEN** — IRIS bug (see §12) |
| MCP transport over inetd/TCP | **Not functional** — blocked by port-forward bug |
| Scratch disk file transfer | Working |
| TFTP file transfer | Unreliable in practice — use scratch disk instead (see §9) |
| NFS (NFSv2, unfsd) | Partial — first `ls` works; repeated listing broken (IRIX 5.3 kernel, see §10) |
| iris-ci (headless CI) on Windows | Not buildable — uses Unix socket APIs |

---

## 1. Building IRIS on Windows

IRIS is written in Rust. Build with the MSYS2 UCRT64 Rust toolchain.

**Prerequisites:**
- MSYS2 installed at `C:\dev\msys64`
- Rust installed via MSYS2 UCRT64: `pacman -S mingw-w64-ucrt-x86_64-rust`

**Build (from MSYS2 UCRT64 terminal):**
```sh
git clone https://github.com/techomancer/iris C:/dev/tools/iris
cd /c/dev/tools/iris
cargo build --release --bin iris    # NOT --bin iris-ci (see note below)
```

The binary is at `C:\dev\tools\iris\target\release\iris.exe`.

**iris-ci is not buildable on Windows.** `iris-ci` (headless automation binary)
uses `std::os::unix::net::UnixStream` which is not available on Windows. Only
build `--bin iris`.

---

## 2. Preparing the IRIX 5.3 Disk Image

IRIS requires a raw disk image. A pre-installed IRIX 5.3 image in MAME CHD
format (`irix53.chd`) can be converted using `chdman` from the MAME package:

```sh
# chdman.exe is in the MAME distribution (C:\dev\tools\mame\chdman.exe)
chdman.exe extractraw -i irix53.chd -o images/5.3/scsi1.raw
```

Place the raw image at `C:\dev\tools\iris\images\5.3\scsi1.raw`.

---

## 3. Configuration — `iris53.toml`

The configuration file is at `C:\dev\tools\iris\images\5.3\iris53.toml`.

Key points:
- `nat_subnet = "10.53.0.0/24"` — isolated from real LAN; IRIS gateway is `10.53.0.1`, IRIX is `10.53.0.2`.
- The `[nfs]` section is active; `unfsd` path points to the separately-built unfs3 binary (see §10).
- Port-forwards are in place but guest→host direction is currently broken (§12).

```toml
headless = false
no_audio = true
banks = [128, 128, 0, 0]
scale = 1

# NAT subnet — isolated from real LAN. Gateway: 10.53.0.1, guest: 10.53.0.2.
nat_subnet = "10.53.0.0/24"

# NFS share — point to a separately-built unfsd binary (see §10).
# Mount from IRIX: mount 10.53.0.1:/c/dev/tools/iris/shared /mnt/host
# NOTE: also requires nfs-proxy.py running on Windows (see §10).
[nfs]
shared_dir = "shared"
unfsd      = "C:/dev/tools/unfs3/unfsd.exe"

# IRIX 5.3 hard disk
[scsi.1]
path  = "images/5.3/scsi1.raw"
cdrom = false

# Scratch device for host↔guest file transfer (auto-created by IRIS)
[scsi.2]
path    = "images/5.3/scratch.raw"
cdrom   = false
scratch = true
size_mb = 350

# MCP server port (inetd on IRIX; guest→host direction broken — see §12)
[[port_forward]]
proto      = "tcp"
host_port  = 8753
guest_port = 8753
bind       = "localhost"

# SSH (for when OpenSSH is installed on IRIX 5.3)
[[port_forward]]
proto      = "tcp"
host_port  = 2253
guest_port = 22
bind       = "localhost"

# Telnet
[[port_forward]]
proto      = "tcp"
host_port  = 2323
guest_port = 23
bind       = "localhost"
```

---

## 4. Starting IRIS

From PowerShell in `C:\dev\tools\iris`:
```powershell
.\target\release\iris.exe --config images/5.3/iris53.toml
```

---

## 5. First Boot — NVRAM Setup (one-time)

On first boot (or after rebuilding the IRIS binary), the Ethernet MAC address
must be set or IRIX will kernel panic.

1. Let the PROM countdown reach the `>>` prompt (or press Escape → 5)
2. At `>>`: `setenv -f eaddr 08:00:69:de:ad:53`
3. Let IRIX boot
4. Connect to the IRIS monitor console: `telnet localhost 8888`
5. At monitor `>`: `rtc save` (persists MAC to `nvram.bin`)

The `nvram.bin` file in the IRIS directory preserves settings across restarts.
This must be repeated any time the IRIS binary is rebuilt from source.

---

## 6. Connecting to IRIX 5.3

**Use the serial console — it is the only reliable connection method.**

The telnet forward (port 2323) works for host→guest connections, but due to
the port-forward bug (§11) the IRIS guest cannot reliably establish the TCP
session in all circumstances. The serial console (port 8881) has no such issue.

**PuTTY configuration for serial console:**
- Host: `localhost`, Port: `8881`
- Connection type: **Raw**
- Terminal → Local echo: **Force off**
- Terminal → Local line editing: **Force off**

After connecting, press Enter to get a login prompt. Log in as `root`
(no password on the base MAME image).

**First time only — fix `/var/run` missing:**
```sh
mkdir -p /var/run
```

**Activate tcsh (if available) or ksh:**
```sh
chsh root /bin/tcsh   # or /bin/ksh for line editing
exec tcsh
```

---

## 7. IRIX 5.3 Networking Configuration (one-time)

IRIS runs IRIX in an isolated NAT network (`10.53.0.0/24`). The IRIX guest
is **not** directly on the real LAN, but real LAN traffic is NATed through
Windows — other machines (e.g., Galaxy at `192.168.0.5`) are reachable from IRIX.

- Gateway (IRIS): `10.53.0.1`
- IRIX guest: `10.53.0.2`

Using a non-overlapping subnet is essential. If the IRIS subnet matches your
real LAN (`192.168.0.x`), IRIX tries to ARP for real hosts directly on the
virtual network and gets no response. With a distinct subnet, all real LAN
traffic routes through the IRIS NAT gateway → Windows → real LAN.

Create these files on IRIX 5.3 (via serial or telnet console):

```sh
# Hostname
echo "indy53" > /etc/sys_id

# Hosts file
cat > /etc/hosts << 'EOF'
127.0.0.1       localhost
10.53.0.2       indy53
EOF

# Ethernet interface (ec0 at 10.53.0.2)
echo "inet 10.53.0.2 netmask 0xffffff00 broadcast 10.53.0.255" \
    > /etc/config/ifconfig-ec0.options

# Default route via IRIS NAT gateway
echo "10.53.0.1" > /etc/config/static-route.options

# Enable networking at boot
echo "on" > /etc/config/network
```

Bring the interface up immediately (without rebooting):
```sh
/etc/init.d/network start
# or manually:
ifconfig ec0 inet 10.53.0.2 netmask 0xffffff00 broadcast 10.53.0.255
route add default 10.53.0.1
# Verify:
ifconfig ec0
ping -c 3 10.53.0.1      # IRIS gateway — always responds
ping -c 3 192.168.0.5    # Galaxy — reachable via NAT
```

---

## 8. MCP Server Transport via inetd

The transport uses inetd on IRIX 5.3 listening on port 8753, which spawns
`mcpserver stdio` per connection. IRIS forwards port 8753 from the Windows
host to the IRIX 5.3 guest.

> **Note:** IRIS has a TCP port-forward bug (see §12) where the guest→host
> direction of data does not reach the Windows socket. The `tcp-bridge.ps1`
> workaround below resolves this — it keeps the TCP connection alive until
> IRIS delivers the buffered data. The transport is **fully functional** with
> this workaround.

**On IRIX 5.3 (already configured on the current disk image):**

`/etc/services` contains:
```
mcpmcp  8753/tcp
```

`/etc/inetd.conf` contains:
```
mcpmcp  stream  tcp  nowait  root  /usr/bin/mcpserver  mcpserver stdio
```

inetd is running and spawns `mcpserver stdio` on each connection.

**On Windows — `.mcp.json` entry (already in place):**

```json
{
  "mcpServers": {
    "irix-indy53": {
      "type": "stdio",
      "command": "powershell",
      "args": [
        "-NonInteractive", "-File",
        "c:/dev/projects/mcpserver-irix/scripts/tcp-bridge.ps1",
        "-RemoteHost", "localhost", "-Port", "8753"
      ]
    }
  }
}
```

`tcp-bridge.ps1` is a PowerShell stdio↔TCP bridge that connects Claude Code's
stdio to the IRIS-forwarded port 8753. It uses `CopyToAsync` on separate threads
to handle bidirectional forwarding correctly (see script for details).

---

## 9. File Transfer — Scratch Device Workflow

The scratch device is the reliable host↔guest file transfer method.

**Windows → IRIX 5.3 (write a tar to scratch, extract on IRIX):**

```sh
# Windows: write tar starting at sector 8 (after the 4KB SGI Volume Header)
dd if=bundle.tar of=C:/dev/tools/iris/images/5.3/scratch.raw bs=512 seek=8 conv=notrunc

# IRIX 5.3: extract
mkdir /usr/tmp/bundle
cd /usr/tmp/bundle
dd if=/dev/rdsk/dks0d2vol bs=512 skip=8 count=<N> | tar xf -
```

Where `<N>` = ceil(file_size_bytes / 512) + 10 (some headroom).

**IRIX 5.3 → Windows (write a tar from IRIX, read on Windows):**

```sh
# IRIX 5.3: write
tar cf - file1 file2 | dd of=/dev/rdsk/dks0d2s0 bs=512

# Windows: read (sector 8 = start of partition 0 = payload)
dd if=C:/dev/tools/iris/images/5.3/scratch.raw of=output.tar bs=512 skip=8 count=<N>
```

**Key facts:**
- The scratch device is 350MB (configured in iris53.toml)
- IRIS auto-creates `scratch.raw` with a valid SGI Volume Header on first start
- The payload area starts at byte 4096 (sector 8) of `scratch.raw`
- From IRIX: `/dev/rdsk/dks0d2vol` = whole volume (including VH); `/dev/rdsk/dks0d2s0` = partition 0 (sector 8+)

---

## 10. NFS File Sharing (Host↔IRIX)

NFS is more convenient than the scratch disk for ongoing file transfer. IRIS
supports NFS by invoking an external `unfsd` binary that you must build separately
(see Components below). IRIS intercepts portmap/mountd traffic and forwards it to
the running unfsd process.

**Why NFS avoids the port-forward bug:** The TCP port-forward bug (§12) only
affects IRIX acting as a TCP *server* (guest→host direction). NFS makes IRIX
the *client* — it sends requests outward through IRIS's NAT, which works fine.

### Components

| Component | Location | Notes |
|---|---|---|
| `unfsd.exe` | `C:\dev\tools\unfs3\unfsd.exe` | NFSv2-capable build of unfs3 |
| `nfs-proxy.py` | `scripts/nfs-proxy.py` in this repo | Bridges IRIS's port 2049 to unfsd's 12049 |
| Shared folder | `C:\dev\tools\iris\shared\` | What IRIX mounts |

### Why nfs-proxy.py is needed (IRIS bug #4)

IRIX 5.3 uses NFSv2, which hardcodes NFS port 2049 rather than using
portmapper to discover it. IRIS correctly forwards the MOUNT protocol
(portmapper → unfsd port 11234), but for the NFS data port IRIS simply
forwards traffic from `10.53.0.1:2049` to `127.0.0.1:2049` literally.
unfsd, however, is configured to listen on port 12049 — so nothing is on 2049,
and every NFS operation silently drops, causing the mount command to hang
indefinitely.

`nfs-proxy.py` listens on `127.0.0.1:2049` (TCP + UDP) and transparently
relays all traffic to `127.0.0.1:12049` (unfsd). This bridges the gap until
IRIS is fixed to redirect 2049 → 12049 internally.

### Step-by-step setup

**Step 1 — Start the NFS proxy (Windows, before or after IRIS starts):**
```bash
# MSYS2 terminal:
nohup /c/dev/msys64/ucrt64/bin/python3 \
  /c/dev/projects/mcpserver-irix/scripts/nfs-proxy.py \
  > /c/dev/tools/iris/logs/nfs_proxy.log 2>&1 &
```

Or from PowerShell:
```powershell
Start-Process -NoNewWindow -FilePath "C:\dev\msys64\ucrt64\bin\python3.exe" `
  -ArgumentList "C:\dev\projects\mcpserver-irix\scripts\nfs-proxy.py" `
  -RedirectStandardOutput "C:\dev\tools\iris\logs\nfs_proxy.log" `
  -RedirectStandardError "C:\dev\tools\iris\logs\nfs_proxy.log"
```

Verify it's running:
```powershell
netstat -ano | Select-String ":2049 "
# Should show TCP and UDP 127.0.0.1:2049 LISTENING
```

**Step 2 — Start IRIS** (if not already running):
```powershell
cd C:\dev\tools\iris
.\target\release\iris.exe --config images/5.3/iris53.toml
```

IRIS launches the unfsd binary you configured, on ports 12049 (NFS) and 11234 (mountd).

**Step 3 — Mount from IRIX** (IRIX networking must be configured — §7):
```sh
mkdir -p /mnt/host
mount -t nfs 10.53.0.1:/c/dev/tools/iris/shared /mnt/host
ls /mnt/host
```

The mount completes in a few seconds. Files placed in `C:\dev\tools\iris\shared\`
on Windows are immediately visible under `/mnt/host` on IRIX.

### Unmounting
```sh
cd /          # leave the mount point first
umount /mnt/host
```

### Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `mount` hangs indefinitely | nfs-proxy.py not running | Start the proxy (Step 1) |
| `mount: no such file` | Wrong path | Path must be `/c/dev/tools/iris/shared` (lowercase drive) |
| `mount: permission denied` | IRIX networking not up | Run `ifconfig ec0` and `route add default 10.53.0.1` |
| Files not visible after write | Stale cache | Run `sync` on IRIX or re-stat the file |

---

## 11. TFTP File Transfer

TFTP uses UDP and bypasses the IRIS guest→host TCP forwarding bug entirely.
Use it to copy files from Windows into IRIX while the emulator is running.

### Setup (Windows — run once per session)

Start the TFTP server as Administrator (required for port 69):
```powershell
# In an Administrator PowerShell:
& "C:\dev\msys64\ucrt64\bin\python3.exe" "C:\dev\tools\tftpd\tftpd.py"
```

Non-admin alternative (port 6969):
```powershell
& "C:\dev\msys64\ucrt64\bin\python3.exe" "C:\dev\tools\tftpd\tftpd.py" --port 6969
```

By default serves files from `C:\dev\tools\iris\shared`.
Use `--dir <path>` to serve a different directory.

### Transferring files from IRIX 5.3

Interactive mode (standard port 69):
```sh
tftp
tftp> connect 10.53.0.1
tftp> binary
tftp> get myfile.tar /usr/tmp/myfile.tar
tftp> quit
```

With non-standard port:
```sh
tftp
tftp> connect 10.53.0.1 6969
tftp> binary
tftp> get myfile.tar /usr/tmp/myfile.tar
tftp> quit
```

### NFS vs TFTP vs Scratch Disk

| Method | Notes |
|---|---|
| Scratch disk | **Preferred** — reliable, no IRIS bugs; IRIS does not hold an exclusive lock so Windows can write while IRIX is running |
| TFTP | UDP-only, bypasses the TCP bug; but in practice transfers time out unpredictably — not reliable enough for routine use |
| NFS | Works for first `ls` and all file access by exact name; directory listing breaks after first use (IRIX 5.3 kernel limitation — unfixable from server side) |

---

## 12. Building mcpserver-irix on IRIX 5.3

After installing IDO (see §13):

```sh
cd /usr/tmp/mcpsrc          # or wherever source was extracted
make irix53-native SHELL=/bin/sh
```

Required build flags (already in the Makefile target):
- `-D_POSIX_SOURCE` — exposes `sigset_t` (hidden by `-ansi` on IRIX 5.3)
- `-D_BSD_TYPES` — exposes `struct timeval` in `<sys/time.h>`

`src/compat/snprintf.c` provides `snprintf` which is absent from IRIX 5.3 libc.

### Installing from a tardist on IRIX 5.3

**Important:** IRIX 5.3 `inst` cannot install directly from a `.tardist` file —
it reports "bad product". The tardist must be unpacked to a directory first:

```sh
mkdir /tmp/mcpinst
cd /tmp/mcpinst
tar xf /path/to/mcpserver-X.Y.Z-irix53.tardist
inst -f /tmp/mcpinst
```

At the `Inst>` prompt: `install all` then `go` then `quit`.

After installing, enable and start:
```sh
/sbin/chkconfig -f mcpserver on
/etc/init.d/mcpserverd start
mcpserver version
mcpserver status
```

**Note on existing binaries:** If `mcpserver`/`mcpserverd` already exist on the
system from a previous manual install, IRIX 5.3 `inst` may not overwrite them.
If `mcpserver version` shows the old version after install, copy the binaries
manually from the build directory:
```sh
cp /path/to/src/mcpserverd /usr/sbin/mcpserverd
cp /path/to/src/mcpserver  /usr/bin/mcpserver
/etc/init.d/mcpserverd start
```

**Note on tcsh:** The default shell on IRIX is tcsh. Use backticks for command
substitution, not `$(...)`. To stop the daemon: `mcpserver stop` or
`` kill `cat /var/run/mcpserverd.pid` ``.

---

## 13. Known IRIS Bugs

### Bug 1 — TCP Port-Forward (Guest→Host Direction Broken)

**Bug:** IRIS does not forward data from the IRIX guest TCP stack to the
Windows-side socket. The host→guest direction works correctly.

**Evidence from IRIS monitor (`telnet localhost 8888`, command `net status tcp`):**

```
slot  proto  state        local            remote           in_flight  rtx    cli_win
0     TCP    ESTABLISHED  192.168.0.2:8753 192.168.0.1:xxxxx  152B     1/152B  61440
```

- `in_flight=152` — IRIS received 152 bytes from the IRIX guest (the MCP `initialize` response)
- `rtx=1/152B` — IRIS has retransmitted the ACK request once (guest is waiting for an ACK)
- `cli_win=61440` — the Windows-side socket has a 60KB receive window (not flow-controlled)

IRIS's NAT layer receives the guest data but does not write it to the Windows TCP socket.
IRIS also does not send TCP ACKs back to the guest, causing the guest to retransmit indefinitely.

**What works:**
- Windows establishes TCP connection to IRIS port 8753 → IRIX inetd accepts it
- IRIX `mcpserver stdio` is spawned and processes the MCP `initialize` request
- IRIX sends the response to its TCP stack; IRIS NAT receives it

**What fails:**
- IRIS NAT does not deliver the response bytes to the Windows socket

**Reproduction:**
1. Configure IRIX networking (§7) and start `mcpserverd` (§11)
2. Start a TCP connection from Windows: `telnet localhost 8753`
3. Type the MCP initialize JSON and press Enter
4. Observe in IRIS monitor: `net status tcp` shows `in_flight` growing; Windows receives nothing

**Filed with developer:** https://github.com/techomancer/iris (open issue)

### Bug 2 — NFS Port 2049 Not Redirected to unfsd

**Bug:** When NFS is enabled, IRIS launches the external unfsd binary configured via
`[nfs] unfsd = ...` with custom ports (NFS on 12049, mountd on 11234), but does not
redirect incoming port-2049 NFS traffic from the guest to that port. IRIX 5.3's
NFSv2 client hardcodes port 2049 for all NFS operations (RFC 1094 era convention —
it does not query portmapper for NFS itself). IRIS forwards guest traffic aimed at
`10.53.0.1:2049` to `127.0.0.1:2049` literally. Nothing listens there, so every UDP
NFS packet is silently dropped. The guest retries indefinitely and `mount` hangs forever
with no timeout and no error message.

**Evidence from IRIS network monitor (`telnet localhost 8888`, `log net on`):**

```
[net] NAT TX ... IPv4 10.53.0.2 > 10.53.0.1  UDP :1023->2049  158 bytes
[net] NAT UDP 10.53.0.2:1023 -> 10.53.0.1:2049
[net] NAT UDP 10.53.0.2:1023 -> 127.0.0.1:2049 len=116 [new]
```

IRIS correctly intercepts the MOUNT portmapper query and returns port 11234 (allowing
the mount handshake to succeed), but then routes NFS data UDP to `localhost:2049` where
unfsd is not listening.

**What works:**
- MOUNT v1 handshake — IRIS intercepts the portmapper GETPORT for MOUNTPROG and returns 11234
- IRIX receives a valid 32-byte NFSv2 file handle from unfsd
- IRIX proceeds to send NFS operations

**What fails:**
- All NFS operations after mount (GETATTR, LOOKUP, READ, etc.) go to port 2049
- IRIS forwards them to `localhost:2049`; nothing is there; packets are silently dropped
- IRIX retries indefinitely; `mount` hangs forever

**Workaround:** Run `nfs-proxy.py` (see §10) before mounting. It listens on
`localhost:2049` and proxies all UDP and TCP to `localhost:12049` (unfsd's actual port).

**Expected fix:** When unfsd is configured via `-n <port>`, IRIS should redirect
`10.53.0.1:2049` → `127.0.0.1:<port>` instead of `127.0.0.1:2049`.

---

## 14. Installing IDO on IRIX 5.3

The IDO C compiler is distributed as a network install dist tree. Transfer and install:

**Step 1 — Write IDO dist to scratch device (Windows):**
```sh
tar cf /tmp/ido.tar -C "C:/path/to/IRIX Network Install/5.3" "IRIS Development Option 5.3.tar"
dd if=/tmp/ido.tar of=C:/dev/tools/iris/images/5.3/scratch.raw bs=512 seek=8 conv=notrunc
```

**Step 2 — Extract and install (IRIX 5.3):**
```sh
mkdir /usr/tmp/ido && cd /usr/tmp/ido
dd if=/dev/rdsk/dks0d2vol bs=512 skip=8 | tar xf -
inst -f /usr/tmp/ido/dist
# At Inst>: install c_dev
#           go
```

After installation, `/usr/bin/cc` is the IDO ucode C compiler.

---

## 15. Known Issues (Other)

| Issue | Workaround |
|---|---|
| `make irix53-native` fails with separator error | Run as `make irix53-native SHELL=/bin/sh` |
| `mcpserver start` hung (old behavior) | Fixed in v0.2.0 — use fork/exec |
| `inst -f tardist` says "bad product" | Use manual install; 6.5 gendist format incompatible with 5.3 inst |
| `/var/run/mcpserverd.sock` fails on bind | Create `/var/run/` first: `mkdir -p /var/run` |
| Serial console echoes double characters | Set PuTTY to Raw mode with local echo/editing forced off |
| csh truncates long commands | Break into short lines; use `make irix53-native SHELL=/bin/sh` |
| NFS `mount` hangs hard | nfs-proxy.py not running — IRIS bug: port 2049 not redirected to unfsd (see §10, §13) |
| NFS mount path on IRIX 5.3 | Server is `10.53.0.1` (IRIS gateway), path is `/c/dev/tools/iris/shared` |
| IRIX cannot reach real LAN | By design — use NAT gateway 10.53.0.1; real LAN hosts reachable via NAT |
| `iris-ci` build fails on Windows | Unix socket API (`UnixStream`) not available on Windows; Unix-only binary |
| IRIS binary rebuilt — IRIX won't boot | Re-run NVRAM setup (§5): `setenv -f eaddr 08:00:69:de:ad:53` then `rtc save` |
| Telnet 2323 unreliable | Use serial console (port 8881) instead |
