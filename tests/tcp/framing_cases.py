"""TCP-01 and CT-01 exercise the C programs on loopback TCP."""

import socket
import struct
import subprocess
import sys

from socket_demo import running_server


def encode(body):
    return struct.pack("!I", len(body)) + body


def read_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        assert chunk, f"EOF after {len(data)} of {size} bytes"
        data.extend(chunk)
    return bytes(data)


def receive(sock):
    size, = struct.unpack("!I", read_exact(sock, 4))
    assert 0 < size <= 65536, size
    return read_exact(sock, size)


def tcp_cases(server_path, cli_path):
    body = '{"x":"café"}'.encode()
    wire = encode(body)
    with running_server(server_path) as port:
        with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
            for chunk in (wire[:1], wire[1:3], wire[3:-3], wire[-3:]):
                sock.sendall(chunk)
            assert receive(sock) == body

            second = b'{"x":2}'
            largest = b"{}" + b" " * 65534
            sock.sendall(encode(second) + encode(largest))
            assert receive(sock) == second
            assert receive(sock) == largest
            sock.shutdown(socket.SHUT_WR)
            assert sock.recv(1) == b""

    bad_cases = [
        (b"\0\0", "Incomplete frame at EOF"),
        (wire[:-1], "Incomplete frame at EOF"),
        (struct.pack("!I", 0), "Invalid frame"),
        (struct.pack("!I", 65537), "Invalid frame"),
        (encode(b'{"x":}'), "Invalid frame"),
        (encode(b'{"x":"\xc3("}'), "Invalid frame"),
    ]
    for invalid, error in bad_cases:
        with running_server(server_path, error) as port:
            with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
                sock.sendall(invalid)
                sock.shutdown(socket.SHUT_WR)
                assert sock.recv(1) == b"", invalid

    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]
        with subprocess.Popen(
            [cli_path, "--port", str(port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True,
        ) as client:
            try:
                sock, _ = listener.accept()
                with sock:
                    sock.settimeout(5)
                    sample = '{"message":"Xin chào"}'.encode()
                    assert receive(sock) == sample
                    sock.sendall(encode(sample.replace(b"Xin", b"Hoi")))
                _, errors = client.communicate(timeout=5)
                assert client.returncode == 1, errors
                assert "Missing or mismatched echo frame" in errors, errors
            finally:
                if client.poll() is None:
                    client.kill()


def contract_cases(server_path):
    golden = b'\x00\x00\x00\x07{"x":1}'
    assert encode(b'{"x":1}') == golden
    with running_server(server_path) as port:
        with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
            sock.sendall(golden)
            assert read_exact(sock, len(golden)) == golden


if __name__ == "__main__":
    if sys.argv[1] == "tcp":
        tcp_cases(*sys.argv[2:])
    elif sys.argv[1] == "contract":
        contract_cases(*sys.argv[2:])
    else:
        raise SystemExit(f"Unknown case {sys.argv[1]}")
