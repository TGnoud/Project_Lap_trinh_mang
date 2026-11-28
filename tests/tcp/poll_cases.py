"""TCP-02 exercises concurrent non-blocking sockets, slow clients, timeouts, and fd cleanup."""

import os
import select
import socket
import subprocess
import sys
import time
from contextlib import ExitStack

from framing_cases import encode, receive


def server_fd_count(pid):
    return len(os.listdir(f"/proc/{pid}/fd"))


def wait_for_fd_count(pid, expected, timeout=3.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if server_fd_count(pid) == expected:
            return
        time.sleep(0.02)
    assert server_fd_count(pid) == expected


def run_poll_cases(server_path):
    with subprocess.Popen(
        [server_path, "--port", "0"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    ) as server:
        try:
            ready, _, _ = select.select([server.stdout], [], [], 5)
            assert ready, "server did not announce its port"
            line = server.stdout.readline().strip()
            prefix = "Listening on 127.0.0.1:"
            assert line.startswith(prefix), line
            port = int(line[len(prefix):])
            baseline_fds = server_fd_count(server.pid)

            with ExitStack() as stack:
                # 1. Kết nối gửi frame dở để kiểm tra deadline 10 giây song song với các ca khác.
                stalled = stack.enter_context(
                    socket.create_connection(("127.0.0.1", port), timeout=15)
                )
                stalled.sendall(b"\x00\x00")

                # 2. Mở 20 kết nối đồng thời, gửi frame tách/gộp xen kẽ nhau.
                clients = [
                    stack.enter_context(
                        socket.create_connection(("127.0.0.1", port), timeout=5)
                    )
                    for _ in range(20)
                ]
                wait_for_fd_count(server.pid, baseline_fds + 21)

                for index, sock in enumerate(clients):
                    first = encode(f'{{"client":{index},"step":1}}'.encode())
                    second = encode(f'{{"client":{index},"step":2}}'.encode())
                    sock.sendall(first[:3])
                    sock.sendall(first[3:] + second)

                for index, sock in enumerate(clients):
                    assert receive(sock) == f'{{"client":{index},"step":1}}'.encode()
                    assert receive(sock) == f'{{"client":{index},"step":2}}'.encode()
                    sock.close()

                wait_for_fd_count(server.pid, baseline_fds + 1)

                # 3. Client chậm gây gửi dở/backpressure chạy cạnh các client bình thường.
                slow = stack.enter_context(socket.socket(socket.AF_INET, socket.SOCK_STREAM))
                slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
                slow.settimeout(5)
                slow.connect(("127.0.0.1", port))
                large_body = b"{}" + b" " * 59998
                large_wire = encode(large_body)
                for _ in range(3):
                    slow.sendall(large_wire)

                for fast_index in range(5):
                    with socket.create_connection(("127.0.0.1", port), timeout=5) as fast:
                        payload = f'{{"fast":{fast_index}}}'.encode()
                        fast.sendall(encode(payload))
                        assert receive(fast) == payload

                for _ in range(3):
                    assert receive(slow) == large_body
                slow.close()
                wait_for_fd_count(server.pid, baseline_fds + 1)

                # 4. Đợi kết nối gửi frame dở hết hạn 10 giây và xác nhận thu hồi hết fd.
                assert stalled.recv(1) == b""
                stalled.close()
                wait_for_fd_count(server.pid, baseline_fds)

            server.terminate()
            output, errors = server.communicate(timeout=5)
            assert server.returncode == 0, errors
            assert "Client connected" in output.splitlines(), output
            assert "Frame timeout" in errors, errors
        finally:
            if server.poll() is None:
                server.kill()


if __name__ == "__main__":
    run_poll_cases(sys.argv[1])
