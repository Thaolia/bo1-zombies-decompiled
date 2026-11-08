"""A folder browser for the Installation view: walk the disk with the arrows instead of typing a path.

The listing is plain data (unit-tested); FolderDialog draws it with ZmAdmin's dialog frame. A folder that is a
Black Ops install (gamesetup.is_install) is marked in the list, so the player sees where to stop.
"""
import curses
import os
import string

from zmadmin.widgets import (BACKSPACE, C_DIM, C_ERR, C_INFO, C_OK, ENTER, ESC, OK_MARK, Dialog, color, put)

from . import gamesetup

COMPUTER = ""                   # Windows: the list of drives, above every drive's root
MARK_LIMIT = 300                # past this many subfolders, the install marks are not looked up (3 file checks each)
_SKIPPED = {"system volume information", "$recycle.bin", "lost+found"}


def drives():
    """The drive roots of this Windows machine ("C:\\", "D:\\"...)."""
    listdrives = getattr(os, "listdrives", None)       # Python 3.12+, Windows only
    if listdrives:
        try:
            return sorted(listdrives())
        except OSError:
            pass
    return [f"{d}:\\" for d in string.ascii_uppercase if os.path.exists(f"{d}:\\")]


def parent(path):
    """The folder above `path`; on Windows, above a drive's root comes COMPUTER; on posix, "/" stays "/"."""
    if path == COMPUTER:
        return COMPUTER
    norm = os.path.normpath(path)
    up = os.path.dirname(norm)
    if up == norm or not up:
        return COMPUTER if os.name == "nt" else norm
    return up


class Entry:
    def __init__(self, name, path, install=False, up=False):
        self.name = name
        self.path = path
        self.install = install
        self.up = up            # the ".." line


def listing(path, mark=True):
    """(entries, error text or ""). The ".." line first (unless at the top), then the subfolders, sorted without
    regard to case; hidden and system folders are left out."""
    if path == COMPUTER:
        return [Entry(d, d, mark and gamesetup.is_install(d)) for d in drives()], ""
    out = []
    if parent(path) != os.path.normpath(path):
        out.append(Entry("..", parent(path), up=True))
    try:
        with os.scandir(path) as it:
            names = []
            for e in it:
                try:
                    if not e.is_dir():
                        continue
                except OSError:
                    continue
                if e.name.startswith((".", "$")) or e.name.lower() in _SKIPPED:
                    continue
                names.append(e.name)
    except OSError as e:
        return out, f"illisible : {e.strerror or e}"
    names.sort(key=str.lower)
    check = mark and len(names) <= MARK_LIMIT
    for n in names:
        full = os.path.join(path, n)
        out.append(Entry(n, full, check and gamesetup.is_install(full)))
    return out, ""


def start_folder(current, installs):
    """Where the browser opens: the chosen install, else the first Steam library's games folder, else the home."""
    for candidate in [current] + [os.path.dirname(p) for p in installs]:
        if candidate and os.path.isdir(candidate):
            return os.path.normpath(candidate)
    for root in gamesetup.steam_roots():
        common = os.path.join(root, "steamapps", "common")
        if os.path.isdir(common):
            return common
    return os.path.expanduser("~")


class FolderDialog(Dialog):
    """on_pick(path) returns an error text (the dialog stays open and shows it) or None."""

    def __init__(self, title, start, on_pick):
        super().__init__()
        self.title = title
        self.on_pick = on_pick
        self.error = ""
        self.go(start)

    def go(self, path, select=None):
        path = path if path == COMPUTER else os.path.abspath(path)
        self.path = path
        self.entries, self.error = listing(path)
        self.here_install = path != COMPUTER and gamesetup.is_install(path)
        self.sel = 0
        self.top = 0
        if select:
            for i, e in enumerate(self.entries):
                if os.path.normcase(e.path) == os.path.normcase(select) and not e.up:
                    self.sel = i
                    break

    def size(self, h, w):
        return h - 2, min(w - 2, 100)

    def draw(self, scr):
        win = self.frame(scr)
        dh, dw = win.getmaxyx()
        where = self.path if self.path != COMPUTER else "Ordinateur (lecteurs)"
        if len(where) > dw - 6:
            where = "…" + where[-(dw - 7):]
        put(win, 1, 2, where, curses.A_BOLD, width=dw - 4)
        if self.here_install:
            put(win, 2, 2, f"{OK_MARK} ce dossier est une installation de Black Ops : « Choisir ce dossier »",
                color(C_OK), width=dw - 4)
        else:
            put(win, 2, 2, "Entrez dans le dossier qui contient BlackOps.exe (zone/, main/).", color(C_DIM),
                width=dw - 4)
        rows = dh - 7
        self.sel = max(0, min(self.sel, len(self.entries) - 1))
        self.top = max(0, min(self.top, self.sel), self.sel - rows + 1)
        if not self.entries:
            put(win, 4, 4, "(aucun sous-dossier)", color(C_DIM))
        for i, e in enumerate(self.entries[self.top:self.top + rows]):
            n = self.top + i
            label = "..  (dossier parent)" if e.up else e.name + ("/" if os.name != "nt" else "\\")
            mark = f"  {OK_MARK} Black Ops" if e.install else ""
            attr = curses.A_REVERSE if n == self.sel else (color(C_OK) if e.install else 0)
            put(win, 3 + i, 2, f" {label}{mark} ".ljust(dw - 4), attr, width=dw - 4)
            self.zone(3 + i, 2, dw - 4, lambda n=n: self.select(n), lambda n=n: self.open_entry(n))
        if len(self.entries) > rows:
            put(win, dh - 4, 2, f"({self.top + 1}-{min(len(self.entries), self.top + rows)} sur {len(self.entries)})",
                color(C_DIM))
        put(win, dh - 3, 2, self.error, color(C_ERR), width=dw - 4)
        x = 2
        for label, action in (("[ Ouvrir ]", lambda: self.open_entry(self.sel)), ("[ Dossier parent ]", self.up),
                              ("[ Choisir ce dossier ]", self.pick), ("[ Annuler ]", lambda: True)):
            x = self.button(win, dh - 2, x, label, curses.A_BOLD, action)
        put(win, dh - 1, 2, " Entrée/→ ouvrir  ←/Retour arrière parent  Espace choisir  lettre : aller à  Échap ",
            color(C_INFO), width=dw - 4)

    def select(self, n):
        self.sel = n
        return False

    def open_entry(self, n):
        if 0 <= n < len(self.entries):
            e = self.entries[n]
            if e.up:
                return self.up()
            self.go(e.path)
        return False

    def up(self):
        if parent(self.path) != self.path:
            self.go(parent(self.path), select=self.path)
        return False

    def pick(self):
        if self.path == COMPUTER:
            self.error = "choisissez un dossier, pas la liste des lecteurs"
            return False
        self.error = self.on_pick(self.path) or ""
        return not self.error

    def jump(self, ch):
        """To the next entry whose name starts with `ch`, after the selection (the type-ahead of file managers)."""
        n = len(self.entries)
        for step in range(1, n + 1):
            i = (self.sel + step) % n
            if not self.entries[i].up and self.entries[i].name.lower().startswith(ch.lower()):
                self.sel = i
                return

    def key(self, k):
        if k == ESC:
            return True
        if k == curses.KEY_UP:
            self.sel = max(0, self.sel - 1)
        elif k == curses.KEY_DOWN:
            self.sel = min(len(self.entries) - 1, self.sel + 1)
        elif k == curses.KEY_PPAGE:
            self.sel = max(0, self.sel - 10)
        elif k == curses.KEY_NPAGE:
            self.sel = min(len(self.entries) - 1, self.sel + 10)
        elif k == curses.KEY_HOME:
            self.sel = 0
        elif k == curses.KEY_END:
            self.sel = len(self.entries) - 1
        elif k in (ENTER, curses.KEY_RIGHT):
            self.open_entry(self.sel)
        elif k in (BACKSPACE, curses.KEY_LEFT):
            self.up()
        elif k == " ":
            return self.pick()
        elif isinstance(k, str) and len(k) == 1 and k.isalnum():
            self.jump(k)
        return False
