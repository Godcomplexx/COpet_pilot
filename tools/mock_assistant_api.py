#!/usr/bin/env python3
"""Minimal mock of the CoPet Cloud /v1/query endpoint for on-device testing.

It implements just enough of docs/architecture/cloud_api_contract.md to exercise
the firmware's HTTP assistant backend -- no real weather/AI provider, no keys.

Run on your PC (same Wi-Fi as the device):
    python tools/mock_assistant_api.py            # listens on 0.0.0.0:8000

Then in `idf.py menuconfig` -> CoPet Pilot:
    Assistant backend        = HTTP /v1/query
    Assistant API base URL   = http://<YOUR-PC-IP>:8000
Rebuild + flash, open Assistant Mode, and pick a preset.
"""
import json
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = 8000


def answer_for(request):
    query_type = request.get("type", "query")
    if query_type == "weather":
        return {"text": "MOCK: SUNNY, ABOUT 18 DEGREES.", "mood": "helpful"}
    if query_type == "time":
        return {"text": "MOCK: HALF PAST TWO.", "mood": "neutral"}
    return {"text": "HELLO FROM THE MOCK CLOUD.", "mood": "happy"}


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/v1/query":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", 0))
        try:
            request = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            request = {}
        print("query:", request)

        response = {"request_id": request.get("request_id", "0"),
                    "status": "ok", "ttl_sec": 60}
        response.update(answer_for(request))
        payload = json.dumps(response).encode("utf-8")

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args):
        pass  # keep the console clean; we print queries ourselves


if __name__ == "__main__":
    print(f"CoPet mock cloud on 0.0.0.0:{PORT} (POST /v1/query)")
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
