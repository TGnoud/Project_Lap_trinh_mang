"""TCP-02 uses real loopback clients and the production deadlines."""

import select
import socket
import sys
import time
from contextlib import ExitStack
from pathlib import Path

from framing_cases import encode, receive
from socket_demo import running_server


MAX_CLIENTS = 256
FRAME_SIZE = 65536 + 4


def fd_count(server):
    return len(list(Path(f"/proc/{server.pid}/fd").iterdir()))


def wait_for_fds(server, expected, timeout):
    deadline = time.monotonic() + timeout
    while fd_count(server) != expected:
        assert server.poll() is None
        assert time.monotonic() < deadline, (expected, fd_count(server))
        time.sleep(0.02)


def echo(port):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
        body = b'{"fast":true}'
        client.sendall(encode(body))
        client.shutdown(socket.SHUT_WR)
        assert receive(client) == body
        assert client.recv(1) == b""


def rx_cases(server_path):
    with running_server(server_path, "RX frame timeout") as (port, server):
        baseline = fd_count(server)
        wire = encode(b'{"slow":true}')
        with (
            socket.create_connection(("127.0.0.1", port), timeout=13) as header,
            socket.create_connection(("127.0.0.1", port), timeout=13) as body,
        ):
            started = time.monotonic()
            header.sendall(wire[:2])
            body.sendall(wire[:-1])
            wait_for_fds(server, baseline + 2, 3)
            echo(port)
            assert time.monotonic() - started < 10
            assert header.recv(1) == b""
            assert body.recv(1) == b""
            assert time.monotonic() - started >= 9.5
        wait_for_fds(server, baseline, 3)
        echo(port)
        wait_for_fds(server, baseline, 3)


def slow_socket(port):
    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        client.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        client.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)
        client.settimeout(3)
        client.connect(("127.0.0.1", port))
        client.setblocking(False)
        return client
    except OSError:
        client.close()
        raise


def fill_without_reading(client, wire):
    deadline = time.monotonic() + 5
    sent = 0
    while sent < len(wire):
        assert time.monotonic() < deadline, "TCP window did not stall"
        _, writable, _ = select.select([], [client], [], 0.4)
        if not writable:
            assert sent > 2 * FRAME_SIZE, sent
            return sent
        sent += client.send(wire[sent:])
    raise AssertionError("TCP window absorbed all test data")


def drain_in_order(client, wire, sent):
    total = ((sent + FRAME_SIZE - 1) // FRAME_SIZE) * FRAME_SIZE
    deadline = time.monotonic() + 10
    received = 0
    while received < total:
        remaining = deadline - time.monotonic()
        assert remaining > 0, (received, total)
        readers, writers, _ = select.select(
            [client], [client] if sent < total else [], [], remaining
        )
        if writers:
            sent += client.send(wire[sent:total])
        if readers:
            data = client.recv(min(65536, total - received))
            assert data and data == wire[received : received + len(data)], received
            received += len(data)
    assert sent == total


def tx_cases(server_path):
    # Mỗi frame có một số thứ tự để phát hiện echo mất, lặp hoặc đảo vị trí.
    wire = b"".join(encode(f'{{"seq":{i}}}'.encode().ljust(65536)) for i in range(128))
    with running_server(server_path, "TX stalled") as (port, server):
        baseline = fd_count(server)
        with slow_socket(port) as recovering:
            sent = fill_without_reading(recovering, wire)
            echo(port)
            drain_in_order(recovering, wire, sent)
        wait_for_fds(server, baseline, 3)

        with slow_socket(port) as stalled:
            started = time.monotonic()
            fill_without_reading(stalled, wire)
            echo(port)
            wait_for_fds(server, baseline, 19)
            assert time.monotonic() - started >= 14.5
        echo(port)
        wait_for_fds(server, baseline, 3)


def capacity_case(server_path):
    with ExitStack() as sockets:
        with running_server(server_path) as (port, server):
            baseline = fd_count(server)
            body = b'{"active":true}'
            live = []
            for _ in range(MAX_CLIENTS):
                client = sockets.enter_context(
                    socket.create_connection(("127.0.0.1", port), timeout=3)
                )
                client.sendall(encode(body))
                assert receive(client) == body
                live.append(client)
            wait_for_fds(server, baseline + MAX_CLIENTS, 3)
            with socket.create_connection(("127.0.0.1", port), timeout=3) as rejected:
                assert rejected.recv(1) == b""
            live[0].close()
            wait_for_fds(server, baseline + MAX_CLIENTS - 1, 3)
            echo(port)
            # Các kết nối còn lại vẫn mở khi fixture gửi SIGTERM.


if __name__ == "__main__":
    rx_cases(sys.argv[1])
    tx_cases(sys.argv[1])
    capacity_case(sys.argv[1])
