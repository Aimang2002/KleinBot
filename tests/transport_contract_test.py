#!/usr/bin/env python3

import argparse
import base64
import hashlib
import hmac
import http.client
import json
import os
import socket
import struct
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


TOKEN = "contract-token"
EVENT = {
    "post_type": "message",
    "message_type": "private",
    "user_id": 7,
    "raw_message": "contract-inbound",
    "message": [{"type": "text", "data": {"text": "contract-inbound"}}],
    "message_id": 9,
    "time": 10,
}


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def receive_exact(sock, length):
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise RuntimeError("socket closed unexpectedly")
        data.extend(chunk)
    return bytes(data)


def receive_headers(sock):
    data = bytearray()
    while b"\r\n\r\n" not in data:
        data.extend(receive_exact(sock, 1))
    return data.decode("iso-8859-1")


def websocket_frame(payload, masked):
    payload = payload.encode("utf-8")
    header = bytearray([0x81])
    mask_flag = 0x80 if masked else 0
    if len(payload) < 126:
        header.append(mask_flag | len(payload))
    elif len(payload) <= 0xFFFF:
        header.append(mask_flag | 126)
        header.extend(struct.pack("!H", len(payload)))
    else:
        header.append(mask_flag | 127)
        header.extend(struct.pack("!Q", len(payload)))

    if not masked:
        return bytes(header) + payload

    mask = os.urandom(4)
    encoded = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    return bytes(header) + mask + encoded


def receive_websocket_frame(sock):
    first, second = receive_exact(sock, 2)
    opcode = first & 0x0F
    masked = bool(second & 0x80)
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", receive_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", receive_exact(sock, 8))[0]
    mask = receive_exact(sock, 4) if masked else b""
    payload = receive_exact(sock, length)
    if masked:
        payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    if opcode != 1:
        raise AssertionError(f"expected text frame, got opcode {opcode}")
    return payload.decode("utf-8")


def assert_action(action):
    assert action["action"] == "send_private_msg", action
    assert action["params"]["user_id"] == 42, action
    segment = action["params"]["message"][0]
    assert segment == {"type": "text", "data": {"text": "contract-outbound"}}, action


def assert_driver_result(process, mode):
    stdout, stderr = process.communicate(timeout=10)
    if process.returncode != 0:
        raise AssertionError(
            f"driver failed for {mode}: code={process.returncode}\nstdout={stdout}\nstderr={stderr}"
        )
    marker = next((line for line in stdout.splitlines() if line.startswith("CONTRACT_EVENT ")), None)
    if marker is None:
        raise AssertionError(f"missing contract marker for {mode}: {stdout}")
    event = json.loads(marker.removeprefix("CONTRACT_EVENT "))
    assert event == {
        "mode": mode,
        "user_id": 7,
        "message_type": "private",
        "plain_text": "contract-inbound",
    }, event


def run_forward(driver):
    port = free_port()
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", port))
        listener.listen(1)
        listener.settimeout(10)
        process = subprocess.Popen(
            [driver, "forward_websocket", str(port)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        connection, _ = listener.accept()

    with connection:
        connection.settimeout(10)
        request = receive_headers(connection)
        assert request.startswith("GET /onebot HTTP/1.1"), request
        assert "Authorization: Bearer contract-token\r\n" in request, request
        key_line = next(line for line in request.split("\r\n") if line.lower().startswith("sec-websocket-key:"))
        key = key_line.split(":", 1)[1].strip()
        accept = base64.b64encode(
            hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
        ).decode()
        connection.sendall(
            (
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                f"Sec-WebSocket-Accept: {accept}\r\n\r\n"
            ).encode()
        )
        assert_action(json.loads(receive_websocket_frame(connection)))
        connection.sendall(websocket_frame(json.dumps(EVENT), masked=False))
        assert_driver_result(process, "forward_websocket")


def connect_with_retry(port):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=1)
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("reverse websocket server did not start")


def send_upgrade_request(connection, port, token):
    key = base64.b64encode(os.urandom(16)).decode()
    connection.sendall(
        (
            "GET /onebot HTTP/1.1\r\n"
            f"Host: 127.0.0.1:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            f"Authorization: Bearer {token}\r\n\r\n"
        ).encode()
    )


def reverse_handshake(port, token):
    connection = connect_with_retry(port)
    connection.settimeout(10)
    send_upgrade_request(connection, port, token)
    return connection, receive_headers(connection)


def assert_rejected(port):
    connection, response = reverse_handshake(port, "wrong-token")
    with connection:
        assert response.startswith("HTTP/1.1 401"), response


# 监听器必须常驻：单个连接失败只结束这一个连接，不重建 acceptor、不停摆。
# 监听器若随每个连接重建，已排进旧 acceptor 积压队列的连接会在它析构时被内核 RST，
# 握手前出错的连接还会让端口停听 10 秒。
def assert_listener_survives_failed_connections(port):
    # 被拒后立即重连，连续多轮
    for _ in range(20):
        assert_rejected(port)

    # 让竞争必现：下一个连接在上一个拿到 401 之前就完成 TCP 握手、排进积压队列
    first = connect_with_retry(port)
    queued = connect_with_retry(port)
    with first, queued:
        first.settimeout(10)
        queued.settimeout(10)
        send_upgrade_request(first, port, "wrong-token")
        response = receive_headers(first)
        assert response.startswith("HTTP/1.1 401"), response
        send_upgrade_request(queued, port, "wrong-token")
        response = receive_headers(queued)
        assert response.startswith("HTTP/1.1 401"), response

    # 连上就断开：下一个连接必须马上被服务
    silent = connect_with_retry(port)
    silent.close()
    started = time.monotonic()
    assert_rejected(port)
    elapsed = time.monotonic() - started
    assert elapsed < 2, f"listener stalled {elapsed:.2f}s after a silent disconnect"

    # 非 HTTP 数据：服务端应直接放弃这个连接（读到 EOF 或被重置），不回任何内容
    garbage = connect_with_retry(port)
    with garbage:
        garbage.settimeout(2)
        garbage.sendall(b"\x16\x03\x01 not an http request\r\n\r\n")
        try:
            leftover = garbage.recv(1)
        except ConnectionResetError:
            leftover = b""
        assert leftover == b"", leftover


def run_reverse(driver):
    port = free_port()
    process = subprocess.Popen(
        [driver, "reverse_websocket", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        # 驱动处理完一个事件就退出，监听器的检查必须放在成功会话之前
        assert_listener_survives_failed_connections(port)

        # 时间自己量：connect_with_retry 会重试 10 秒，掩盖端口停听
        started = time.monotonic()
        connection, response = reverse_handshake(port, TOKEN)
        elapsed = time.monotonic() - started
        with connection:
            assert response.startswith("HTTP/1.1 101"), response
            assert elapsed < 2, f"listener stalled {elapsed:.2f}s after a non-HTTP connection"
            assert_action(json.loads(receive_websocket_frame(connection)))
            connection.sendall(websocket_frame(json.dumps(EVENT), masked=True))
            assert_driver_result(process, "reverse_websocket")
    finally:
        # 断言失败时驱动仍在等事件，收掉它以免残留到下一轮
        if process.poll() is None:
            process.kill()
            process.communicate()


class ApiHandler(BaseHTTPRequestHandler):
    action = None
    event = threading.Event()

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length))
        assert self.headers.get("Authorization") == "Bearer contract-token"
        ApiHandler.action = {"action": self.path.lstrip("/"), "params": body}
        ApiHandler.event.set()
        response = json.dumps({"status": "ok", "retcode": 0}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def log_message(self, *_):
        pass


def post_event_with_retry(port, secret=TOKEN):
    payload = json.dumps(EVENT).encode()
    signature = "sha1=" + hmac.new(secret.encode(), payload, hashlib.sha1).hexdigest()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
            connection.request(
                "POST",
                "/onebot/events",
                body=payload,
                headers={
                    "Content-Type": "application/json",
                    "X-Signature": signature,
                },
            )
            response = connection.getresponse()
            response.read()
            connection.close()
            return response.status
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("HTTP event server did not start")


def run_http(driver):
    api_port = free_port()
    event_port = free_port()
    ApiHandler.action = None
    ApiHandler.event.clear()
    server = ThreadingHTTPServer(("127.0.0.1", api_port), ApiHandler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    try:
        process = subprocess.Popen(
            [driver, "http", str(api_port), str(event_port)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        assert post_event_with_retry(event_port, "wrong-token") == 401
        assert post_event_with_retry(event_port) == 204
        assert ApiHandler.event.wait(timeout=10), "HTTP API action was not received"
        assert_action(ApiHandler.action)
        assert_driver_result(process, "http")
    finally:
        server.shutdown()
        server.server_close()
        server_thread.join(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", required=True)
    args = parser.parse_args()

    run_forward(args.driver)
    run_reverse(args.driver)
    run_http(args.driver)
    print("transport contracts passed")


if __name__ == "__main__":
    main()
