#!/usr/bin/env python3

import argparse
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def wait_for(log_path: Path, needle: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    last_text = ""
    while time.monotonic() < deadline:
        if log_path.exists():
            last_text = log_path.read_text(errors="replace")
            if needle in last_text:
                return last_text
        time.sleep(0.1)

    raise AssertionError(f"Timed out waiting for {needle!r} in {log_path}\n--- log ---\n{last_text}")


def start_peer(binary: Path, log_path: Path, *args: str, public_ip: str | None = None) -> subprocess.Popen:
    env = os.environ.copy()
    env["P2P_DISABLE_SIGNALING"] = "1"
    env["P2P_DISABLE_STUN"] = "1"
    if public_ip:
        env["P2P_PUBLIC_IP"] = public_ip

    log_file = log_path.open("w")
    try:
        return subprocess.Popen(
            [str(binary), *args],
            stdout=log_file,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
            env=env,
            text=True,
        )
    finally:
        log_file.close()


def stop_peer(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return

    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def assert_running(process: subprocess.Popen, name: str, log_path: Path) -> None:
    if process.poll() is not None:
        log_text = log_path.read_text(errors="replace") if log_path.exists() else ""
        raise AssertionError(f"{name} exited with {process.returncode}\n--- log ---\n{log_text}")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def test_handshake(binary: Path, temp_dir: Path) -> None:
    server_log = temp_dir / "handshake_server.log"
    client_log = temp_dir / "handshake_client.log"
    server_port = free_port()
    client_port = free_port()

    server = start_peer(binary, server_log, "--listen", str(server_port), "--id", "server", public_ip="127.0.0.1")
    client = None
    try:
        wait_for(server_log, f"Peer server listening on port {server_port}", 5)
        client = start_peer(
            binary,
            client_log,
            "--listen",
            str(client_port),
            "--id",
            "client",
            "--connect",
            "127.0.0.1",
            str(server_port),
        )

        wait_for(client_log, f"Connected to 127.0.0.1:{server_port}", 8)
        wait_for(client_log, "[client] got: PONG from server", 8)
        wait_for(server_log, "[server] got: HELLO client", 8)
        wait_for(client_log, "[client] got: PING", 20)
        wait_for(server_log, "[server] got: PONG", 20)

        assert_running(server, "server", server_log)
        assert_running(client, "client", client_log)
    finally:
        if client:
            stop_peer(client)
        stop_peer(server)


def test_reconnect(binary: Path, temp_dir: Path) -> None:
    server_log_1 = temp_dir / "reconnect_server_1.log"
    server_log_2 = temp_dir / "reconnect_server_2.log"
    client_log = temp_dir / "reconnect_client.log"
    server_port = free_port()
    client_port = free_port()

    server = start_peer(binary, server_log_1, "--listen", str(server_port), "--id", "server", public_ip="127.0.0.1")
    client = None
    restarted_server = None
    try:
        wait_for(server_log_1, f"Peer server listening on port {server_port}", 5)
        client = start_peer(
            binary,
            client_log,
            "--listen",
            str(client_port),
            "--id",
            "client",
            "--connect",
            "127.0.0.1",
            str(server_port),
        )
        wait_for(client_log, f"Connected to 127.0.0.1:{server_port}", 8)

        stop_peer(server)
        wait_for(client_log, f"[client] outbound peer disconnected: 127.0.0.1:{server_port}", 25)
        wait_for(client_log, "Connect error: Connection refused", 25)

        restarted_server = start_peer(
            binary,
            server_log_2,
            "--listen",
            str(server_port),
            "--id",
            "server",
            public_ip="127.0.0.1",
        )
        wait_for(server_log_2, f"Peer server listening on port {server_port}", 5)
        wait_for(client_log, f"Connected to 127.0.0.1:{server_port}", 25)
        wait_for(client_log, "[client] got: PONG from server", 25)

        assert_running(client, "client", client_log)
        assert_running(restarted_server, "restarted server", server_log_2)
    finally:
        if client:
            stop_peer(client)
        if restarted_server:
            stop_peer(restarted_server)
        stop_peer(server)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()

    if not args.binary.exists():
        raise FileNotFoundError(args.binary)

    with tempfile.TemporaryDirectory(prefix="p2p-e2e-") as temp:
        temp_dir = Path(temp)
        test_handshake(args.binary, temp_dir)
        test_reconnect(args.binary, temp_dir)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"p2p_e2e failed: {exc}", file=sys.stderr)
        raise
