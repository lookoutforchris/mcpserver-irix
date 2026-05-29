import socket, time, sys

def process_iac(buf):
    text = b''
    replies = b''
    i = 0
    while i < len(buf):
        if buf[i] == 0xff and i + 2 < len(buf):
            cmd = buf[i+1]
            opt = buf[i+2]
            if cmd == 0xfd:
                replies += bytes([0xff, 0xfc, opt])
            elif cmd == 0xfb:
                replies += bytes([0xff, 0xfe, opt])
            i += 3
        elif buf[i] == 0xff and i + 1 < len(buf):
            i += 2
        else:
            text += bytes([buf[i]])
            i += 1
    return text, replies

def recv_until(sock, pattern, timeout=15):
    buf = b''
    deadline = time.time() + timeout
    while time.time() < deadline:
        sock.settimeout(min(2, deadline - time.time()))
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
            plain, _ = process_iac(buf)
            if pattern in plain:
                break
        except socket.timeout:
            pass
    return buf

def negotiate(sock):
    collected = b''
    deadline = time.time() + 10
    while time.time() < deadline:
        sock.settimeout(2)
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            collected += chunk
            _, replies = process_iac(chunk)
            if replies:
                sock.sendall(replies)
            plain, _ = process_iac(collected)
            if b'login:' in plain or b'Login:' in plain:
                break
        except socket.timeout:
            plain, _ = process_iac(collected)
            if b'login:' in plain or b'Login:' in plain:
                break
    return collected

def run_cmd(sock, cmd, timeout=30):
    sock.sendall(cmd.encode() + b'\n')
    raw = recv_until(sock, b'# ', timeout)
    plain, _ = process_iac(raw)
    return plain

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 2323))
sys.stdout.buffer.write(b'[*] Connected\n'); sys.stdout.buffer.flush()

neg = negotiate(sock)
plain, _ = process_iac(neg)
sys.stdout.buffer.write(b'[*] Negotiated: ' + plain + b'\n'); sys.stdout.buffer.flush()

sock.sendall(b'root\n')
raw = recv_until(sock, b'# ', timeout=15)
plain, _ = process_iac(raw)
sys.stdout.buffer.write(b'[*] Login: ' + plain + b'\n'); sys.stdout.buffer.flush()

if b'#' not in plain:
    sys.stdout.buffer.write(b'[!] No shell prompt\n'); sys.stdout.buffer.flush()
    sys.exit(1)

sys.stdout.buffer.write(b'[*] mkdir /mnt/host\n'); sys.stdout.buffer.flush()
out = run_cmd(sock, 'mkdir -p /mnt/host')
sys.stdout.buffer.write(out + b'\n'); sys.stdout.buffer.flush()

sys.stdout.buffer.write(b'[*] Mounting NFS...\n'); sys.stdout.buffer.flush()
out = run_cmd(sock, 'mount -o rsize=8192,wsize=8192 10.53.0.1:/c/dev/tools/iris/shared /mnt/host', timeout=30)
sys.stdout.buffer.write(out + b'\n'); sys.stdout.buffer.flush()

sys.stdout.buffer.write(b'[*] ls /mnt/host\n'); sys.stdout.buffer.flush()
out = run_cmd(sock, 'ls -la /mnt/host', timeout=15)
sys.stdout.buffer.write(out + b'\n'); sys.stdout.buffer.flush()

sock.close()
