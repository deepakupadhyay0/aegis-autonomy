"""Local canned chat-completions server for diagnostic transport smoke tests."""

import argparse
import json
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format_string, *args):
        return

    def send_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            self.send_json(200, {"status": "ok"})
        else:
            self.send_json(404, {"error": "not found"})

    def do_POST(self):
        if self.path != "/v1/chat/completions":
            self.send_json(404, {"error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 65536:
                raise ValueError("invalid request length")
            request = json.loads(self.rfile.read(length))
            if "response_format" in request:
                raise ValueError("structured output was not requested")
            context = json.loads(request["messages"][-1]["content"])
            if not context["observations"]:
                raise ValueError("missing observations")
            self.send_json(200, {"choices": [{"message": {
                "role": "assistant",
                "content": "The observations show a fault, but its cause needs further checks."
            }}]})
        except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
            self.send_json(400, {"error": str(error)})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()
    server = HTTPServer(("127.0.0.1", args.port), Handler)
    print(f"Mock diagnostics server listening on 127.0.0.1:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
