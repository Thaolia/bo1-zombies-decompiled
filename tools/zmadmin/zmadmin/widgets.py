"""curses building blocks: safe drawing, key names, a line editor and the modal dialogs."""
import curses
import os
import re
import textwrap

ENTER, ESC, TAB, BTAB, BACKSPACE, CTRL_S, CTRL_C = "ENTER", "ESC", "TAB", "BTAB", "BACKSPACE", "CTRL_S", "CTRL_C"
OK_MARK = "OK" if os.name == "nt" else "✓"
BAD_MARK = "X" if os.name == "nt" else "✗"

C_TITLE, C_OK, C_ERR, C_WARN, C_INFO, C_DIM, C_SEL = range(1, 8)


def init_colors():
    if not curses.has_colors():
        return
    curses.start_color()
    try:
        curses.use_default_colors()
        bg = -1
    except curses.error:
        bg = curses.COLOR_BLACK
    curses.init_pair(C_TITLE, curses.COLOR_BLACK, curses.COLOR_CYAN)
    curses.init_pair(C_OK, curses.COLOR_GREEN, bg)
    curses.init_pair(C_ERR, curses.COLOR_RED, bg)
    curses.init_pair(C_WARN, curses.COLOR_YELLOW, bg)
    curses.init_pair(C_INFO, curses.COLOR_CYAN, bg)
    curses.init_pair(C_DIM, curses.COLOR_BLUE, bg)
    curses.init_pair(C_SEL, curses.COLOR_BLACK, curses.COLOR_WHITE)


def cursor(visible):
    try:
        curses.curs_set(1 if visible else 0)
    except curses.error:
        pass  # a terminal that cannot hide or show its cursor


def color(pair):
    return curses.color_pair(pair) if curses.has_colors() else 0


def normalize_key(k):
    """get_wch's result as a printable str of one character, a name above, or a curses KEY_* int."""
    if isinstance(k, str):
        if k in ("\n", "\r"):
            return ENTER
        if k == "\x1b":
            return ESC
        if k == "\t":
            return TAB
        if k in ("\x7f", "\b"):
            return BACKSPACE
        if k == "\x13":
            return CTRL_S
        if k == "\x03":
            return CTRL_C
        if len(k) == 1 and ord(k) < 32:
            return None
        return k
    if k in (curses.KEY_ENTER, 459):  # 459: PDCurses' keypad Enter
        return ENTER
    if k == curses.KEY_BACKSPACE:
        return BACKSPACE
    if k == curses.KEY_BTAB:
        return BTAB
    return k


def put(win, y, x, text, attr=0, width=None):
    """addstr clipped to the window (and to `width`); never raises for the bottom-right cell."""
    h, w = win.getmaxyx()
    if y < 0 or y >= h or x >= w:
        return
    room = w - x if width is None else min(width, w - x)
    if room <= 0:
        return
    text = text[:room]
    try:
        win.addstr(y, x, text, attr)
    except curses.error:
        pass


def fill(win, y, attr, x=0):
    h, w = win.getmaxyx()
    put(win, y, x, " " * (w - x), attr)


def wrap(text, width):
    return textwrap.wrap(text, max(10, width)) or [""]


def wrap_hanging(line, width):
    """A help line wrapped; its continuation lines start under the text after the key column ('F4    text')."""
    if len(line) <= width:
        return [line]
    m = re.match(r"^(\s*\S+(?: \S+){0,2}\s{2,})", line)
    indent = len(m.group(1)) if m and len(m.group(1)) < width // 2 else 0
    return textwrap.wrap(line, width, subsequent_indent=" " * indent) or [""]


class LineEdit:
    def __init__(self, text="", mask=False, history=None):
        self.text = text
        self.pos = len(text)
        self.mask = mask
        self.history = history if history is not None else []
        self._hist_at = None

    def set(self, text):
        self.text, self.pos = text, len(text)

    def key(self, k):
        """True when the key was used."""
        if isinstance(k, str) and len(k) == 1:
            self.text = self.text[:self.pos] + k + self.text[self.pos:]
            self.pos += 1
        elif k == BACKSPACE:
            if self.pos:
                self.text = self.text[:self.pos - 1] + self.text[self.pos:]
                self.pos -= 1
        elif k == curses.KEY_DC:
            self.text = self.text[:self.pos] + self.text[self.pos + 1:]
        elif k == curses.KEY_LEFT:
            self.pos = max(0, self.pos - 1)
        elif k == curses.KEY_RIGHT:
            self.pos = min(len(self.text), self.pos + 1)
        elif k == curses.KEY_HOME:
            self.pos = 0
        elif k == curses.KEY_END:
            self.pos = len(self.text)
        elif k in (curses.KEY_UP, curses.KEY_DOWN) and self.history:
            if self._hist_at is None:
                self._hist_at = len(self.history)
            self._hist_at += -1 if k == curses.KEY_UP else 1
            self._hist_at = max(0, min(len(self.history), self._hist_at))
            self.set(self.history[self._hist_at] if self._hist_at < len(self.history) else "")
        else:
            return False
        if k not in (curses.KEY_UP, curses.KEY_DOWN):
            self._hist_at = None
        return True

    def draw(self, win, y, x, width, attr=0, focused=True):
        shown = "*" * len(self.text) if self.mask else self.text
        start = max(0, self.pos - width + 1)
        put(win, y, x, shown[start:start + width].ljust(width), attr | curses.A_UNDERLINE)
        if focused:
            try:
                win.move(y, x + self.pos - start)
            except curses.error:
                pass


class Dialog:
    """A centred box. `key` returns True when the dialog is done (then the app drops it)."""

    title = ""
    cursor = False

    def size(self, h, w):
        return min(h - 2, 20), min(w - 4, 76)

    def frame(self, scr):
        h, w = scr.getmaxyx()
        dh, dw = self.size(h, w)
        y, x = (h - dh) // 2, (w - dw) // 2
        win = scr.derwin(dh, dw, y, x)
        win.erase()
        win.box()
        put(win, 0, 2, f" {self.title} ", curses.A_BOLD)
        return win

    def draw(self, scr):
        raise NotImplementedError

    def key(self, k):
        raise NotImplementedError


class MessageDialog(Dialog):
    def __init__(self, title, lines):
        self.title = title
        self.source = lines
        self.lines = lines
        self.top = 0

    def size(self, h, w):
        width = min(w - 2, max(40, max((len(l) for l in self.source), default=0) + 4))
        self.lines = [part for line in self.source for part in wrap_hanging(line, width - 4)]
        return min(h - 2, len(self.lines) + 4), width

    def draw(self, scr):
        win = self.frame(scr)
        dh, dw = win.getmaxyx()
        rows = dh - 3
        self.top = max(0, min(self.top, len(self.lines) - rows))
        for i, line in enumerate(self.lines[self.top:self.top + rows]):
            put(win, 1 + i, 2, line, width=dw - 4)
        more = " Haut/Bas : défiler " if len(self.lines) > rows else ""
        put(win, dh - 1, 2, f"{more} Entrée/Échap : fermer ", color(C_INFO))

    def key(self, k):
        if k in (curses.KEY_UP, curses.KEY_PPAGE):
            self.top -= 1 if k == curses.KEY_UP else 10
        elif k in (curses.KEY_DOWN, curses.KEY_NPAGE):
            self.top += 1 if k == curses.KEY_DOWN else 10
        elif k in (ENTER, ESC, "q", "?"):
            return True
        return False


class ConfirmDialog(Dialog):
    """Lines, an optional checkbox, [ Oui ] [ Non ]. on_yes(checked)."""

    def __init__(self, title, lines, on_yes, checkbox=None, checked=False):
        self.title = title
        # commands keep their line; prose wraps to the dialog's width
        self.lines = [w for line in lines for w in (wrap(line, 70) if line and not line.startswith("  ") else [line])]
        self.on_yes = on_yes
        self.checkbox = checkbox
        self.checked = checked
        self.focus = 0              # 0 Oui, 1 Non, 2 checkbox

    def size(self, h, w):
        extra = 2 if self.checkbox else 0
        widest = max([len(l) for l in self.lines] + [len(self.checkbox or "") + 4])
        return min(h - 2, len(self.lines) + 4 + extra), min(w - 2, max(46, widest + 4))

    def draw(self, scr):
        win = self.frame(scr)
        dh, dw = win.getmaxyx()
        for i, line in enumerate(self.lines[:dh - 4 - (2 if self.checkbox else 0)]):
            put(win, 1 + i, 2, line, width=dw - 4)
        y = dh - 2
        if self.checkbox:
            mark = "[x]" if self.checked else "[ ]"
            put(win, y - 2, 2, f"{mark} {self.checkbox}", curses.A_REVERSE if self.focus == 2 else 0)
        put(win, y, 2, "[ Oui ]", curses.A_REVERSE if self.focus == 0 else 0)
        put(win, y, 11, "[ Non ]", curses.A_REVERSE if self.focus == 1 else 0)
        put(win, y, 20, "o/n, Tab, Espace : cocher" if self.checkbox else "o/n, Tab", color(C_INFO))

    def key(self, k):
        if k in ("o", "O", "y", "Y"):
            self.on_yes(self.checked)
            return True
        if k in ("n", "N", ESC):
            return True
        if k in (TAB, curses.KEY_RIGHT, curses.KEY_DOWN):
            self.focus = (self.focus + 1) % (3 if self.checkbox else 2)
        elif k in (BTAB, curses.KEY_LEFT, curses.KEY_UP):
            self.focus = (self.focus - 1) % (3 if self.checkbox else 2)
        elif k == " " and self.checkbox:
            self.checked = not self.checked
        elif k == ENTER:
            if self.focus == 2:
                self.checked = not self.checked
                return False
            if self.focus == 0:
                self.on_yes(self.checked)
            return True
        return False


class FormDialog(Dialog):
    """Several labelled fields. fields: (key, label, initial, help, mask). on_ok(values) returns an error text or
    None; with an error the dialog stays open and shows it."""

    cursor = True

    def __init__(self, title, fields, on_ok, intro=None):
        self.title = title
        self.fields = fields
        self.edits = [LineEdit(str(initial), mask=mask) for _, _, initial, _, mask in fields]
        self.on_ok = on_ok
        self.intro = [part for line in intro or [] for part in wrap(line, 70)]
        self.focus = 0
        self.error = ""

    def size(self, h, w):
        return min(h - 2, len(self.intro) + (1 if self.intro else 0) + len(self.fields) * 2 + 4), min(w - 2, 76)

    def draw(self, scr):
        win = self.frame(scr)
        dh, dw = win.getmaxyx()
        y = 1
        for line in self.intro:
            put(win, y, 2, line, width=dw - 4)
            y += 1
        if self.intro:
            y += 1
        label_w = max(len(f[1]) for f in self.fields) + 2
        focus_pos = None
        for i, (key, label, _, help_text, _) in enumerate(self.fields):
            put(win, y, 2, label, curses.A_BOLD if i == self.focus else 0)
            if i == self.focus:
                focus_pos = (y, 2 + label_w)
            else:
                self.edits[i].draw(win, y, 2 + label_w, dw - label_w - 5, focused=False)
            put(win, y + 1, 2 + label_w, help_text, color(C_DIM), width=dw - label_w - 5)
            y += 2
        put(win, dh - 2, 2, self.error, color(C_ERR), width=dw - 4)
        put(win, dh - 1, 2, " Entrée : valider  Tab/Haut/Bas : champ  Échap : annuler ", color(C_INFO))
        if focus_pos:
            self.edits[self.focus].draw(win, focus_pos[0], focus_pos[1], dw - label_w - 5)

    def key(self, k):
        if k == ESC:
            return True
        if k in (TAB, curses.KEY_DOWN):
            self.focus = (self.focus + 1) % len(self.fields)
        elif k in (BTAB, curses.KEY_UP):
            self.focus = (self.focus - 1) % len(self.fields)
        elif k == ENTER:
            values = {f[0]: e.text for f, e in zip(self.fields, self.edits)}
            self.error = self.on_ok(values) or ""
            return not self.error
        else:
            self.edits[self.focus].key(k)
        return False


class InputDialog(FormDialog):
    def __init__(self, title, label, initial, help_text, on_ok, mask=False, intro=None):
        super().__init__(title, [("value", label, initial, help_text, mask)], lambda v: on_ok(v["value"]), intro)


class ChoiceDialog(Dialog):
    """A list to pick one item from. items: (value, label, description)."""

    def __init__(self, title, items, on_pick, current=None):
        self.title = title
        self.items = items
        self.on_pick = on_pick
        self.sel = next((i for i, it in enumerate(items) if it[0] == current), 0)

    def size(self, h, w):
        return min(h - 2, len(self.items) + 7), min(w - 2, 76)

    def draw(self, scr):
        win = self.frame(scr)
        dh, dw = win.getmaxyx()
        rows = dh - 6
        top = max(0, self.sel - rows + 1)
        for i, (value, label, _) in enumerate(self.items[top:top + rows]):
            n = top + i
            put(win, 1 + i, 2, f" {label} ".ljust(dw - 4), curses.A_REVERSE if n == self.sel else 0)
        desc = self.items[self.sel][2] if self.items else ""
        for i, line in enumerate(wrap(desc, dw - 4)[:2]):
            put(win, dh - 4 + i, 2, line, color(C_DIM), width=dw - 4)
        put(win, dh - 1, 2, " Entrée : choisir  Échap : annuler ", color(C_INFO))

    def key(self, k):
        if k == ESC:
            return True
        if k == curses.KEY_UP:
            self.sel = (self.sel - 1) % len(self.items)
        elif k == curses.KEY_DOWN:
            self.sel = (self.sel + 1) % len(self.items)
        elif k == ENTER:
            self.on_pick(self.items[self.sel][0])
            return True
        return False


class ChecklistDialog(Dialog):
    """Items to tick. items: (value, label). on_ok(list of ticked values)."""

    def __init__(self, title, items, ticked, on_ok, intro=None):
        self.title = title
        self.items = list(items)
        self.ticked = set(ticked)
        self.on_ok = on_ok
        self.intro = intro or []
        self.sel = 0
        # values the list does not have (a mod typed for a remote server) stay ticked
        for v in sorted(self.ticked - {i[0] for i in self.items}):
            self.items.append((v, v))

    def size(self, h, w):
        return min(h - 2, len(self.items) + len(self.intro) + 5), min(w - 2, 76)

    def draw(self, scr):
        win = self.frame(scr)
        dh, dw = win.getmaxyx()
        y = 1
        for line in self.intro:
            put(win, y, 2, line, width=dw - 4)
            y += 1
        rows = dh - y - 2
        top = max(0, self.sel - rows + 1)
        for i, (value, label) in enumerate(self.items[top:top + rows]):
            n = top + i
            mark = "[x]" if value in self.ticked else "[ ]"
            put(win, y + i, 2, f" {mark} {label} ".ljust(dw - 4), curses.A_REVERSE if n == self.sel else 0)
        put(win, dh - 1, 2, " Espace : cocher  Entrée : valider  Échap : annuler ", color(C_INFO))

    def key(self, k):
        if k == ESC:
            return True
        if not self.items:
            if k == ENTER:
                self.on_ok([])
                return True
            return False
        if k == curses.KEY_UP:
            self.sel = (self.sel - 1) % len(self.items)
        elif k == curses.KEY_DOWN:
            self.sel = (self.sel + 1) % len(self.items)
        elif k == " ":
            v = self.items[self.sel][0]
            self.ticked.symmetric_difference_update({v})
        elif k == ENTER:
            self.on_ok([v for v, _ in self.items if v in self.ticked])
            return True
        return False
