#!/usr/bin/env python3
"""
NFS port proxy: bridges IRIS's NAT on port 2049 to unfsd on port 12049.

IRIX 5.3 hardcodes NFS port 2049 (NFSv2 era convention) instead of using
portmapper. IRIS forwards 10.53.0.1:2049 → 127.0.0.1:2049, but unfsd is
on port 12049. This proxy sits on 127.0.0.1:2049 and transparently forwards
all UDP and TCP traffic to 127.0.0.1:12049.

Usage: python3 nfs-proxy.py
"""

import socket
import threading
import sys
import time

LISTEN_HOST   = "127.0.0.1"
LISTEN_PORT   = 2049
BACKEND_HOST  = "127.0.0.1"
BACKEND_PORT  = 12049

# -------------------------------------------------------------------------
# UDP proxy
# -------------------------------------------------------------------------

def udp_proxy():
    """Receive UDP packets on port 2049 and forward to port 12049."""
    front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    front.bind((LISTEN_HOST, LISTEN_PORT))
    front.settimeout(1.0)

    # One backend socket per upstream source address, kept alive briefly.
    # Maps (src_ip, src_port) → (backend_socket, last_used)
    backends = {}
    IDLE_TTL = 30  # seconds

    def cleanup():
        while True:
            now = time.monotonic()
            for key in list(backends.keys()):
                sock, ts = backends[key]
                if now - ts > IDLE_TTL:
                    sock.close()
                    del backends[key]
            time.sleep(10)

    threading.Thread(target=cleanup, daemon=True).start()

    print(f"[udp] proxy 127.0.0.1:{LISTEN_PORT} -> 127.0.0.1:{BACKEND_PORT}", flush=True)

    while True:
        try:
            data, src = front.recvfrom(65536)
        except socket.timeout:
            continue

        # Lazily create a backend socket for this source.
        if src not in backends:
            back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            back.settimeout(10.0)
            backends[src] = (back, time.monotonic())
        else:
            back, _ = backends[src]
            backends[src] = (back, time.monotonic())

        # Forward to unfsd.
        try:
            back.sendto(data, (BACKEND_HOST, BACKEND_PORT))
            resp, _ = back.recvfrom(65536)
            front.sendto(resp, src)
        except socket.timeout:
            pass  # unfsd didn't reply; client will retry via its own timeout


# -------------------------------------------------------------------------
# TCP proxy
# -------------------------------------------------------------------------

def tcp_relay(client_sock, client_addr):
    """Relay a single TCP connection between client and unfsd backend."""
    try:
        back = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        back.connect((BACKEND_HOST, BACKEND_PORT))
    except OSError as e:
        print(f"[tcp] backend connect failed: {e}", flush=True)
        client_sock.close()
        return

    def pipe(src, dst, tag):
        try:
            while True:
                d = src.recv(65536)
                if not d:
                    break
                dst.sendall(d)
        except OSError:
            pass
        finally:
            try: src.shutdown(socket.SHUT_RD)
            except: pass
            try: dst.shutdown(socket.SHUT_WR)
            except: pass

    t1 = threading.Thread(target=pipe, args=(client_sock, back, "c→b"), daemon=True)
    t2 = threading.Thread(target=pipe, args=(back, client_sock, "b→c"), daemon=True)
    t1.start(); t2.start()
    t1.join(); t2.join()
    client_sock.close()
    back.close()


def tcp_proxy():
    """Accept TCP connections on port 2049 and relay to port 12049."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((LISTEN_HOST, LISTEN_PORT))
    srv.listen(32)
    print(f"[tcp] proxy 127.0.0.1:{LISTEN_PORT} -> 127.0.0.1:{BACKEND_PORT}", flush=True)
    while True:
        client, addr = srv.accept()
        threading.Thread(target=tcp_relay, args=(client, addr), daemon=True).start()


# -------------------------------------------------------------------------
# Main
# -------------------------------------------------------------------------

if __name__ == "__main__":
    # TCP and UDP share port 2049 — each needs its own socket / thread.
    t_udp = threading.Thread(target=udp_proxy, daemon=False)
    t_tcp = threading.Thread(target=tcp_proxy, daemon=False)
    t_udp.start()
    t_tcp.start()
    print(f"NFS proxy running on {LISTEN_HOST}:{LISTEN_PORT} -> {BACKEND_HOST}:{BACKEND_PORT}")
    print("Press Ctrl-C to stop.")
    try:
        t_udp.join()
        t_tcp.join()
    except KeyboardInterrupt:
        print("\nStopped.")
        sys.exit(0)
