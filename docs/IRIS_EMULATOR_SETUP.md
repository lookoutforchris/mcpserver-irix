# IRIS Emulator Setup for IRIX 5.3 Development

This document describes how to set up the IRIS emulator as a development
environment for building and testing mcpserver-irix on IRIX 5.3.

IRIS is an SGI Indy (MIPS R4400) emulator. Repository: https://github.com/techomancer/iris

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
cargo build --release --bin iris    # not --bin iris-ci (Unix sockets only)
```

The binary is at `C:\dev\tools\iris\target\release\iris.exe`.

**Note:** `iris-ci` (headless automation binary) uses Unix sockets and cannot
be built on Windows. Build only `--bin iris`.

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

Create `C:\dev\tools\iris\images\5.3\iris53.toml`:

```toml
headless = false
no_audio = true
banks = [128, 128, 0, 0]
scale = 1

[scsi.1]
path  = "images/5.3/scsi1.raw"
cdrom = false

# Scratch device for host→guest file transfer (auto-created by IRIS)
[scsi.2]
path    = "images/5.3/scratch.raw"
cdrom   = false
scratch = true
size_mb = 350

# MCP server transport via inetd (see §7)
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

On first boot, the Ethernet MAC address must be set or IRIX will kernel panic.

1. Let the PROM countdown reach the `>>` prompt (or press Escape → 5)
2. At `>>`: `setenv -f eaddr 08:00:69:de:ad:53`
3. Let IRIX boot
4. Connect to the IRIS monitor console: `telnet localhost 8888`
5. At monitor `>`: `rtc save` (persists MAC to `nvram.bin`)

The `nvram.bin` file in the IRIS directory preserves settings across restarts.

---

## 6. Connecting to IRIX 5.3

The serial console gives the best interactive experience.

**PuTTY configuration (saves the double-echo problem):**
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

## 7. MCP Server Transport via inetd

IRIX 5.3 does not have SSH. Use inetd to expose `mcpserver stdio` over TCP.

**On IRIX 5.3 (one-time setup):**

Add to `/etc/services`:
```
mcpmcp  8753/tcp
```

Add to `/etc/inetd.conf`:
```
mcpmcp  stream  tcp  nowait  root  /usr/bin/mcpserver  mcpserver stdio
```

Reload inetd:
```sh
kill -HUP `cat /var/run/inetd.pid`
```

**On Windows — add to `.mcp.json`:**
```json
{
  "mcpServers": {
    "irix-indy53": {
      "type": "stdio",
      "command": "nc",
      "args": ["localhost", "8753"]
    }
  }
}
```

`nc` (netcat) from MSYS2 UCRT64 acts as the stdio transport. IRIS forwards
port 8753 to the IRIX 5.3 guest, where inetd launches `mcpserver stdio`
for each connection.

---

## 8. File Transfer — Scratch Device Workflow

Without NFS, use the IRIS scratch device to transfer files between Windows
and IRIX 5.3.

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
- From IRIX, `/dev/rdsk/dks0d2vol` is the whole volume (including VH)
- From IRIX, `/dev/rdsk/dks0d2s0` is partition 0 (starts at sector 8)

---

## 9. Building mcpserver-irix on IRIX 5.3

After installing IDO (see §10):

```sh
# Transfer source via scratch device, extract, then:
cd /usr/tmp/mcpsrc
make irix53-native SHELL=/bin/sh

# Install
cp mcpserverd /usr/sbin/mcpserverd
cp mcpserver /usr/bin/mcpserver
```

Required build flags (already in the Makefile target):
- `-D_POSIX_SOURCE` — exposes `sigset_t` (hidden by `-ansi` on IRIX 5.3)
- `-D_BSD_TYPES` — exposes `struct timeval` in `<sys/time.h>`

`src/compat/snprintf.c` provides `snprintf` which is absent from IRIX 5.3 libc.

---

## 10. Installing IDO on IRIX 5.3

The IDO C compiler is distributed as a network install dist tree (a directory
of `.idb` and `.sw` files). Transfer and install:

**Step 1 — Write IDO dist to scratch device (Windows):**
```sh
tar cf /tmp/ido.tar -C "C:/Users/.../IRIX Network Install/5.3" "IRIS Development Option 5.3.tar"
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

## 11. Known Issues

| Issue | Workaround |
|---|---|
| `make irix53-native` fails with separator error | Run as `make irix53-native SHELL=/bin/sh` |
| `mcpserver start` hung (old behavior) | Fixed in v0.2.0 — use fork/exec |
| `inst -f tardist` says "bad product" | Use manual install; 6.5 gendist format incompatible with 5.3 inst |
| `/var/run/mcpserverd.sock` fails on bind | Create `/var/run/` first: `mkdir -p /var/run` |
| Serial console echoes double characters | Set PuTTY to Raw mode with local echo/editing forced off |
| csh truncates long commands | Break into short lines; use `make irix53-native SHELL=/bin/sh` |
| NFS (unfsd) cannot build on Windows | unfsd requires Unix APIs; use scratch device instead or WSL |
