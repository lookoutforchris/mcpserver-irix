#!/usr/bin/env python3
"""
Direct RPC test for unfsd MOUNT v1 and NFS v2.
Bypasses IRIS NAT - connects directly to localhost:11234 (mountd) and localhost:12049 (NFS).
"""

import socket
import struct
import sys
import time

MOUNT_PROG = 100005
MOUNT_VERS1 = 1
MOUNT_PROC_NULL = 0
MOUNT_PROC_MNT  = 1

NFS_PROG  = 100003
NFS_VERS2 = 2
NFS_PROC_NULL    = 0
NFS_PROC_GETATTR = 1

NFS2_FHSIZE = 32


def build_rpc_call(xid, prog, vers, proc, args=b""):
    """Build an RPC CALL message (no auth)."""
    return struct.pack(">IIIIIIIIII",
        xid,          # XID
        0,            # msg_type: CALL
        2,            # rpc_version: 2
        prog,         # program
        vers,         # version
        proc,         # procedure
        0, 0,         # credentials: AUTH_NULL, len=0
        0, 0,         # verifier:    AUTH_NULL, len=0
    ) + args


def parse_rpc_reply(data, xid_expected):
    """Parse RPC REPLY header. Returns (accept_stat, payload_offset) or raises."""
    if len(data) < 24:
        raise ValueError(f"Reply too short: {len(data)} bytes")
    (rxid, mtype, reply_stat, vflavor, vlen) = struct.unpack_from(">IIIII", data, 0)
    if rxid != xid_expected:
        raise ValueError(f"XID mismatch: got {rxid}, expected {xid_expected}")
    if mtype != 1:
        raise ValueError(f"Not a REPLY (msg_type={mtype})")
    if reply_stat != 0:
        raise ValueError(f"MSG_DENIED (reply_stat={reply_stat})")
    offset = 20 + vlen  # skip over verifier opaque data
    accept_stat = struct.unpack_from(">I", data, offset)[0]
    return accept_stat, offset + 4


def xdr_string(s):
    """Encode a string as XDR: 4-byte length + padded bytes."""
    b = s.encode("utf-8")
    n = len(b)
    pad = (4 - n % 4) % 4
    return struct.pack(">I", n) + b + b"\x00" * pad


def rpc_call_udp(host, port, prog, vers, proc, args=b"", timeout=5):
    """Send a single-packet RPC call over UDP. Returns reply bytes or None."""
    xid = int(time.time()) & 0xFFFFFFFF
    pkt = build_rpc_call(xid, prog, vers, proc, args)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        sock.sendto(pkt, (host, port))
        data, _ = sock.recvfrom(65536)
        return data, xid
    except socket.timeout:
        return None, xid
    finally:
        sock.close()


def test_mount_null(host, port):
    print(f"\n--- MOUNT v1 NULL ({host}:{port}/UDP) ---")
    data, xid = rpc_call_udp(host, port, MOUNT_PROG, MOUNT_VERS1, MOUNT_PROC_NULL)
    if data is None:
        print("  TIMEOUT - no reply from unfsd")
        return False
    accept_stat, _ = parse_rpc_reply(data, xid)
    if accept_stat == 0:
        print("  OK - unfsd replied to MOUNT v1 NULL")
        return True
    print(f"  ACCEPT_STAT={accept_stat} (unexpected)")
    return False


def test_mount_mnt(host, port, path):
    print(f"\n--- MOUNT v1 MNT '{path}' ({host}:{port}/UDP) ---")
    args = xdr_string(path)
    data, xid = rpc_call_udp(host, port, MOUNT_PROG, MOUNT_VERS1, MOUNT_PROC_MNT, args)
    if data is None:
        print("  TIMEOUT - no reply from unfsd")
        return None
    accept_stat, offset = parse_rpc_reply(data, xid)
    if accept_stat != 0:
        print(f"  ACCEPT_STAT={accept_stat} (RPC-level error)")
        return None
    fhs_status = struct.unpack_from(">I", data, offset)[0]
    if fhs_status != 0:
        print(f"  fhstatus={fhs_status} (mount error - {mount_err(fhs_status)})")
        return None
    fh = data[offset + 4 : offset + 4 + NFS2_FHSIZE]
    if len(fh) < NFS2_FHSIZE:
        print(f"  Short file handle: got {len(fh)} bytes, expected {NFS2_FHSIZE}")
        return None
    print(f"  OK - got {NFS2_FHSIZE}-byte file handle: {fh.hex()}")
    return fh


def test_nfs2_null(host, port):
    print(f"\n--- NFS v2 NULL ({host}:{port}/UDP) ---")
    data, xid = rpc_call_udp(host, port, NFS_PROG, NFS_VERS2, NFS_PROC_NULL)
    if data is None:
        print("  TIMEOUT - no reply from unfsd")
        return False
    accept_stat, _ = parse_rpc_reply(data, xid)
    if accept_stat == 0:
        print("  OK - unfsd replied to NFS v2 NULL")
        return True
    print(f"  ACCEPT_STAT={accept_stat}")
    return False


def test_nfs2_getattr(host, port, fh):
    print(f"\n--- NFS v2 GETATTR ({host}:{port}/UDP) ---")
    assert len(fh) == NFS2_FHSIZE
    data, xid = rpc_call_udp(host, port, NFS_PROG, NFS_VERS2, NFS_PROC_GETATTR, fh)
    if data is None:
        print("  TIMEOUT - no reply from unfsd")
        return False
    accept_stat, offset = parse_rpc_reply(data, xid)
    if accept_stat != 0:
        print(f"  ACCEPT_STAT={accept_stat}")
        # decode rpc error
        if accept_stat == 1:
            print("  PROG_UNAVAIL - NFS v2 program not registered!")
        elif accept_stat == 2:
            print("  PROG_MISMATCH - version mismatch")
        elif accept_stat == 3:
            print("  PROC_UNAVAIL")
        elif accept_stat == 4:
            print("  GARBAGE_ARGS")
        return False
    nfs2stat = struct.unpack_from(">I", data, offset)[0]
    if nfs2stat == 0:
        print("  OK - NFS v2 GETATTR succeeded, attributes returned")
    else:
        print(f"  NFS status={nfs2stat} (NFS-level error)")
    return nfs2stat == 0


def mount_err(code):
    errs = {1: "EPERM", 2: "ENOENT", 5: "EIO", 13: "EACCES", 20: "ENOTDIR", 22: "EINVAL"}
    return errs.get(code, f"code {code}")


if __name__ == "__main__":
    host = "127.0.0.1"
    mount_port = 11234
    nfs_port   = 12049
    export_path = "/c/dev/tools/iris/shared"

    print(f"Testing unfsd directly (bypassing IRIS NAT)")
    print(f"  mountd: {host}:{mount_port}/UDP")
    print(f"  NFS:    {host}:{nfs_port}/UDP")
    print(f"  export: {export_path}")

    # Test 1: MOUNT v1 NULL
    if not test_mount_null(host, mount_port):
        print("\nFAIL: unfsd is not responding to MOUNT v1 NULL")
        sys.exit(1)

    # Test 2: MOUNT v1 MNT
    fh = test_mount_mnt(host, mount_port, export_path)
    if fh is None:
        print("\nFAIL: MOUNT v1 MNT failed")
        sys.exit(1)

    # Test 3: NFS v2 NULL
    if not test_nfs2_null(host, nfs_port):
        print("\nFAIL: unfsd is not responding to NFS v2 NULL")
        sys.exit(1)

    # Test 4: NFS v2 GETATTR on root
    if not test_nfs2_getattr(host, nfs_port, fh):
        print("\nFAIL: NFS v2 GETATTR failed")
        sys.exit(1)

    print("\n\nAll tests PASSED - unfsd NFSv2 is working correctly.")
    print("If mount from IRIX still hangs, the issue is in IRIS's portmapper/NAT routing.")
