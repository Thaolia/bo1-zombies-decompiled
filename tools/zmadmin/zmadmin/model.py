"""Parsers for the server's console replies, and the player table they feed."""
import re

from .protocol import strip_colors

GAME_STATES = ("playing", "down", "dead", "spectator", "intermission", "loading")
ADMIN_FLAGS = ("god", "demigod", "notarget", "noclip", "frozen")

_ADMIN_HEAD = re.compile(r'zm_admin: start points (-?\d+), start perks "(.*)", join in progress (\S*), fs_mods "(.*)"')


class AdminPlayer:
    def __init__(self, slot, name, state, health, flags):
        self.slot = slot
        self.name = name
        self.state = state
        self.health = health
        self.flags = flags


class AdminReport:
    def __init__(self, settings, players):
        self.settings = settings
        self.players = players


def parse_zm_admin(text):
    """zm_admin (src/server_mp/sv_zm_admin.cpp, ZmAdmin_f): a settings line, then per player
    '  %2d %-24s %-10s health %4d[ god][ demigod][ notarget][ noclip][ frozen]'. Returns None without the
    settings line (not a zm_admin reply)."""
    settings = None
    players = {}
    for line in strip_colors(text).splitlines():
        m = _ADMIN_HEAD.search(line)
        if m:
            settings = {"zm_startpoints": m.group(1), "zm_startperks": m.group(2), "zm_jip_mode": m.group(3),
                        "fs_mods": m.group(4)}
            continue
        left, sep, right = line.partition(" health ")
        if not sep or settings is None:
            continue
        head = left.split()
        if not head or not head[0].isdigit():
            continue
        slot = int(head[0])
        rest = left.strip()[len(head[0]):].strip()
        state = ""
        words = rest.rsplit(None, 1)
        if len(words) == 2 and words[1] in GAME_STATES:
            rest, state = words
        elif rest in GAME_STATES:
            rest, state = "", rest
        tail = right.split()
        if not tail or not tail[0].lstrip("-").isdigit():
            continue
        players[slot] = AdminPlayer(slot, rest.strip(), state, int(tail[0]),
                                    [f for f in tail[1:] if f in ADMIN_FLAGS])
    if settings is None:
        return None
    return AdminReport(settings, players)


def parse_dvars(text):
    """Every '"name" is: "value^7" default: "value^7"' line (Dvar_Command, src/qcommon/dvar_cmds.cpp) of a reply,
    as {name: value}. A dvar with a latched value maps to it (what the next map load uses)."""
    values = {}
    last = None
    for line in text.splitlines():
        m = re.match(r'"([^"]+)" is: "(.*)\^7" default: "(.*)\^7"\s*$', line)
        if m:
            last = m.group(1)
            values[last] = strip_colors(m.group(2))
            continue
        m = re.match(r'latched: "(.*)"\s*$', line)
        if m and last:
            values[last] = strip_colors(m.group(1)).removesuffix("^7")
    return values


class PlayerRow:
    """One slot as the views show it: getstatus gives the connection, zm_admin the game state."""

    def __init__(self, slot):
        self.slot = slot
        self.name = ""
        self.score = 0
        self.ping = 0
        self.connection = ""
        self.state = ""
        self.health = None
        self.flags = []

    @property
    def display_state(self):
        if self.connection and self.connection != "active":
            return self.connection
        return self.state or self.connection


def merge_players(status, report):
    rows = {}
    if status is not None:
        for p in status.players:
            row = rows.setdefault(p.slot, PlayerRow(p.slot))
            row.name, row.score, row.ping, row.connection = p.name, p.score, p.ping, p.state_name
    if report is not None:
        for p in report.players.values():
            # zm_admin is polled less often: a slot getstatus no longer lists has left
            if status is not None and p.slot not in rows:
                continue
            row = rows.setdefault(p.slot, PlayerRow(p.slot))
            row.name = row.name or p.name
            row.state, row.health, row.flags = p.state, p.health, p.flags
    return [rows[k] for k in sorted(rows)]
