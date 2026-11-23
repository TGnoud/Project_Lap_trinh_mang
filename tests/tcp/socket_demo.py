"""Run the executables against each other over loopback TCP."""

import select
import socket
import subprocess
import sys
from contextlib import contextmanager


@contextmanager
def running_server(server_path, error=None):
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
            assert port > 0, line

            yield port
            output, errors = server.communicate(timeout=5)
            assert server.returncode == (0 if error is None else 1), errors
            assert output.strip() == "Client connected", output
            if error is not None:
                assert error in errors, errors
        finally:
            if server.poll() is None:
                server.kill()


def run_demo(server_path, cli_path):
    with running_server(server_path) as port:
        client = subprocess.run(
            [cli_path, "--port", str(port)], capture_output=True, text=True, timeout=5
        )
        assert client.returncode == 0, client.stderr
        assert client.stdout.splitlines() == [
            f"Connected to 127.0.0.1:{port}",
            'Echo: {"message":"Xin chào"}',
        ], client.stdout

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as bound_socket:
        bound_socket.bind(("127.0.0.1", 0))
        port = bound_socket.getsockname()[1]
        for executable, syscall in ((server_path, "bind"), (cli_path, "connect")):
            failed = subprocess.run(
                [executable, "--port", str(port)], capture_output=True, text=True, timeout=5
            )
            assert failed.returncode == 1, failed
            assert failed.stderr.startswith(f"{syscall}:"), failed.stderr


if __name__ == "__main__":
    run_demo(*sys.argv[1:])
