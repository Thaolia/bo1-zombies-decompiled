"""ZmAdmin's own files: saved servers, the local server's settings, the console history."""
import json
import os
import secrets
import string


def config_dir():
    if os.name == "nt":
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(base, "zmadmin")


def new_password(length=16):
    alphabet = string.ascii_letters + string.digits
    return "".join(secrets.choice(alphabet) for _ in range(length))


class Config:
    def __init__(self, directory=None):
        self.dir = directory or config_dir()
        self.path = os.path.join(self.dir, "servers.json")
        self.history_path = os.path.join(self.dir, "history.txt")
        self.servers = []           # dicts: name, host, port, password
        self.local = {}             # dist folder -> dict: password, port, map
        self.load()

    def load(self):
        try:
            with open(self.path, encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError):
            return
        self.servers = [s for s in data.get("servers", []) if isinstance(s, dict) and s.get("host")]
        self.local = data.get("local", {}) if isinstance(data.get("local"), dict) else {}

    def save(self):
        os.makedirs(self.dir, exist_ok=True)
        tmp = self.path + ".tmp"
        # passwords inside: readable by the user only
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump({"servers": self.servers, "local": self.local}, f, indent=2, ensure_ascii=False)
        os.replace(tmp, self.path)

    def local_settings(self, dist_root):
        key = os.path.normcase(os.path.abspath(dist_root))
        entry = self.local.setdefault(key, {})
        entry.setdefault("password", new_password())
        entry.setdefault("port", 3074)
        entry.setdefault("map", "zombie_theater")
        return entry

    def load_history(self):
        try:
            with open(self.history_path, encoding="utf-8") as f:
                return [line.rstrip("\n") for line in f if line.strip()][-100:]
        except OSError:
            return []

    def save_history(self, history):
        os.makedirs(self.dir, exist_ok=True)
        with open(self.history_path, "w", encoding="utf-8") as f:
            f.write("".join(h + "\n" for h in history[-100:]))
