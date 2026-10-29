"""The network thread: one per connected server. The UI never waits on the network; it sends jobs and reads events.

Events (tuples on `events`):
  ("status", Status or None, error text)     every `status_every` seconds
  ("admin", AdminReport)                      zm_admin read in the background
  ("reply", tag, command, text, error)        a command the UI sent; text None on error
  ("auth", error text)                        the password was refused: polling of rcon stops until it changes
"""
import queue
import socket
import threading
import time

from . import protocol
from .model import parse_zm_admin


class ServerLink:
    def __init__(self, host, port, password, status_every=1.0, admin_every=5.0):
        self.host, self.port = host, port
        self.addr = (socket.gethostbyname(host), port)
        self.rcon = protocol.RconClient(self.addr, password)
        self.status_every = status_every
        self.admin_every = admin_every
        self.admin_wanted = False   # the UI shows a view that uses zm_admin
        self.events = queue.Queue()
        self.online = None          # None until the first status answer or timeout
        self.auth_ok = None
        self._jobs = queue.Queue()
        self._busy = 0
        self._admin_due = 0.0
        self._stop = threading.Event()
        self._status_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._thread = threading.Thread(target=self._run, name="zmadmin-link", daemon=True)
        self._thread.start()

    @property
    def password(self):
        return self.rcon.password

    def set_password(self, password):
        self.rcon.password = password
        self.auth_ok = None

    @property
    def queued(self):
        return self._jobs.qsize() + self._busy

    def send(self, command, tag="console"):
        self._jobs.put((tag, command))

    def refresh_admin(self, delay=0.0):
        """Read zm_admin again after `delay` s (the admin mod acts one server frame after a command)."""
        self._admin_due = min(self._admin_due, time.monotonic() + delay)

    def close(self):
        self._stop.set()
        self._jobs.put(None)
        self._thread.join(timeout=3)
        self.rcon.close()
        self._status_sock.close()

    def _run(self):
        next_status = 0.0
        self._admin_due = time.monotonic()
        while not self._stop.is_set():
            now = time.monotonic()
            if now >= next_status:
                next_status = now + self.status_every
                self._poll_status()
                continue
            if self._admin_poll_due(now):
                self._admin_due = now + self.admin_every if self.admin_every > 0 else float("inf")
                self._run_job(("admin", "zm_admin"))
                continue
            wait = next_status - now
            if self.admin_wanted and self.auth_ok is not False and self.online:
                wait = min(wait, max(0.0, self._admin_due - now))
            try:
                job = self._jobs.get(timeout=max(0.02, wait))
            except queue.Empty:
                continue
            if job is None:
                break
            self._busy = 1
            try:
                self._run_job(job)
            finally:
                self._busy = 0

    def _admin_poll_due(self, now):
        return (self.admin_wanted and self.online and self.auth_ok is not False and self._jobs.empty()
                and now >= self._admin_due)

    def _poll_status(self):
        try:
            status = protocol.query_status(self.addr, timeout=0.8, sock=self._status_sock)
        except (protocol.ProtocolError, OSError) as e:
            self.online = False
            self.events.put(("status", None, str(e)))
            return
        self.online = True
        self.events.put(("status", status, ""))

    def _run_job(self, job):
        tag, command = job
        try:
            text = self.rcon.command(command)
        except protocol.RconAuthError as e:
            self.auth_ok = False
            self.events.put(("auth", str(e)))
            if tag != "admin":
                self.events.put(("reply", tag, command, None, str(e)))
            return
        except (protocol.ProtocolError, OSError) as e:
            if tag != "admin":
                self.events.put(("reply", tag, command, None, str(e)))
            return
        self.auth_ok = True
        if tag == "admin":
            report = parse_zm_admin(text)
            if report is not None:
                self.events.put(("admin", report))
            return
        self.events.put(("reply", tag, command, text, ""))
        if command.split(" ", 1)[0] == "zm_admin":
            report = parse_zm_admin(text)
            if report is not None:
                self.events.put(("admin", report))
