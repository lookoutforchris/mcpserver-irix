# Bug Report: NFS NAT Port Remapping Broken — `NFS_VM_PORT` Set to Host Port Instead of Guest Port

**Repository:** https://github.com/techomancer/iris  
**Component:** NFS NAT / `nfs_remap_dst()`  
**File:** `src/net.rs` line 39  
**IRIS version:** (built from main, 2026-05-26)  
**Host OS:** Windows 11 Pro (build 26200.8037)  
**Guest OS:** IRIX 5.3 (SGI Indy R4400)

---

## Summary

NFS mounts from an IRIX 5.3 guest hang indefinitely. The MOUNT handshake
completes successfully (IRIX receives a valid file handle), but all subsequent
NFS data operations are silently dropped. The root cause is a one-line constant
error in `src/net.rs`: `NFS_VM_PORT` is set to `12049` (the host-side unfsd
port) instead of `2049` (the standard NFSv2 port that the guest actually sends
to). The NAT remapping match in `nfs_remap_dst()` therefore never fires for
NFS data traffic, causing it to fall through to the generic case at
`localhost:2049` where nothing is listening.

---

## Configuration

`iris53.toml` excerpt (standard NFS config — no custom ports):

```toml
[nfs]
shared_dir = "shared"
unfsd      = "/path/to/unfsd"
```

IRIX guest networking:

- Interface: `ec0` at `10.53.0.2`, netmask `255.255.255.0`
- Gateway: `10.53.0.1` (IRIS NAT)
- Connectivity confirmed: `ping -c 1 10.53.0.1` succeeds

IRIX mount command:

```sh
mount -t nfs 10.53.0.1:/path/to/shared /mnt/host
```

---

## Reproduction Steps

1. Build IRIS and unfsd; configure `[nfs]` with default ports in `iris53.toml`.
2. Boot the IRIX guest; configure guest networking.
3. Run `mount -t nfs 10.53.0.1:<export_path> /mnt/host` from the IRIX shell.
4. Observe: the mount command hangs indefinitely. No error message, no timeout.
5. In the IRIS monitor (`telnet localhost 8888`), enable network logging:
   ```
   log net on
   log net file /path/to/iris_net.log
   ```
6. Observe in the log: NFS data packets are being forwarded to
   `127.0.0.1:2049`, not to unfsd's port.

---

## Network Monitor Evidence

With `log net on` enabled in the IRIS monitor, the following trace was captured
during a mount attempt. The MOUNT handshake (portmapper + mountd) completes
normally; NFS data traffic is then misrouted:

```
[net] NAT TX ... IPv4 10.53.0.2 > 10.53.0.1  UDP  :1023->111   portmap GETPORT NFS
[net] NAT portmap reply: NFS -> port 12049
[net] NAT TX ... IPv4 10.53.0.2 > 10.53.0.1  UDP  :1023->111   portmap GETPORT MOUNT
[net] NAT portmap reply: MOUNT -> port 11234
[net] NAT UDP 10.53.0.2:1023 -> 127.0.0.1:11234  (mountd — correct)
[net] NAT TX ... IPv4 10.53.0.2 > 10.53.0.1  UDP  :1023->2049  158 bytes
[net] NAT UDP 10.53.0.2:1023 -> 10.53.0.1:2049
[net] NAT UDP 10.53.0.2:1023 -> 127.0.0.1:2049 len=116 [new]
                                            ^^^^
                                            unfsd is not here — it is on 12049
```

The MOUNT request correctly reaches unfsd on port 11234. All NFS data requests
(GETATTR, LOOKUP, READ, WRITE, etc.) go to port 2049, which is forwarded
verbatim to `127.0.0.1:2049`. unfsd is not listening there. The UDP packets
are silently dropped. IRIX retries indefinitely.

---

## Direct unfsd Verification

To confirm unfsd itself is functioning correctly, NFS RPC calls were sent
directly to `127.0.0.1:12049` (bypassing IRIS NAT entirely). All tests passed:

```
--- MOUNT v1 NULL (127.0.0.1:11234/UDP) ---
  OK - unfsd replied to MOUNT v1 NULL

--- MOUNT v1 MNT '/path/to/shared' (127.0.0.1:11234/UDP) ---
  OK - got 32-byte file handle: <hex>

--- NFS v2 NULL (127.0.0.1:12049/UDP) ---
  OK - unfsd replied to NFS v2 NULL

--- NFS v2 GETATTR (127.0.0.1:12049/UDP) ---
  OK - NFS v2 GETATTR succeeded, attributes returned

All tests PASSED
```

unfsd is fully functional. The failure is entirely in IRIS's NAT routing.

---

## Root Cause

`src/net.rs`, line 39:

```rust
const NFS_VM_PORT: u16 = 12049;   // BUG: this is the host-side port
```

This constant is used in two places:

**`nfs_remap_dst()` (line 1073)** — remaps guest-outbound NFS packets to the
host unfsd port:

```rust
fn nfs_remap_dst(&self, dst_ip: Ipv4Addr, dport: u16) -> (Ipv4Addr, u16) {
    if dst_ip != self.config.gateway_ip { return (dst_ip, dport); }
    if let Some(nfs) = &self.config.nfs {
        match dport {
            NFS_VM_PORT    => return (Ipv4Addr::LOCALHOST, nfs.nfs_host_port),  // never matches
            MOUNTD_VM_PORT => return (Ipv4Addr::LOCALHOST, nfs.mountd_host_port),
            _ => {}
        }
    }
    (Ipv4Addr::LOCALHOST, dport)  // ← NFS traffic falls through to here
}
```

Because `NFS_VM_PORT = 12049` but IRIX sends NFS traffic to port `2049`, the
match arm for `NFS_VM_PORT` never fires. The NFS packets fall through to the
generic case, which maps them to `127.0.0.1:2049` — where nothing is listening.

**Portmapper interception (line 435)** — returns `NFS_VM_PORT` in portmapper
GETPORT replies for NFS:

```rust
match req_prog {
    RPC_PROG_NFS    => NFS_VM_PORT as u32,  // tells guest: NFS is on port 12049
    RPC_PROG_MOUNTD => MOUNTD_VM_PORT as u32,
    ...
}
```

IRIX does receive the portmapper reply saying NFS is on port 12049, but
**NFSv2 clients do not use portmapper to locate the NFS service**. Per RFC 1094
(NFSv2), the NFS port 2049 is a well-known fixed port. IRIX 5.3, like all
NFSv2 clients of that era, ignores the portmapper result for NFS and connects
directly to port 2049. The portmapper reply is therefore irrelevant for NFS
data traffic.

MOUNTD works correctly because IRIS *controls* what port IRIX uses: it
intercepts the portmapper GETPORT for MOUNTD and returns `MOUNTD_VM_PORT`
(11234). IRIX then connects to port 11234 — matching the constant — and the
remap fires correctly. The same approach cannot work for NFS because IRIX
ignores the portmapper result for NFS.

---

## Proposed Fix

**`src/net.rs`, line 39** — change `NFS_VM_PORT` to the actual NFSv2
well-known port:

```rust
- const NFS_VM_PORT: u16 = 12049;
+ const NFS_VM_PORT: u16 = 2049;
```

With this change:

1. Guest NFS traffic to `10.53.0.1:2049` matches the `NFS_VM_PORT` arm in
   `nfs_remap_dst()` and is correctly redirected to `127.0.0.1:nfs_host_port`.
2. `nfs_unmap_src()` correctly translates replies back from
   `localhost:nfs_host_port` to `gateway:2049` for the guest.
3. The portmapper reply now returns `2049` for NFS GETPORT queries — consistent
   with the actual well-known port, though this value continues to be ignored by
   NFSv2 clients.
4. No changes to `MOUNTD_VM_PORT`, unfsd configuration, or any other code are
   required.

---

## Impact

- NFS is completely non-functional with the default configuration and any
  NFSv2 client. IRIX 5.3 is the tested case; the same failure would affect
  any other NFSv2 guest that hardcodes port 2049.
- NFSv3 clients that do use portmapper to discover the NFS port would
  receive port 12049 from the portmapper interception and connect there
  directly — `NFS_VM_PORT = 12049` would match — so NFSv3 clients may be
  unaffected by this bug. NFSv2-only clients are broken.
- The symptom (`mount` hanging with no error or timeout) gives no indication
  of the actual cause, making the bug very hard to diagnose without the IRIS
  network monitor.

---

## Workaround (until fix is available)

Add `nfs_host_port = 2049` to the `[nfs]` section of `iris53.toml`:

```toml
[nfs]
shared_dir    = "shared"
unfsd         = "/path/to/unfsd"
nfs_host_port = 2049
```

This makes unfsd listen on `localhost:2049`, which happens to be where the
broken generic fallthrough in `nfs_remap_dst()` already sends NFS traffic.
It exploits the bug rather than fixing it, but requires no code changes.
