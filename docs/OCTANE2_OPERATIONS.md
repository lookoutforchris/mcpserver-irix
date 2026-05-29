# Octane2 Operations Guide

System-level notes for the SGI Octane2 (IRIX 6.5.30) development machine at `speed.siliconsurf.net`.

---

## Machine Identity

| Property | Value |
|---|---|
| Hostname | `speed.siliconsurf.net` |
| IP | `192.168.0.10` |
| OS | IRIX 6.5.30 |
| SSH access | `ssh root@speed.siliconsurf.net` |
| SSH binary | `/usr/sgug/sbin/sshd` (SGUG package) |
| SSH config | `/usr/sgug/etc/ssh/sshd_config` |

---

## MCP Server

| Item | Path |
|---|---|
| Daemon binary | `/usr/sbin/mcpserverd` |
| CLI binary | `/usr/bin/mcpserver` |
| Init script | `/etc/init.d/mcpserverd` |
| Config | `/etc/mcpserver/boundaries.json` |
| Socket | `/var/run/mcpserverd.sock` |
| PID file | `/var/run/mcpserverd.pid` |
| Project source | `/home/work/projects/mcpserver-irix/` |
| Policy roots | `/home/work/projects` (rw), `/home/work/tools` (rw) |

Boot integration: `chkconfig mcpserver on`. The init script must background the daemon with `&` — without it, the daemon blocks the boot sequence indefinitely (confirmed failure mode, fixed 2026-05-23).

**SSH access is as root.** Git credentials in `/home/chris/.gitconfig` and `/home/chris/.git-credentials` are NOT visible to root. Public repo clones over HTTPS work without credentials. Authenticated git operations (push) must be done from Windows — Octane2 is never the git authority.

**Working directory discipline:** Always verify the current directory before running `make`, `gendist`, or any build command. Running build commands from `/` instead of the project root will scatter binaries and source directories across the filesystem root. After any build session, check `ls /` for stray `mcpserver*`, `packaging/`, or `scripts/` entries and remove them.

---

## NFS Layout

Galaxy (`192.168.0.5`) serves two shares, both permanently mounted via `/etc/fstab`:

| Galaxy export | Octane2 mount point | Access |
|---|---|---|
| `galaxy:/volume1/SS` | `/nfs/galaxy/SS` | rw |
| `galaxy:/volume1/SRR` | `/nfs/galaxy/SRR` | rw |
| `galaxy:/volume1/SS/Media/Music/Media` | `/home/chris/Music/Media` | ro |

Mount options: `vers=3,proto=tcp,bg,soft,intr` — `bg` prevents boot hang if Galaxy is unreachable.

**File transfer fast path:** `S:\` on Windows (SMB direct to Galaxy) and `/nfs/galaxy/SS` on Octane2 both point at the same Galaxy storage. Transfers between them are instant. Dropbox eventually syncs but adds delay — use `S:\` not the Dropbox folder for fast round-trips.

automount is **disabled** (`chkconfig automount off`). Do not re-enable.

---

## automount — Important IRIX Behavior

automount on IRIX is configured via `/etc/config/automount.options`, **not** via `auto_master` or `auto.master`. The man page explicitly states automount does not consult a local `auto.master` file unless `-f` is passed on the command line. The NIS "bind failed" warning at boot comes from the `-hosts` map querying ypbind — removing entries from `auto_master` has no effect on this.

---

## Disk Space

Root filesystem (`/dev/root`, XFS) is large but consistently near capacity. Watch `df -k /` — at 98% it can cause subtle failures. Periodic cleanup of `/tmp`, build artifacts, and core dumps is needed.

---

## Boot Sequence Notes

Known benign warnings at boot (do not investigate unless behavior changes):

| Warning | Source | Why benign |
|---|---|---|
| `automount: NIS bind failed` | automount probing ypbind | Expected — no NIS server, automount is disabled anyway |
| `mediad: Can't connect to fam` | startup race | mediad retries and recovers |

Fixed warnings (should not reappear):
- `sshd: Unsupported option GSSAPIAuthentication/UsePAM` — commented out in sshd_config
- `ntpd: keyword "monitor" unknown` — removed from `/etc/ntp.conf`
- `nsd: missing ':' in nsswitch.conf` — fixed by user

---

## Key System File Locations

| File | Notes |
|---|---|
| `/etc/fstab` | Filesystem/NFS mount table |
| `/etc/hosts` | Static hostname→IP table |
| `/etc/nsswitch.conf` | Name service lookup order (uses NSD) |
| `/etc/ntp.conf` | NTP server config |
| `/usr/sgug/etc/ssh/sshd_config` | SGUG OpenSSH server config |
| `/etc/config/automount.options` | automount daemon startup args (not used, automount disabled) |
| `/var/adm/SYSLOG` | System log (also accessible as `/usr/adm/SYSLOG`) |
