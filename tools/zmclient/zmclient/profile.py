"""ZmClient's profile (what to play and how) and the client command line it gives.

The solo part follows tools/mod-launcher.hta's args() rule for rule (horde, mapkit, nightmare, zinfo, noperks, quick
restart); the base and the join part follow Play.bat / play.sh (tools/make_dist.sh) and the Switch launcher
(src/platform/switch/launcher_config.cpp).
"""
import copy
import functools
import json
import os
import re
import time

from .modflags import horde_flags, nightmare_flags

MODES = ("solo", "join", "host")
WINDOW_MODES = ("borderless", "window", "fullscreen")
DEFAULT_PORT = 3074

# (bsp, label, horde's napalm/shrieker spawner files exist: mods/horde maps/<map>.zombie_temple.ents)
BASE_MAPS = [
    ("zombie_theater", "Kino der Toten", True),
    ("zombie_pentagon", "Five", True),
    ("zombie_cosmodrome", "Ascension", False),
    ("zombie_coast", "Call of the Dead", False),
    ("zombie_temple", "Shangri-La", False),
    ("zombie_moon", "Moon", False),
    ("zombie_cod5_prototype", "Nacht der Untoten", False),
    ("zombie_cod5_asylum", "Verrückt", False),
    ("zombie_cod5_sumpf", "Shi No Numa", False),
    ("zombie_cod5_factory", "Der Riese", False),
    ("zombietron", "Dead Ops Arcade", False),
]
MAP_LABELS = {bsp: label for bsp, label, _ in BASE_MAPS}
# the labels tools/mod-launcher.hta gives its mapkit maps; other layouts show their file name
MAPKIT_LABELS = {
    "mapkit_test": "Mapkit test (Five + une salle construite)",
    "mapkit_room": "Mapkit room (entièrement construite)",
    "relay_station": "Relay Station (trois salles, quatre fenêtres)",
    "sluice_works": "Sluice Works (boucle de quatre zones)",
    "aldric_sanatorium": "Aldric Sanatorium (deux étages, power, Pack-a-Punch)",
    # docs/BO2_MAPS.md: the player's own extraction of Black Ops II's Nuketown, which make_dist.sh --bo2 packages
    "bo2_zm_nuked": "Nuketown (Black Ops II)",
    "bo2_zm_transit": "TranZit (Black Ops II)",
    # the same Nuketown on a world of its own BSP (tools/bo2/bo2_bsp.py): BO2's props, lightmaps and sky
    "bo2_zm_nuked_bsp": "Nuketown native (Black Ops II, monde BSP)",
}
# the zones a layout's assets come from beside Five's (bo1_mod_zones): TranZit's Stamin-Up machine and bottle are
# Ascension's (_mapkit.gsc mapkit_marathon_perk)
MAPKIT_ZONES = {"bo2_zm_transit": "zombie_cosmodrome"}
# a layout played on a world of its own BSP (docs/BO2_MAPS.md, "Props, gameplay and lighting on the native world"):
# its zone usermaps/<zone>/<zone>.ff (bo1_mod_zones) and its world main/maps/<zone>.d3dbsp (bo1_bspworld)
MAPKIT_BSP_WORLDS = {"bo2_zm_nuked_bsp": "zombie_bo2nuked"}
NIGHTMARE_MAPS = ("zombie_pentagon", "zombie_theater")
AIM_DVARS = (("aim_assist", "bo1_aimassist"), ("aim_slowdown", "aim_slowdown_enabled"),
             ("aim_lockon", "aim_lockon_enabled"), ("aim_autoaim", "aim_autoaim_enabled"))
MAX_COMMAND_LINES = 63      # Com_ParseCommandLine keeps 64 lines, the first being the text before the first '+'
# r_mod_sceneModels: the models the renderer draws per frame (retail 1024, the engine's cap 4096). A map imported by
# MapKit (Nuketown: 2 096 props) needs more; each model past retail's also takes a model lighting entry
SCENE_MODEL_CHOICES = (1024, 2048, 4096)
MODEL_LIGHTING_ENTRIES_MAX = 5120   # r_mod_modelLightingEntries' cap (r_model_lighting.cpp)
# The start kit of a solo game, given by mods/admin when the player first spawns (zm_startpoints, zm_startperks): 0
# points keeps the game's own 500. The perk names are those _admin.gsc admin_perk_name takes, in its order; a perk
# without a machine on the map is not given.
START_POINT_CHOICES = (0, 1000, 2500, 5000, 10000, 25000, 50000, 100000)
# mods/bo2weapons (docs/BO2_WEAPONS.md): the player's own Black Ops II weapons, in mods/bo2weapons/mod.ff, which the
# engine loads when fs_mods names the mod (DB_FindModZone). Choice 0 is off; 1 to 3 are the script's bo2weapons_box 0
# to 2. A client joining a server that has them loads the same mod.ff (join_bo2_weapons), or the weapons' indices do not
# match: the game goes back to the menu on entering, naming the missing weapon (cg_weapons.cpp).
BO2_WEAPON_CHOICES = ((0, "non"), (1, "dans la boîte, avec celles de la map"),
                      (2, "seules dans la boîte, + armes spéciales de la map"), (3, "seules dans la boîte"))
START_PERKS = (("jugg", "Juggernog"), ("revive", "Quick Revive"), ("speed", "Speed Cola"), ("doubletap", "Double Tap"),
               ("staminup", "Stamin-Up"), ("phd", "PhD Flopper"), ("deadshot", "Deadshot Daiquiri"),
               ("mule", "Mule Kick"))

DEFAULTS = {
    "mode": "solo",
    "map": "zombie_theater",            # a bsp, or "mapkit:<layout>"
    "god": False,
    "zinfo": True,
    "noperks": False,
    "quickrestart": True,
    "start_points": 0,                  # one of START_POINT_CHOICES
    "start_perks": [],                  # names of START_PERKS
    "start_power": False,               # mods/admin's zm_startpower: the power switch used at the start
    "horde": False,
    "horde_max": 128,
    "horde_round": 1,
    "horde_napalm": True,
    "horde_sonic": True,
    "horde_spawn_all": False,
    "horde_unjam": True,
    "horde_clear_round": False,
    "difficulty": 0,                    # 0 Normal, 1 Nightmare, 2 Hell
    "bo2_weapons": 0,                   # one of BO2_WEAPON_CHOICES
    "name": "",
    "window": "borderless",
    "window_size": "1280x720",
    "fps_counter": False,
    "scene_models": 1024,              # one of SCENE_MODEL_CHOICES
    # aim assist (gamepad): "" leaves the game's value, "1" / "0" sets it
    "aim_assist": "",
    "aim_slowdown": "",
    "aim_lockon": "",
    "aim_autoaim": "",
    "join_host": "",
    "join_port": DEFAULT_PORT,
    "join_password": "",                # the server's g_password, not rcon
    "host_map": "zombie_theater",
    "host_port": DEFAULT_PORT,
    "host_bo2_weapons": 0,              # the local server's BO2 weapons, one of BO2_WEAPON_CHOICES
    "join_bo2_weapons": False,          # load mods/bo2weapons/mod.ff to join a server that has the BO2 weapons
    "extra": [],                        # {"on": bool, "text": "set r_foo 1"}: one + command each
    "favorites": [],                    # {"name", "host", "port", "password"}
    "steam_install": "",                # the Black Ops install the Installation view copies from
    "setup_video": True,
    "setup_convert": True,
}

_NAME = re.compile(r"^[A-Za-z0-9_.\-]{1,15}$")
_HOST = re.compile(r"^[A-Za-z0-9.\-]{1,63}$")
_SIZE = re.compile(r"^(\d{3,5})x(\d{3,5})$")
_TOKEN = re.compile(r'^[^\s";+]+$')


def valid_name(name):
    """The Switch launcher's rule (Launcher_ValidPlayerName): 1-15 letters, digits, '_', '-' or '.'."""
    return bool(_NAME.match(name))


def valid_host(host):
    """Launcher_ValidServerHost: an IPv4 address or a host name, 1-63 letters, digits, '.' or '-'."""
    return bool(_HOST.match(host))


def valid_password(password):
    """No blank, quote, ';' or '+': the command line is cut at every '+' (Com_ParseCommandLine)."""
    return bool(_TOKEN.match(password))


def parse_size(text):
    m = _SIZE.match(text.strip())
    if not m:
        return None
    w, h = int(m.group(1)), int(m.group(2))
    return (w, h) if 320 <= w <= 16384 and 200 <= h <= 16384 else None


def check_extra(text):
    """None when one free command line is usable, else why not."""
    text = text.strip()
    if not text:
        return "la ligne est vide"
    if "+" in text:
        return "pas de « + » : le jeu coupe sa ligne de commande à chaque « + »"
    if '"' in text or "\n" in text:
        return "pas de guillemet ni de retour à la ligne"
    return None


class Profile:
    def __init__(self, path=None):
        self.path = path
        self.data = copy.deepcopy(DEFAULTS)
        if path:
            self.load()

    def __getitem__(self, key):
        return self.data[key]

    def __setitem__(self, key, value):
        self.data[key] = value

    def load(self):
        try:
            with open(self.path, encoding="utf-8") as f:
                saved = json.load(f)
        except (OSError, ValueError):
            return
        if not isinstance(saved, dict):
            return
        # an unknown key or a value of the wrong type keeps the default: a file of another version still loads
        for key, default in DEFAULTS.items():
            value = saved.get(key)
            if value is not None and type(value) is type(default):
                self.data[key] = value
        if self.data["mode"] not in MODES:
            self.data["mode"] = DEFAULTS["mode"]
        if self.data["window"] not in WINDOW_MODES:
            self.data["window"] = DEFAULTS["window"]
        if self.data["scene_models"] not in SCENE_MODEL_CHOICES:
            self.data["scene_models"] = DEFAULTS["scene_models"]
        for key in ("bo2_weapons", "host_bo2_weapons"):
            if self.data[key] not in dict(BO2_WEAPON_CHOICES):
                self.data[key] = 0
        if self.data["start_points"] not in START_POINT_CHOICES:
            self.data["start_points"] = DEFAULTS["start_points"]
        self.data["start_perks"] = [k for k, _ in START_PERKS if k in self.data["start_perks"]]
        self.data["extra"] = [e for e in self.data["extra"] if isinstance(e, dict) and isinstance(e.get("text"), str)]
        self.data["favorites"] = [f for f in self.data["favorites"] if isinstance(f, dict) and f.get("host")]

    def save(self):
        if not self.path:
            return
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        tmp = self.path + ".tmp"
        # server passwords inside: readable by the user only
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(self.data, f, indent=2, ensure_ascii=False)
        os.replace(tmp, self.path)


# ------------------------------------------------------------------------------------------------------------ maps
@functools.lru_cache(maxsize=8)
def mapkit_layouts(dist_root):
    """(layout, base bsp) of mods/mapkit/layouts/*.json that name a base map. Read once per folder: the views ask at
    every frame."""
    folder = os.path.join(dist_root, "mods", "mapkit", "layouts") if dist_root else None
    out = []
    try:
        names = sorted(os.listdir(folder)) if folder else []
    except OSError:
        names = []
    for n in names:
        if not n.endswith(".json"):
            continue
        try:
            with open(os.path.join(folder, n), encoding="utf-8") as f:
                base = json.load(f).get("base")
        except (OSError, ValueError, AttributeError):
            continue
        if isinstance(base, str) and base in MAP_LABELS:
            out.append((n[:-len(".json")], base))
    known = [l for l in out if l[0] in MAPKIT_LABELS]
    return tuple(sorted(known, key=lambda l: list(MAPKIT_LABELS).index(l[0]))
                 + [l for l in out if l[0] not in MAPKIT_LABELS])


def bsp_world_files(layout, dist_root):
    """The files a BSP-world layout needs that the package lacks (empty for any other layout)."""
    zone = MAPKIT_BSP_WORLDS.get(layout)
    if not zone:
        return []
    need = (os.path.join("usermaps", zone, zone + ".ff"), os.path.join("main", "maps", zone + ".d3dbsp"))
    return [f for f in need if not (dist_root and os.path.isfile(os.path.join(dist_root, f)))]


def solo_maps(dist_root):
    """(value, label, bsp, layout or None) of every map the Solo view offers."""
    maps = [(bsp, label, bsp, None) for bsp, label, _ in BASE_MAPS]
    for layout, base in mapkit_layouts(dist_root):
        if bsp_world_files(layout, dist_root):
            continue
        label = MAPKIT_LABELS.get(layout, layout)
        maps.append((f"mapkit:{layout}", f"Mapkit : {label}", base, layout))
    return maps


def resolve_map(value, dist_root=None):
    """(bsp, layout or None) of a profile's map value."""
    if value.startswith("mapkit:"):
        layout = value[len("mapkit:"):]
        for name, base in mapkit_layouts(dist_root):
            if name == layout:
                return base, layout
        return "zombie_pentagon", layout     # the .hta's base for every kit map
    return value, None


_installed = {}


def installed_maps(dist_root):
    """The base maps whose zone is in zone/<any folder>/<map>.ff, or None when the folder has no zone/ at all (an
    install without the DLCs converts only Kino, Five and Dead Ops). Cached 5 s: the views ask at every frame."""
    now = time.monotonic()
    hit = _installed.get(dist_root)
    if hit and now - hit[0] < 5:
        return hit[1]
    zone = os.path.join(dist_root, "zone") if dist_root else ""
    try:
        folders = [os.path.join(zone, d) for d in os.listdir(zone)]
    except OSError:
        result = None
    else:
        names = set()
        for folder in folders:
            try:
                names |= {n[:-3].lower() for n in os.listdir(folder) if n.lower().endswith(".ff")}
            except OSError:
                pass
        result = frozenset(b for b, _, _ in BASE_MAPS if b in names)
    _installed[dist_root] = (now, result)
    return result


def map_installed(bsp, dist_root):
    maps = installed_maps(dist_root)
    return maps is None or bsp in maps


def map_label(value, dist_root=None):
    for v, label, _, _ in solo_maps(dist_root):
        if v == value:
            return label
    return MAP_LABELS.get(value, value)


def specials_available(bsp, layout):
    return not layout and any(b == bsp and s for b, _, s in BASE_MAPS)


def nightmare_available(bsp, layout):
    return not layout and bsp in NIGHTMARE_MAPS


def bo2_weapons_installed(dist_root):
    """mods/bo2weapons/mod.ff is there: the player built it from their own Black Ops II (docs/BO2_WEAPONS.md)."""
    return bool(dist_root) and os.path.isfile(os.path.join(dist_root, "mods", "bo2weapons", "mod.ff"))


def bo2_weapons_available(bsp):
    """Dead Ops Arcade has no box."""
    return bsp != "zombietron"


def add_bo2_weapons(argv, choice=None):
    """Stacks mods/bo2weapons on the line's fs_mods, or adds one, naming fs_game's mod first as the engine stacks it
    (fs_mods must name bo2weapons: an empty fs_mods runs no mod's hooks). choice: the solo box choice (1 to 3), which
    sets bo2weapons_box; None for a client of a server, whose server decides."""
    for i in range(1, len(argv)):
        if argv[i] == "fs_mods" and argv[i - 1] == "+set":
            j = i + 1
            while j < len(argv) and not argv[j].startswith("+"):
                j += 1
            if "bo2weapons" not in argv[i + 1:j]:
                argv.insert(j, "bo2weapons")
            break
    else:
        game = next((argv[i + 2] for i in range(len(argv) - 2) if argv[i] == "+set" and argv[i + 1] == "fs_game"), "")
        argv += ["+set", "fs_mods"] + ([game[len("mods/"):]] if game.startswith("mods/") else []) + ["bo2weapons"]
    if choice:
        argv += ["+set", "bo2weapons_box", str(choice - 1)]


# ---------------------------------------------------------------------------------------------------- command line
def solo_mod_flags(p, dist_root=None):
    """The mod part of mod-launcher.hta's args(), from the map's flags to the quick restart."""
    bsp, layout = resolve_map(p["map"], dist_root)
    a = ""
    horde_opts = dict(n=p["horde_max"], round_=p["horde_round"], zinfo=p["zinfo"], spawn_all=p["horde_spawn_all"],
                      unjam=p["horde_unjam"], noperks=p["noperks"], carryover=not p["horde_clear_round"])
    if layout:
        if p["horde"]:
            a += " " + horde_flags(**horde_opts).replace("+set fs_mods ", "+set fs_mods mapkit ", 1)
            a += " +set bo1_mod_mapkit " + layout
        else:
            a += " +set fs_game mods/mapkit +set bo1_mod_mapkit " + layout
            if p["zinfo"] or p["noperks"]:
                a += (" +set fs_mods mapkit" + (" zinfo" if p["zinfo"] else "")
                      + (" noperks +set noperks 1" if p["noperks"] else ""))
        zones = [z for z in (MAPKIT_ZONES.get(layout), MAPKIT_BSP_WORLDS.get(layout)) if z]
        if zones:
            a += " +set bo1_mod_zones " + " ".join(zones)
        if layout in MAPKIT_BSP_WORLDS:
            a += f" +set bo1_bspworld maps/{MAPKIT_BSP_WORLDS[layout]}.d3dbsp"
    elif p["horde"]:
        specials = specials_available(bsp, layout)
        a += " " + horde_flags(**horde_opts, napalm=p["horde_napalm"] and specials, sonic=p["horde_sonic"] and specials)
    elif p["zinfo"] or p["noperks"]:
        a += " +set fs_mods" + (" zinfo" if p["zinfo"] else "") + (" noperks +set noperks 1" if p["noperks"] else "")
    if nightmare_available(bsp, layout):
        a = nightmare_flags(p["difficulty"], a, bsp)
    if p["quickrestart"]:
        a += " +set bo1_mod_quickrestart 1"
    argv = a.split()
    if p["start_points"] or p["start_perks"] or p["start_power"]:
        add_admin_mod(argv, bool(layout) and not p["horde"])
        if p["start_power"]:
            argv += ["+set", "zm_startpower", "1"]
        if p["start_points"]:
            argv += ["+set", "zm_startpoints", str(p["start_points"])]
        if p["start_perks"]:
            argv += ["+set", "zm_startperks", ",".join(k for k, _ in START_PERKS if k in p["start_perks"])]
    if p["bo2_weapons"] and bo2_weapons_available(bsp):
        add_bo2_weapons(argv, p["bo2_weapons"])
    return argv


def add_admin_mod(argv, mapkit):
    """Stacks mods/admin (the start kit's script) on the line's fs_mods, or adds one: "mapkit admin" on a MapKit map
    without other mods, whose line only sets fs_game."""
    for i in range(1, len(argv)):
        if argv[i] == "fs_mods" and argv[i - 1] == "+set":
            j = i + 1
            while j < len(argv) and not argv[j].startswith("+"):
                j += 1
            if "admin" not in argv[i + 1:j]:
                argv.insert(j, "admin")
            return
    argv += ["+set", "fs_mods"] + (["mapkit", "admin"] if mapkit else ["admin"])


def fs_game(p, dist_root=None):
    """The fs_game the solo line sets ('' for none): where the client's console_mp.log goes."""
    if p["mode"] != "solo":
        return ""
    words = solo_mod_flags(p, dist_root)
    for i in range(len(words) - 2):
        if words[i] == "+set" and words[i + 1] == "fs_game":
            return words[i + 2]
    return ""


def window_flags(p):
    if p["window"] == "fullscreen":
        return ["+set", "r_fullscreen", "1", "+set", "r_borderless", "0"]
    if p["window"] == "window":
        size = parse_size(p["window_size"]) or (1280, 720)
        return ["+set", "r_fullscreen", "0", "+set", "r_borderless", "0", "+set", "r_customMode",
                f"{size[0]}x{size[1]}", "+set", "vid_xpos", "60", "+set", "vid_ypos", "60"]
    return ["+set", "r_fullscreen", "0", "+set", "r_borderless", "1", "+set", "vid_xpos", "0", "+set", "vid_ypos", "0"]


def add_scene_models(argv, count):
    """Raises the renderer's models per frame to count: r_mod_sceneModels, and a model lighting pool of count + 1024
    (the retail pool, which DObjs and dyn models share; measured on Nuketown at 4096: none refused). A larger pool the
    line already asks for (the horde's past 300 zombies) is kept."""
    if count <= 1024:
        return
    entries = min(MODEL_LIGHTING_ENTRIES_MAX, count + 1024)
    for i in range(len(argv) - 2):
        if argv[i] == "+set" and argv[i + 1] == "r_mod_modelLightingEntries":
            argv[i + 2] = str(max(int(argv[i + 2]), entries))
            break
    else:
        argv += ["+set", "r_mod_modelLightingEntries", str(entries)]
    argv += ["+set", "r_mod_sceneModels", str(count)]


def server_address(p):
    if p["mode"] == "host":
        return f"127.0.0.1:{p['host_port']}"
    return f"{p['join_host']}:{p['join_port']}"


def wants_bo2_weapons(p, dist_root=None):
    """The client loads mods/bo2weapons/mod.ff: solo with the BO2 weapons, or a client of a server that has them."""
    if p["mode"] == "solo":
        return bool(p["bo2_weapons"]) and bo2_weapons_available(resolve_map(p["map"], dist_root)[0])
    return bool(p["host_bo2_weapons"]) if p["mode"] == "host" else p["join_bo2_weapons"]


def problems(p, dist_root=None):
    """Why the profile cannot be launched as it is (empty when it can)."""
    out = []
    if p["name"] and not valid_name(p["name"]):
        out.append("nom du joueur : 1 à 15 lettres, chiffres, « _ », « - » ou « . »")
    if p["mode"] == "join":
        if not valid_host(p["join_host"]):
            out.append("adresse du serveur absente ou invalide (vue Serveurs, F2)")
        if not 1 <= p["join_port"] <= 65535:
            out.append("port du serveur hors de 1-65535")
        if p["join_password"] and not valid_password(p["join_password"]):
            out.append("mot de passe du serveur : ni espace, ni guillemet, ni « ; », ni « + »")
    if p["mode"] == "host" and not 1 <= p["host_port"] <= 65535:
        out.append("port du serveur local hors de 1-65535")
    if p["mode"] in ("solo", "host"):
        bsp = resolve_map(p["map"], dist_root)[0] if p["mode"] == "solo" else p["host_map"]
        if not map_installed(bsp, dist_root):
            out.append(f"{MAP_LABELS.get(bsp, bsp)} n'est pas installée (pas de zone {bsp}.ff : votre Black Ops "
                       "n'a pas ce DLC)")
    if p["mode"] == "solo":
        layout = resolve_map(p["map"], dist_root)[1]
        missing = bsp_world_files(layout, dist_root) if layout else []
        if missing:
            out.append(f"{MAPKIT_LABELS.get(layout, layout)} : il manque {', '.join(missing)} (votre extraction de "
                       "Black Ops II, docs/BO2_MAPS.md)")
    if wants_bo2_weapons(p, dist_root) and not bo2_weapons_installed(dist_root):
        out.append("armes de Black Ops II : pas de mods/bo2weapons/mod.ff (à construire depuis votre Black Ops II, "
                   "docs/BO2_WEAPONS.md)")
    if p["window"] == "window" and not parse_size(p["window_size"]):
        out.append("taille de fenêtre : LARGEURxHAUTEUR, ex. 1280x720")
    for e in p["extra"]:
        if e.get("on"):
            why = check_extra(e["text"])
            if why:
                out.append(f"dvar libre « {e['text']} » : {why}")
    if dist_root and "+" in dist_root:
        out.append("le chemin du dossier du jeu contient un « + » : le jeu couperait sa ligne de commande")
    return out


def build_command(p, exe, dist_root, launch_cfg=""):
    """The client's argv. launch_cfg: the text of the launch.cfg next to the program, which the engine puts before
    the command line; it only counts towards the 63-command limit here. Raises ValueError when the line is unusable."""
    issues = problems(p, dist_root)
    if issues:
        raise ValueError(issues[0])
    argv = [exe, "+set", "bo1_zombies", "1", "+set", "fs_b", dist_root, "+set", "fs_h", dist_root,
            "+set", "logfile", "2", "+set", "bo1_mod_sharedconfig", "1"]
    if p["name"]:
        argv += ["+set", "name", p["name"]]
    if p["mode"] == "solo":
        # a listen server bound to loopback, as Play.bat: no firewall question and nobody else can join
        argv += ["+set", "net_ip", "127.0.0.1"]
        if p["god"]:
            # not the `god` command, which ClientSpawn clears: this dvar puts the bit back on every playing client
            argv += ["+set", "bo1_testclient_god", "1"]
        argv += solo_mod_flags(p, dist_root)
    elif wants_bo2_weapons(p, dist_root):
        add_bo2_weapons(argv)
    argv += window_flags(p)
    add_scene_models(argv, p["scene_models"])
    if p["fps_counter"]:
        argv += ["+set", "bo1_fpscounter", "1"]
    for key, dvar in AIM_DVARS:
        if p[key] in ("0", "1"):
            argv += ["+set", dvar, p[key]]
    if p["mode"] == "join" and p["join_password"]:
        argv += ["+set", "password", p["join_password"]]
    for e in p["extra"]:
        if e.get("on"):
            argv += ("+" + e["text"].strip()).split()
    if p["mode"] == "solo":
        bsp, _ = resolve_map(p["map"], dist_root)
        argv += ["+devmap", bsp]
    else:
        # cl_autoConnect, not +connect: the menu's own load undoes a connect from the command line, and it retries
        # while the server is not up yet (a local server takes a while to load its map) or after it went away
        argv += ["+set", "cl_allowWanConnect", "1", "+set", "cl_autoConnect", server_address(p)]
    count = sum(1 for a in argv[1:] if a.startswith("+")) + launch_cfg.count("+")
    if count > MAX_COMMAND_LINES:
        raise ValueError(f"{count} commandes « + » : le jeu n'en lit que {MAX_COMMAND_LINES} (retirer des dvars libres)")
    return argv


def quote_command(argv):
    """argv as one line to show (the engine joins its arguments the same way, quoting those with blanks)."""
    return " ".join(f'"{a}"' if (not a or any(c in a for c in ' \t"')) else a for a in argv)
