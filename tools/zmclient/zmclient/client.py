"""Starts, follows and stops the game client of a make_dist.sh folder.

The client's PID is kept in main/zmclient_client.json, so a later ZmClient finds a game it started (the PID's program
is checked to be this folder's, as ZmAdmin does for its server). The client and a local server run the same program
from the same folder: they differ by PID and by fs_h (the server's is server/).
"""
import json
import os
import subprocess
import time

from zmadmin.launcher import IS_WINDOWS, _pid_exe, _same_file

STATE_NAME = "zmclient_client.json"
OUTPUT_NAME = "zmclient_client_output.log"


class Client:
    def __init__(self, dist):
        self.dist = dist
        self.main = os.path.join(dist.root, "main")
        self.state_path = os.path.join(self.main, STATE_NAME)
        self.output = os.path.join(self.main, OUTPUT_NAME)
        self.pid = None
        self.started = None
        self.fs_game = ""
        self._proc = None
        self._read_state()

    def _read_state(self):
        try:
            with open(self.state_path, encoding="utf-8") as f:
                state = json.load(f)
        except (OSError, ValueError):
            return
        if isinstance(state, dict):
            self.pid, self.started, self.fs_game = state.get("pid"), state.get("started"), state.get("fs_game", "")

    def running(self):
        if self._proc is not None and self._proc.poll() is not None:
            self._proc = None
            return False
        if not self.pid:
            return False
        exe = _pid_exe(self.pid)
        return exe is not None and _same_file(exe.removesuffix(" (deleted)"), self.dist.exe)

    def start(self, argv, fs_game=""):
        if self.running():
            raise RuntimeError(f"le jeu tourne déjà (PID {self.pid})")
        os.makedirs(self.main, exist_ok=True)
        kwargs = {}
        if IS_WINDOWS:
            kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            kwargs["start_new_session"] = True
        out = open(self.output, "wb")
        try:
            self._proc = subprocess.Popen(argv, cwd=self.dist.root, stdin=subprocess.DEVNULL, stdout=out,
                                          stderr=subprocess.STDOUT, **kwargs)
        finally:
            out.close()
        self.pid, self.started, self.fs_game = self._proc.pid, time.time(), fs_game
        with open(self.state_path, "w", encoding="utf-8") as f:
            json.dump({"pid": self.pid, "started": self.started, "fs_game": fs_game}, f)

    def kill(self):
        if not self.running():
            return
        if self._proc is not None:
            self._proc.kill()
            self._proc.wait(timeout=10)
        elif IS_WINDOWS:
            subprocess.run(["taskkill", "/F", "/PID", str(self.pid)], capture_output=True)
        else:
            os.kill(self.pid, 9)

    def log_candidates(self, fs_game=None):
        """Where the client's console_mp.log may be: under fs_h, in fs_game's folder or main/ (Windows), or fs_h itself
        (seen in a Linux package)."""
        game = self.fs_game if fs_game is None else fs_game
        folders = [game.replace("/", os.sep)] if game else []
        folders += ["main", ""]
        return [os.path.join(self.dist.root, f, "console_mp.log") for f in folders]

    def log_path(self, fs_game=None):
        """The most recently written of the candidates since the game started (a file an earlier run left in
        another folder does not count), else the first one."""
        found = []
        for p in self.log_candidates(fs_game):
            try:
                mtime = os.path.getmtime(p)
            except OSError:
                continue
            if not self.started or mtime >= self.started - 1:
                found.append((mtime, p))
        return max(found)[1] if found else self.log_candidates(fs_game)[0]

    def exit_text(self):
        try:
            with open(self.output, "rb") as f:
                f.seek(max(0, os.path.getsize(self.output) - 2048))
                return f.read().decode("latin-1").strip().splitlines()[-5:]
        except OSError:
            return []
