"""The Settings view's form, without curses: what changed, the commands Save sends, and server.cfg."""
import re

from .help import PERKS

PENDING, APPLIED, REFUSED = "pending", "applied", "refused"
_SAFE_PASSWORD = re.compile(r"^[A-Za-z0-9_.-]+$")


class Setting:
    def __init__(self, name, kind, choices=None, minimum=None, maximum=None, reload=False):
        self.name = name
        self.kind = kind            # text, password, int, choice, bool, perks, mods
        self.choices = choices or []
        self.minimum = minimum
        self.maximum = maximum
        self.reload = reload        # applies at the next map load


SETTINGS = [
    Setting("sv_hostname", "text"),
    Setting("g_password", "text"),
    Setting("rcon_password", "password"),
    Setting("zm_startpoints", "int", minimum=0),
    Setting("zm_startperks", "perks", choices=[p for p, _ in PERKS]),
    Setting("zm_jip_mode", "choice", choices=["immediate", "nextround"]),
    Setting("zm_debug_revive", "bool"),
    Setting("g_inactivity", "int", minimum=0),
    Setting("sv_maxclients", "int", minimum=1, maximum=32, reload=True),
    Setting("fs_mods", "mods"),
]
BY_NAME = {s.name: s for s in SETTINGS}


def validate(setting, value):
    """The value normalised, or raise ValueError with the reason (shown to the user)."""
    value = value.strip()
    if '"' in value or ";" in value or "\n" in value:
        raise ValueError("pas de guillemet ni de point-virgule")
    if setting.kind == "int":
        if not re.fullmatch(r"-?\d+", value):
            raise ValueError("un nombre entier")
        n = int(value)
        if setting.minimum is not None and n < setting.minimum:
            raise ValueError(f"au moins {setting.minimum}")
        if setting.maximum is not None and n > setting.maximum:
            raise ValueError(f"au plus {setting.maximum}")
        return str(n)
    if setting.kind == "password":
        if not _SAFE_PASSWORD.match(value):
            raise ValueError("lettres, chiffres, - _ . seulement, et pas vide")
        return value
    if setting.kind == "bool":
        if value not in ("0", "1"):
            raise ValueError("0 ou 1")
        return value
    if setting.kind == "choice" and value not in setting.choices:
        raise ValueError("une de : " + ", ".join(setting.choices))
    if setting.kind in ("perks", "mods"):
        return " ".join(value.split())
    return value


def same(setting, a, b):
    if setting.kind in ("perks", "mods"):
        return sorted(a.split()) == sorted(b.split())
    return a == b


class Form:
    def __init__(self):
        self.server = {}            # values read from the server
        self.edits = {}             # name -> new value, not sent yet
        self.status = {}            # name -> (PENDING|APPLIED|REFUSED, server output)

    def load(self, values):
        self.server.update(values)

    def value(self, name):
        return self.edits.get(name, self.server.get(name, ""))

    def set(self, name, value):
        setting = BY_NAME[name]
        value = validate(setting, value)
        if name in self.server and same(setting, value, self.server[name]):
            self.edits.pop(name, None)
        else:
            self.edits[name] = value
        self.status.pop(name, None)

    def cancel(self):
        self.edits.clear()
        self.status.clear()

    @property
    def dirty(self):
        return bool(self.edits)

    def needs_reload(self):
        return any(BY_NAME[n].reload for n in self.edits)

    def commands(self, reset=False):
        """The rcon commands Save sends, in order: plain sets, rcon_password last of them (the reply to it still
        comes with the old password, the next command needs the new one), then the reloads."""
        cmds = []
        for s in SETTINGS:
            if s.name in self.edits and s.kind != "mods" and s.name != "rcon_password":
                cmds.append(f'set {s.name} "{self.edits[s.name]}"')
        if "rcon_password" in self.edits:
            cmds.append(f'set rcon_password "{self.edits["rcon_password"]}"')
        if "fs_mods" in self.edits:
            cmds.append(f'zm_mods "{self.edits["fs_mods"]}"')
        elif reset and self.needs_reload():
            cmds.append("zm_reset")
        return cmds

    def check(self, values, outputs=None, names=None):
        """Mark each edit (of `names`) applied or refused from the values read back after Save; applied edits
        leave `edits`."""
        for name in list(self.edits if names is None else names):
            setting = BY_NAME[name]
            got = values.get(name)
            # parse_dvars gives a latched value for a reload setting: equal means the next load uses it
            ok = got is not None and same(setting, got, self.edits[name])
            if got is not None:
                self.server[name] = got
            if ok:
                self.status[name] = (APPLIED, "")
                del self.edits[name]
            else:
                reason = f"le serveur garde « {got} »" if got is not None else "valeur non relue"
                self.status[name] = (REFUSED, (outputs or {}).get(name) or reason)


_CFG_LINE = re.compile(r'^\s*(set[as]?\s+)?([A-Za-z_][A-Za-z0-9_]*)(\s|$)')


def update_server_cfg(text, values):
    """server.cfg with each name of `values` set: an existing 'set name ...' line is rewritten in place (its first
    occurrence; later ones would override it, they are dropped); a missing one goes before the first map load
    (devmap/map lines), or at the end. Every other line is kept as it was."""
    lines = text.splitlines()
    written = set()
    out = []
    for line in lines:
        m = _CFG_LINE.match(line)
        name = m.group(2) if m else None
        if name in values:
            if name in written:
                continue
            out.append(f'set {name} "{values[name]}"')
            written.add(name)
            continue
        out.append(line)
    missing = [f'set {n} "{v}"' for n, v in values.items() if n not in written]
    if missing:
        at = next((i for i, l in enumerate(out) if re.match(r"^\s*(dev)?map\s", l)), len(out))
        out[at:at] = missing
    return "\n".join(out) + "\n"
