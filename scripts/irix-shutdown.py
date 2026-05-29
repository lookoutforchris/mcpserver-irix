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
    sock.settimeout(timeout)
    buf = b''
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
            if pattern in buf:
                break
        except socket.timeout:
            break
    return buf

def negotiate(sock):
    # Round 1
    data = recv_until(sock, b'\xff', timeout=5)
    text, replies = process_iac(data)
    if replies:
        sock.sendall(replies)
    # Round 2 -- collect more IAC sequences and any non-IAC text
    collected = b''
    deadline = time.time() + 8
    while time.time() < deadline:
        sock.settimeout(2)
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            collected += chunk
            text2, replies2 = process_iac(chunk)
            if replies2:
                sock.sendall(replies2)
            # Stop when we see login prompt text
            plain, _ = process_iac(collected)
            if b'login:' in plain or b'Login:' in plain:
                break
        except socket.timeout:
            plain, _ = process_iac(collected)
            if b'login:' in plain or b'Login:' in plain:
                break
    return collected

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 2323))
sys.stdout.buffer.write(b'[*] Connected to port 2323\n')
sys.stdout.buffer.flush()

neg_data = negotiate(sock)
plain, _ = process_iac(neg_data)
sys.stdout.buffer.write(b'[*] After negotiate: ' + plain + b'\n')
sys.stdout.buffer.flush()

# Wait for login prompt
if b'login:' not in plain and b'Login:' not in plain:
    more = recv_until(sock, b'login:', timeout=10)
    neg_data += more
    plain2, _ = process_iac(more)
    sys.stdout.buffer.write(b'[*] More: ' + plain2 + b'\n')
    sys.stdout.buffer.flush()

sys.stdout.buffer.write(b'[*] Sending login: root\n')
sys.stdout.buffer.flush()
sock.sendall(b'root\n')

# Wait for shell prompt
reply = recv_until(sock, b'# ', timeout=15)
plain3, _ = process_iac(reply)
sys.stdout.buffer.write(b'[*] Login reply: ' + plain3 + b'\n')
sys.stdout.buffer.flush()

if b'#' not in plain3 and b'$' not in plain3:
    sys.stdout.buffer.write(b'[!] Did not get shell prompt, aborting\n')
    sys.stdout.buffer.flush()
    sock.close()
    sys.exit(1)

sys.stdout.buffer.write(b'[*] Sending shutdown command\n')
sys.stdout.buffer.flush()
sock.sendall(b'shutdown -y -i0 -g0\n')

# Collect output for up to 90 seconds
sys.stdout.buffer.write(b'[*] Waiting for IRIX to halt (up to 90s)...\n')
sys.stdout.buffer.flush()
deadline = time.time() + 90
buf = b''
while time.time() < deadline:
    sock.settimeout(3)
    try:
        chunk = sock.recv(4096)
        if not chunk:
            sys.stdout.buffer.write(b'[*] Connection closed (IRIX halted)\n')
            sys.stdout.buffer.flush()
            break
        buf += chunk
        plain4, _ = process_iac(chunk)
        sys.stdout.buffer.write(plain4)
        sys.stdout.buffer.flush()
        low = plain4.lower()
        if b'halted' in low or b'halt' in low or b'sync' in low or b'power off' in low:
            sys.stdout.buffer.write(b'\n[*] Halt message detected, waiting 5s for full halt\n')
            sys.stdout.buffer.flush()
            time.sleep(5)
            break
    except socket.timeout:
        continue

sys.stdout.buffer.write(b'\n[*] Shutdown sequence complete\n')
sys.stdout.buffer.flush()
sock.close()
