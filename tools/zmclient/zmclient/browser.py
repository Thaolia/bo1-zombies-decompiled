"""The server browser's network thread: getstatus to the favourites, and a broadcast getstatus for the LAN.

A server answers at most one getstatus every 50 ms, all senders together (SV_StatusRequest): each round broadcasts
first, then asks only the favourites the broadcast did not reach, so a favourite on the LAN is asked once per round.
"""
import select
import socket
import threading
import time

from zmadmin.protocol import OOB, ProtocolError, parse_status

LAN_PORTS = range(3074, 3084)   # a second server on one machine takes net_port + 1..9
ROUND_SECONDS = 3.0
LISTEN_SECONDS = 0.6
FORGET_SECONDS = 15.0           # a LAN server silent for this long leaves the list


def _local_broadcasts():
    """255.255.255.255 and the /24 broadcast of each of this machine's IPv4 addresses (Windows sends the limited
    broadcast on one interface only)."""
    out = {"255.255.255.255"}
    try:
        infos = socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)
    except OSError:
        infos = []
    for info in infos:
        ip = info[4][0]
        if not ip.startswith("127."):
            out.add(ip.rsplit(".", 1)[0] + ".255")
    return sorted(out)


class Entry:
    def __init__(self, addr):
        self.addr = addr            # (ip, port)
        self.status = None
        self.ping = None            # ms
        self.seen = 0.0             # monotonic time of the last answer
        self.lan = False
        self.error = ""


class Browser(threading.Thread):
    """favorites(): the current list of (host, port). lan_targets: the (address, port) pairs a round broadcasts to,
    None for the machine's broadcast addresses on LAN_PORTS."""

    def __init__(self, favorites, lan_targets=None, round_seconds=ROUND_SECONDS, listen=LISTEN_SECONDS):
        super().__init__(daemon=True)
        self.favorites = favorites
        self.lan_targets = lan_targets
        self.round_seconds = round_seconds
        self.listen = listen
        self.lock = threading.Lock()
        self.entries = {}           # (ip, port) -> Entry
        self.resolved = {}          # (host, port) -> (ip, port) or the resolution error
        self.rounds = 0
        self.wake = threading.Event()
        self.running = True
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.sock.bind(("", 0))

    def stop(self):
        self.running = False
        self.wake.set()
        self.join(timeout=3)
        self.sock.close()

    def refresh(self):
        self.wake.set()

    def snapshot(self):
        """{(ip, port): Entry} copies and {(host, port): (ip, port) or error text}."""
        with self.lock:
            entries = {}
            for k, e in self.entries.items():
                c = Entry(e.addr)
                c.status, c.ping, c.seen, c.lan, c.error = e.status, e.ping, e.seen, e.lan, e.error
                entries[k] = c
            return entries, dict(self.resolved)

    def _resolve(self, host, port):
        try:
            ip = socket.gethostbyname(host)
        except (OSError, UnicodeError) as e:
            return f"adresse introuvable ({e.__class__.__name__})"
        return (ip, port)

    def _send(self, addr):
        try:
            self.sock.sendto(OOB + b"getstatus", addr)
        except OSError:
            return False
        return True

    def _collect(self, sent_at, lan, until):
        while True:
            left = until - time.monotonic()
            if left <= 0 or not select.select([self.sock], [], [], left)[0]:
                return
            try:
                data, src = self.sock.recvfrom(65536)
            except (ConnectionResetError, OSError):
                continue        # Windows: the ICMP unreachable of an earlier send
            if not data.startswith(OOB + b"statusResponse"):
                continue
            try:
                status = parse_status(data)
            except (ProtocolError, ValueError):
                continue
            now = time.monotonic()
            key = (src[0], src[1])
            with self.lock:
                e = self.entries.setdefault(key, Entry(key))
                e.status, e.seen, e.error = status, now, ""
                e.ping = int((now - sent_at.get(key, sent_at.get("broadcast", now))) * 1000)
                e.lan = e.lan or lan

    def round(self):
        targets = self.lan_targets
        if targets is None:
            targets = [(b, p) for b in _local_broadcasts() for p in LAN_PORTS]
        start = time.monotonic()
        sent = {"broadcast": start}
        for t in targets:
            self._send(t)
        self._collect(sent, True, start + self.listen)
        favs = []
        for host, port in self.favorites():
            key = (host, port)
            if key not in self.resolved or isinstance(self.resolved[key], str):
                found = self._resolve(host, port)
                with self.lock:
                    self.resolved[key] = found
            if not isinstance(self.resolved[key], str):
                favs.append(self.resolved[key])
        with self.lock:
            fresh = {k for k, e in self.entries.items() if e.seen >= start}
        ask = [a for a in dict.fromkeys(favs) if a not in fresh]
        if ask:
            now = time.monotonic()
            for a in ask:
                if self._send(a):
                    sent[a] = now
            self._collect(sent, False, now + self.listen)
        now = time.monotonic()
        with self.lock:
            for a in ask:
                e = self.entries.setdefault(a, Entry(a))
                if e.seen < start:
                    e.status, e.ping, e.error = None, None, "pas de réponse"
            favset = set(favs)
            for k in [k for k, e in self.entries.items()
                      if k not in favset and now - e.seen > FORGET_SECONDS]:
                del self.entries[k]
            self.rounds += 1

    def run(self):
        while self.running:
            try:
                self.round()
            except OSError:
                pass
            self.wake.wait(self.round_seconds)
            self.wake.clear()
