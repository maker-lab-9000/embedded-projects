"""Puts the generated "Alerts" view first in the default Home Assistant dashboard.

One websocket login (HA bans IPs after failed logins; never loop this):
  1. reads the dashboard config and saves a backup to ha/backups/
  2. has HA render the alert template and checks it against the Python
     evaluation of the same rules on HA's current states; stops if they differ
  3. replaces/inserts the view with path "alerts" at position 0 and saves
  4. reads the config back to confirm

  python3 ha/deploy_alerts.py                 deploy
  python3 ha/deploy_alerts.py --restore FILE  save a backup back unchanged

The token comes from HA_TOKEN in the project's .env and is never printed.
"""
import datetime
import json
import os
import socket
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import alerts  # noqa: E402

HOST, PORT = "192.168.178.137", 8123
HERE = os.path.dirname(os.path.abspath(__file__))


def token():
    for line in open(os.path.join(HERE, "..", ".env")):
        if line.startswith("HA_TOKEN="):
            return line.split("=", 1)[1].strip()
    sys.exit("HA_TOKEN missing in .env")


class Ws:
    def __init__(self):
        self.s = socket.create_connection((HOST, PORT), 15)
        self.s.sendall(b"GET /api/websocket HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n" % (HOST.encode(), PORT))
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            self.buf += self.s.recv(4096)
        self.buf = self.buf.split(b"\r\n\r\n", 1)[1]
        self.next_id = 1

    def _read(self, n):
        while len(self.buf) < n:
            c = self.s.recv(65536)
            if not c:
                raise EOFError("connection closed")
            self.buf += c
        r, self.buf = self.buf[:n], self.buf[n:]
        return r

    def recv(self):
        data = b""
        while True:
            b0, b1 = self._read(2)
            n = b1 & 127
            if n == 126: n = struct.unpack(">H", self._read(2))[0]
            elif n == 127: n = struct.unpack(">Q", self._read(8))[0]
            data += self._read(n)
            if b0 & 0x80:
                return json.loads(data)

    def send(self, o):
        p = json.dumps(o).encode()
        m = os.urandom(4)
        n = len(p)
        h = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0xFE]) + struct.pack(">H", n) if n < 65536
                             else bytes([0xFF]) + struct.pack(">Q", n))
        self.s.sendall(h + m + bytes(b ^ m[i % 4] for i, b in enumerate(p)))

    def call(self, o):
        """Sends a command and returns its result message (events for it are skipped)."""
        o = dict(o, id=self.next_id)
        self.next_id += 1
        self.send(o)
        while True:
            r = self.recv()
            if r.get("id") == o["id"] and r.get("type") == "result":
                return r


def login():
    ws = Ws()
    ws.recv()  # auth_required
    ws.send({"type": "auth", "access_token": token()})
    a = ws.recv()
    if a.get("type") != "auth_ok":
        sys.exit("login failed (%s); not retrying" % a.get("type"))
    return ws


def render(ws, template):
    """Renders a template once through render_template; returns the text."""
    sub = ws.next_id
    ws.next_id += 1
    ws.send({"id": sub, "type": "render_template", "template": template, "report_errors": True})
    text = None
    while text is None:
        r = ws.recv()
        if r.get("id") != sub:
            continue
        if r["type"] == "result" and not r.get("success"):
            sys.exit("render_template failed: %s" % r.get("error"))
        if r["type"] == "event":
            ev = r["event"]
            if "error" in ev:
                sys.exit("template error: %s" % ev["error"])
            text = ev["result"]
    ws.call({"type": "unsubscribe_events", "subscription": sub})
    return text


def save(ws, cfg):
    r = ws.call({"type": "lovelace/config/save", "config": cfg})
    if not r.get("success"):
        sys.exit("save failed: %s" % r.get("error"))


def main():
    ws = login()
    if len(sys.argv) == 3 and sys.argv[1] == "--restore":
        save(ws, json.load(open(sys.argv[2])))
        print("restored", sys.argv[2])
        return
    r = ws.call({"type": "lovelace/config"})
    if not r.get("success"):
        sys.exit("cannot read the dashboard: %s" % r.get("error"))
    cfg = r["result"]
    os.makedirs(os.path.join(HERE, "backups"), exist_ok=True)
    backup = os.path.join(HERE, "backups", "lovelace-%s.json" % datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    json.dump(cfg, open(backup, "w"), indent=1, ensure_ascii=False)
    print("backup:", os.path.relpath(backup))

    states = {s["entity_id"]: s["state"] for s in ws.call({"type": "get_states"})["result"]}
    missing = sorted({a[1] for rule in alerts.RULES for a in rule.any_of} - states.keys())
    if missing:
        print("note: not in HA (count as unavailable):", ", ".join(missing))
    expected = alerts.active(states)
    rendered = [line.split("|", 1)[1] for line in render(ws, alerts.jinja()).splitlines() if "|" in line]
    if rendered != expected:
        print("HA template :", rendered)
        print("Python rules:", expected)
        sys.exit("template and rules disagree; nothing saved")
    print("alerts now (%d):" % len(expected), "; ".join(expected) or "none")

    views = [v for v in cfg["views"] if v.get("path") != "alerts"]
    replaced = len(views) != len(cfg["views"])
    cfg["views"] = [alerts.view()] + views
    save(ws, cfg)
    check = ws.call({"type": "lovelace/config"})["result"]["views"]
    if check[0].get("path") != "alerts" or len(check) != len(views) + 1:
        sys.exit("saved, but the read-back looks wrong; restore with --restore %s" % os.path.relpath(backup))
    print("saved: Alerts view %s, first of %d views" % ("replaced" if replaced else "added", len(check)))


if __name__ == "__main__":
    main()
