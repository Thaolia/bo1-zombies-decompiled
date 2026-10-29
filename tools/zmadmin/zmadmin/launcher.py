"""Starts and stops a server from a make_dist.sh folder (BO1Zombies_linux/, BO1Zombies_windows/).

The command line is the one of the folder's server.sh / Server.bat (tools/make_dist.sh), plus:
- '+set bo1_tui 0': no curses console, the output goes to a file;
- the rcon password twice. '+set rcon_password' is applied before server.cfg runs (Com_StartupVariable), and a
  server.cfg 'set rcon_password' would override it; the '+rcon_password <pw>' command runs after server.cfg's exec,
  in command-line order (Dvar_Command), so ZmAdmin's password always wins.
"""
import json
import os
import subprocess
import sys
import time

IS_WINDOWS = os.name == "nt"
EXE_NAME = "BO1Zombies.exe" if IS_WINDOWS else "BO1Zombies"
SCRIPT_NAME = "Server.bat" if IS_WINDOWS else "server.sh"


class Dist:
    def __init__(self, root):
        self.root = os.path.abspath(root)
        self.exe = os.path.join(self.root, EXE_NAME)
        self.home = os.path.join(self.root, "server")
        self.main = os.path.join(self.home, "main")
        self.log = os.path.join(self.main, "console_mp.log")
        self.server_cfg = os.path.join(self.main, "server.cfg")
        self.output = os.path.join(self.main, "zmadmin_server_output.log")
        self.state_path = os.path.join(self.main, "zmadmin_server.json")

    def mods(self):
        try:
            names = sorted(os.listdir(os.path.join(self.root, "mods")))
        except OSError:
            return []
        return [n for n in names if os.path.isdir(os.path.join(self.root, "mods", n)) and not n.startswith((".", "_"))
                and n != "admin"]


def is_dist(path):
    return (os.path.isfile(os.path.join(path, EXE_NAME)) and os.path.isfile(os.path.join(path, SCRIPT_NAME))
            and os.path.isdir(os.path.join(path, "zone")))


def program_dir():
    # a Nuitka onefile binary runs from a temporary folder: argv[0] is where the user put it
    return os.path.dirname(os.path.abspath(sys.argv[0]))


def find_dist(explicit=None):
    candidates = [explicit] if explicit else [program_dir(), os.getcwd()]
    for c in candidates:
        if c and is_dist(c):
            return Dist(c)
    return None


def server_command(dist, map_name, port, password):
    return [dist.exe,
            "+set", "fs_b", dist.root, "+set", "fs_h", dist.home,
            "+set", "bo1_server", "1", "+set", "net_ip", "0.0.0.0", "+set", "net_port", str(port),
            "+set", "bo1_tui", "0", "+set", "rcon_password", password,
            "+rcon_password", password,
            "+devmap", map_name]


def _pid_exe(pid):
    if IS_WINDOWS:
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return None
        try:
            size = wintypes.DWORD(1024)
            buf = ctypes.create_unicode_buffer(size.value)
            if not kernel32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(size)):
                return None
            code = wintypes.DWORD()
            # a process that has exited keeps its handle while someone holds one: STILL_ACTIVE is 259
            if kernel32.GetExitCodeProcess(handle, ctypes.byref(code)) and code.value != 259:
                return None
            return buf.value
        finally:
            kernel32.CloseHandle(handle)
    try:
        return os.readlink(f"/proc/{pid}/exe")
    except OSError:
        return None


def _same_file(a, b):
    return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))


class LocalServer:
    """The server this dist folder runs, found again through its state file by a later ZmAdmin."""

    def __init__(self, dist):
        self.dist = dist
        self.pid = None
        self.port = None
        self.map = None
        self._proc = None
        self._read_state()

    def _read_state(self):
        try:
            with open(self.dist.state_path, encoding="utf-8") as f:
                state = json.load(f)
        except (OSError, ValueError):
            return
        self.pid, self.port, self.map = state.get("pid"), state.get("port"), state.get("map")

    def running(self):
        if self._proc is not None and self._proc.poll() is not None:
            self._proc = None
            return False
        if not self.pid:
            return False
        exe = _pid_exe(self.pid)
        # a PID the system gave to another program since: not our server
        return exe is not None and _same_file(exe.removesuffix(" (deleted)"), self.dist.exe)

    def start(self, map_name, port, password):
        if self.running():
            raise RuntimeError(f"le serveur local tourne déjà (PID {self.pid})")
        os.makedirs(self.dist.main, exist_ok=True)
        out = open(self.dist.output, "wb")
        kwargs = {}
        if IS_WINDOWS:
            kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            kwargs["start_new_session"] = True
        try:
            self._proc = subprocess.Popen(server_command(self.dist, map_name, port, password), cwd=self.dist.root,
                                          stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT, **kwargs)
        finally:
            out.close()
        self.pid, self.port, self.map = self._proc.pid, port, map_name
        with open(self.dist.state_path, "w", encoding="utf-8") as f:
            json.dump({"pid": self.pid, "port": port, "map": map_name, "started": time.time()}, f)

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

    def exit_text(self):
        """The last lines of the server's output, to explain why it stopped."""
        try:
            with open(self.dist.output, "rb") as f:
                f.seek(max(0, os.path.getsize(self.dist.output) - 2048))
                return f.read().decode("latin-1").strip().splitlines()[-5:]
        except OSError:
            return []
