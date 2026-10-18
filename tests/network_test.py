"""Development-only HTTP tests. Standard Python, compiled client_test.exe required."""
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path
import json, subprocess, sys, threading, time

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build/client_test.exe"
BODY = json.dumps({"live": {"state": "generating", "tok_s": 62.5}, "hardware": {"gpu_util": 80}, "engine": {"model": "test"}}).encode()
results = []
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *args): pass
    def handle(self):
        try: super().handle()
        except (ConnectionResetError, BrokenPipeError, OSError): pass
    def do_GET(self):
        server = self.server
        server.requests.append((time.monotonic(), self.path))
        mode = server.mode
        count = len(server.requests)
        status, body = 200, BODY
        if mode == "transition" and count > 1: status = 503
        if mode == "recovery" and count == 1: status = 503
        if mode == "auth": status = 401
        if mode == "forbidden": status = 403
        if mode == "http": status = 503
        if mode == "redirect": status = 302
        if mode == "format": body = b"{"
        if mode == "schema": body = b"{}"
        if mode == "empty": body = b""
        if mode == "large": body = b"x" * (256 * 1024 + 1)
        try:
            if mode == "timeout": time.sleep(3.2)
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            if mode == "redirect": self.send_header("Location", "/must-not-follow")
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, OSError): pass

def case(mode, expected, duration=1.25, hidden=False):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.mode, server.requests = mode, []
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        args = [str(EXE), str(server.server_port), str(duration), expected]
        if hidden: args.append("hidden")
        proc = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", timeout=duration+6)
        output = json.loads(proc.stdout)
        output.update(case=mode + ("-hidden" if hidden else ""), requests=len(server.requests))
        assert proc.returncode == 0 and output["pass"], output
        assert all(path == "/metrics" for _, path in server.requests), "redirect/path violation"
        if mode == "transition" or mode == "recovery": assert len(server.requests) >= 2
        if hidden:
            assert 2 <= len(server.requests) <= 3, server.requests
            gaps = [b[0]-a[0] for a,b in zip(server.requests, server.requests[1:])]
            assert all(gap >= 4.8 for gap in gaps), gaps
        results.append(output)
        print(json.dumps(output, ensure_ascii=False), flush=True)
    finally:
        server.shutdown(); server.server_close(); thread.join()

if __name__ == "__main__":
    for mode, expected in [("ok","ok"),("format","format"),("schema","format"),("empty","format"),("auth","auth"),("forbidden","auth"),("http","http"),("redirect","http"),("large","large")]:
        case(mode, expected)
    case("reuse", "reuse")
    case("transition", "http", 2.3)
    case("recovery", "ok", 2.3)
    case("timeout", "unreachable", 3.6)
    case("ok", "ok", 6.2, True)
    server = ThreadingHTTPServer(("127.0.0.1",0), Handler)
    port = server.server_port
    server.server_close()
    proc = subprocess.run([str(EXE), str(port), "3.2", "unreachable"], capture_output=True, text=True, encoding="utf-8", timeout=7)
    output = json.loads(proc.stdout); output["case"] = "refused"
    assert proc.returncode == 0 and output["pass"], output
    results.append(output)
    print(json.dumps(output, ensure_ascii=False))
    if len(sys.argv) > 1: Path(sys.argv[1]).write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    print(str(len(results)) + " HTTP/lifecycle scenarios passed")
