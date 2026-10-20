# Local stand-in for hamloprod.org /latest while recording the demo (make_video.ps1 -LocalUpdateServer):
# answers with the current release, so the Update button shows "Latest". Keep the version in step with CMakeLists.txt.
import http.server, json
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = json.dumps({"version": "0.1.0-beta.6", "title": "HPDG 0.1.0 Beta 6"}).encode()
        self.send_response(200 if self.path.endswith("/latest") else 404)
        self.send_header("Content-Type", "application/json"); self.end_headers(); self.wfile.write(body)
    def log_message(self, *a): pass
http.server.HTTPServer(("127.0.0.1", 8765), H).serve_forever()
