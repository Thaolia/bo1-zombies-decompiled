"""Puts the player's own game files in a package folder, as setup.ps1 does for the development tree.

From the player's Call of Duty: Black Ops install (Steam), it copies localization.txt, main/*.iwd and main/video/, and
converts the zombies zones with the package's ffconv (the converter built with this game: its zones match the
program). A package made by `tools/make_dist.sh --no-game-data` then holds no file of the game.

The install is only read, never changed. The new zones go to a temporary folder and replace zone/ only once ffconv
succeeded, as make_dist.sh's convert_zones does, so a failed run leaves the old ones in place.
"""
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import threading
import time

from zmadmin.launcher import IS_WINDOWS

GAME_FOLDER = "Call of Duty Black Ops"
MARKER = "zmclient_setup.json"
ZONES_BYTES = 1 << 30           # the converted zones take ~770 MB: room for them and the temporary copy's growth
FFCONV_NAME = "ffconv.exe"
_PROGRESS = re.compile(r"^\[(\d+)/(\d+)\]")


# ------------------------------------------------------------------------------------------------- finding the game
def steam_roots():
    roots = []
    if IS_WINDOWS:
        try:
            import winreg
            for hive, key in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
                              (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam")):
                try:
                    with winreg.OpenKey(hive, key) as k:
                        for name in ("SteamPath", "InstallPath"):
                            try:
                                roots.append(os.path.normpath(winreg.QueryValueEx(k, name)[0]))
                            except OSError:
                                pass
                except OSError:
                    pass
        except ImportError:
            pass
        roots.append(r"C:\Program Files (x86)\Steam")
    else:
        home = os.path.expanduser("~")
        roots += [os.path.join(home, ".steam", "steam"), os.path.join(home, ".local", "share", "Steam"),
                  os.path.join(home, ".var", "app", "com.valvesoftware.Steam", ".local", "share", "Steam")]
    return list(dict.fromkeys(roots))


def library_folders(steam_root):
    """The library paths steamapps/libraryfolders.vdf lists, the Steam folder itself first."""
    out = [steam_root]
    try:
        with open(os.path.join(steam_root, "steamapps", "libraryfolders.vdf"), encoding="utf-8",
                  errors="replace") as f:
            text = f.read()
    except OSError:
        return out
    for m in re.finditer(r'"path"\s+"([^"]+)"', text):
        out.append(os.path.normpath(m.group(1).replace("\\\\", "\\")))
    return list(dict.fromkeys(out))


def is_install(path):
    """zone/ with its common folder, localization.txt and main/*.iwd. Steam names the folder "Common": compared
    without regard to case, for a case-sensitive file system (a Linux Steam library)."""
    if not path:
        return False
    try:
        common = any(n.lower() == "common" and os.path.isdir(os.path.join(path, "zone", n))
                     for n in os.listdir(os.path.join(path, "zone")))
    except OSError:
        return False
    return common and os.path.isfile(os.path.join(path, "localization.txt")) \
        and bool(glob.glob(os.path.join(path, "main", "*.iwd")))


def find_installs():
    found = []
    for root in steam_roots():
        for lib in library_folders(root):
            p = os.path.join(lib, "steamapps", "common", GAME_FOLDER)
            if is_install(p):
                found.append(os.path.normpath(p))
    return list(dict.fromkeys(found))


# ------------------------------------------------------------------------------------------------------------ state
_md5_cache = {}


def file_md5(path):
    """The file's md5, computed again only when its size or date changed (the views ask every few seconds)."""
    st = os.stat(path)
    key = (st.st_size, st.st_mtime_ns)
    hit = _md5_cache.get(path)
    if hit and hit[0] == key:
        return hit[1]
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    _md5_cache[path] = (key, h.hexdigest())
    return h.hexdigest()


class Layout:
    """Where the package keeps each part."""

    def __init__(self, dist_root):
        self.root = dist_root
        self.localization = os.path.join(dist_root, "localization.txt")
        self.main = os.path.join(dist_root, "main")
        self.video = os.path.join(dist_root, "main", "video")
        self.zone = os.path.join(dist_root, "zone")
        self.shaders_pack = os.path.join(dist_root, "shaders.pack")
        self.ffconv = os.path.join(dist_root, FFCONV_NAME)
        self.marker = os.path.join(dist_root, "zone", MARKER)
        self.log = os.path.join(dist_root, "main", "zmclient_setup.log")
        self.needs_pack = not IS_WINDOWS        # the GL renderer reads shaders.pack; D3D9 the zones' own shaders


def state(dist_root):
    """{part: (ok, text)} for localization, iwd, video, zones, pack."""
    lay = Layout(dist_root)
    out = {}
    out["localization"] = (os.path.isfile(lay.localization), "présent" if os.path.isfile(lay.localization)
                           else "absent")
    iwds = glob.glob(os.path.join(lay.main, "*.iwd"))
    out["iwd"] = (bool(iwds), f"{len(iwds)} fichiers .iwd" if iwds else "aucun .iwd")
    videos = glob.glob(os.path.join(lay.video, "*"))
    out["video"] = (bool(videos), f"{len(videos)} fichiers" if videos else "absentes (les cinématiques ne jouent pas)")
    zones = glob.glob(os.path.join(lay.zone, "*", "*.ff"))
    marker = None
    try:
        with open(lay.marker, encoding="utf-8") as f:
            marker = json.load(f)
    except (OSError, ValueError):
        pass
    if not zones:
        out["zones"] = (False, "non converties")
    elif marker and os.path.isfile(lay.ffconv) and marker.get("ffconv_md5") != file_md5(lay.ffconv):
        out["zones"] = (False, f"{len(zones)} zones d'un autre ffconv : à reconvertir")
    elif marker:
        when = time.strftime("%Y-%m-%d %H:%M", time.localtime(marker.get("time", 0)))
        out["zones"] = (True, f"{len(zones)} zones converties le {when}")
    else:
        out["zones"] = (True, f"{len(zones)} zones (livrées avec le paquet)")
    if lay.needs_pack:
        ok = os.path.isfile(lay.shaders_pack)
        out["pack"] = (ok, "présent" if ok else "absent (vient avec les zones)")
    return out


def ready(dist_root):
    """None when the game can start, else what is missing."""
    missing = [k for k, (ok, _) in state(dist_root).items() if not ok and k != "video"]
    names = {"localization": "localization.txt", "iwd": "les .iwd", "zones": "les zones converties",
             "pack": "shaders.pack"}
    return None if not missing else "manque " + ", ".join(names[k] for k in missing)


def _windows_paths(tool, paths):
    """The paths as the Windows program sees them (wslpath -w, winepath -w), or the error text."""
    try:
        return [subprocess.run([tool, "-w", p], capture_output=True, text=True, check=True).stdout.strip()
                for p in paths]
    except (OSError, subprocess.CalledProcessError) as e:
        return f"{os.path.basename(tool)} : {e}"


def converter_command(lay, install, out_dir):
    """ffconv's command line, or a text saying why there is none. ffconv is a Windows program: Linux runs it through
    WSL's interop when it can (a WSL distribution runs .exe files itself), else under wine."""
    if not os.path.isfile(lay.ffconv):
        return f"pas de {FFCONV_NAME} dans le dossier : ce paquet ne sait pas convertir les zones"
    if IS_WINDOWS:
        return [lay.ffconv, "--verify", "--game", install, "--out", out_dir]
    wslpath = shutil.which("wslpath")
    if wslpath and os.path.exists("/proc/sys/fs/binfmt_misc/WSLInterop"):
        runner, tool = [], wslpath
    else:
        wine, winepath = shutil.which("wine"), shutil.which("winepath")
        if not wine or not winepath:
            return ("ffconv est un programme Windows : installez wine, ou convertissez sur un PC Windows (ZmClient, "
                    "vue Installation) et copiez zone/ et shaders.pack ici")
        runner, tool = [wine], winepath
    win = _windows_paths(tool, [install, out_dir])
    if isinstance(win, str):
        return win
    return runner + [lay.ffconv, "--verify", "--game", win[0], "--out", win[1]]


# ------------------------------------------------------------------------------------------------------------- copy
def _plan_copies(install, lay, video):
    """(source, destination, size) of every file to copy (the ones whose size or date differ)."""
    pairs = [(os.path.join(install, "localization.txt"), lay.localization)]
    pairs += [(p, os.path.join(lay.main, os.path.basename(p))) for p in sorted(glob.glob(os.path.join(install, "main",
                                                                                                      "*.iwd")))]
    if video:
        src = os.path.join(install, "main", "video")
        for dirpath, _, files in os.walk(src):
            for n in sorted(files):
                p = os.path.join(dirpath, n)
                pairs.append((p, os.path.join(lay.video, os.path.relpath(p, src))))
    plan = []
    for s, d in pairs:
        st = os.stat(s)
        try:
            dt = os.stat(d)
            if dt.st_size == st.st_size and int(dt.st_mtime) == int(st.st_mtime):
                continue
        except OSError:
            pass
        plan.append((s, d, st.st_size))
    return plan


class Setup(threading.Thread):
    """Runs the copy and the conversion; the view reads `phase`, `done_bytes`/`total_bytes`, `zone_at`/`zone_count`,
    `lines` (the last output lines, `line_count` in all) and, at the end, `error` (None on success)."""

    def __init__(self, dist_root, install, video=True, convert=True):
        super().__init__(daemon=True)
        self.lay = Layout(dist_root)
        # absolute: ffconv runs from the package folder, where a relative path would point elsewhere
        self.install = os.path.abspath(install)
        self.video = video
        self.convert = convert
        self.phase = "préparation"
        self.done_bytes = self.total_bytes = 0
        self.zone_at = self.zone_count = 0
        self.lines = []             # the last 200 lines
        self.line_count = 0         # every line noted so far
        self._lines_lock = threading.Lock()
        self.error = None
        self.finished = False
        self.ended = 0.0            # monotonic time it finished
        self.cancelled = False
        self._proc = None

    def note(self, line):
        with self._lines_lock:
            self.lines.append(line)
            self.line_count += 1
            del self.lines[:-200]
        try:
            with open(self.lay.log, "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except OSError:
            pass

    def since(self, count):
        """(the lines noted after the first `count`, as many as are kept; the new total)."""
        with self._lines_lock:
            new = min(self.line_count - count, len(self.lines))
            return (self.lines[-new:] if new > 0 else []), self.line_count

    def cancel(self):
        self.cancelled = True
        if self._proc and self._proc.poll() is None:
            self._proc.kill()

    def run(self):
        try:
            self._run()
        except OSError as e:
            self.error = str(e)
        if self.error:
            self.note(f"échec : {self.error}")
        self.ended = time.monotonic()
        self.finished = True

    def _run(self):
        lay = self.lay
        if not is_install(self.install):
            self.error = f"{self.install} n'est pas une installation de Black Ops (zone/, main/*.iwd, localization.txt)"
            return
        if os.path.normcase(os.path.abspath(self.install)) == os.path.normcase(os.path.abspath(lay.root)):
            self.error = "le dossier du jeu et celui du paquet sont le même"
            return
        os.makedirs(lay.main, exist_ok=True)
        self.note(f"--- installation depuis {self.install} ({time.strftime('%Y-%m-%d %H:%M:%S')}) ---")
        plan = _plan_copies(self.install, lay, self.video)
        self.total_bytes = sum(p[2] for p in plan)
        need = self.total_bytes + (ZONES_BYTES if self.convert else 0)
        free = shutil.disk_usage(lay.root).free
        if free < need:
            self.error = f"pas assez de place : {need >> 20} Mo nécessaires, {free >> 20} Mo libres"
            return
        if self.convert:
            # before the long copy: a package that cannot convert should say so at once
            probe = converter_command(lay, self.install, lay.root)
            if isinstance(probe, str):
                self.error = probe
                return
        self.phase = "copie"
        self.note(f"{len(plan)} fichiers à copier ({self.total_bytes >> 20} Mo), les autres sont à jour")
        for src, dst, size in plan:
            if self.cancelled:
                self.error = "annulé"
                return
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            tmp = dst + ".zmclient-part"
            with open(src, "rb") as fi, open(tmp, "wb") as fo:
                while True:
                    chunk = fi.read(4 << 20)
                    if not chunk:
                        break
                    fo.write(chunk)
                    self.done_bytes += len(chunk)
                    if self.cancelled:
                        break
            if self.cancelled:
                os.remove(tmp)
                self.error = "annulé"
                return
            shutil.copystat(src, tmp)
            os.replace(tmp, dst)
            self.note(f"copié {os.path.relpath(dst, lay.root)}")
        if self.convert:
            self._convert()
        else:
            self.phase = "terminé"

    def _convert(self):
        lay = self.lay
        self.phase = "conversion des zones"
        new = os.path.join(lay.root, "zone.zmclient-new")
        shutil.rmtree(new, ignore_errors=True)
        os.makedirs(new)
        cmd = converter_command(lay, self.install, new)
        if isinstance(cmd, str):
            self.error = cmd
            return
        self.note("ffconv : " + " ".join(cmd))
        kwargs = {"creationflags": subprocess.CREATE_NO_WINDOW} if IS_WINDOWS else {}
        self._proc = subprocess.Popen(cmd, cwd=lay.root, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, **kwargs)
        with self._proc.stdout:
            for raw in self._proc.stdout:
                line = raw.decode("latin-1").rstrip("\r\n")
                m = _PROGRESS.match(line)
                if m:
                    self.zone_at, self.zone_count = int(m.group(1)), int(m.group(2))
                self.note(line)
        code = self._proc.wait()
        if self.cancelled:
            shutil.rmtree(new, ignore_errors=True)
            self.error = "annulé"
            return
        produced = glob.glob(os.path.join(new, "zone", "*", "*.ff"))
        if code != 0 or not produced:
            shutil.rmtree(new, ignore_errors=True)
            self.error = f"ffconv a échoué (code {code}) : les zones précédentes sont gardées ; détails dans {lay.log}"
            return
        self.phase = "mise en place"
        old = os.path.join(lay.root, "zone.zmclient-old")
        shutil.rmtree(old, ignore_errors=True)
        if os.path.isdir(lay.zone):
            os.replace(lay.zone, old)
        os.replace(os.path.join(new, "zone"), lay.zone)
        pack = os.path.join(new, "shaders.pack")
        if lay.needs_pack and os.path.isfile(pack):
            os.replace(pack, lay.shaders_pack)
        shutil.rmtree(new, ignore_errors=True)
        shutil.rmtree(old, ignore_errors=True)
        with open(lay.marker, "w", encoding="utf-8") as f:
            json.dump({"ffconv_md5": file_md5(lay.ffconv), "time": time.time(), "install": self.install,
                       "zones": len(produced)}, f)
        self.note(f"{len(produced)} zones en place")
        self.phase = "terminé"
