"""ZmClient's command line, mod flags, config.cfg editing, server browser, game-file setup and client process.

    python3 -m unittest discover -s tests/zmclient -v

The mod flags are compared with the JS they copy (tools/horde_flags.js, tools/nightmare_flags.js) and with
tools/mod-launcher.hta's own args(), run under node (hta_args.js); those tests are skipped without node.
"""
import itertools
import json
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "..", "tools")
sys.path.insert(0, os.path.join(TOOLS, "zmclient"))
sys.path.insert(0, os.path.join(TOOLS, "zmadmin"))

from zmclient import dirbrowser, gamesetup, profile as P  # noqa: E402
from zmclient.browser import Browser  # noqa: E402
from zmclient.cli import main as cli_main  # noqa: E402
from zmclient.client import Client  # noqa: E402
from zmclient.gamecfg import GameConfig, validate  # noqa: E402
from zmclient.modflags import horde_flags, nightmare_flags  # noqa: E402
from zmadmin.launcher import Dist, EXE_NAME  # noqa: E402

NODE = shutil.which("node")
OOB = b"\xff\xff\xff\xff"


def node(script, *args):
    return subprocess.run([NODE, "-e", script, *args], capture_output=True, text=True, check=True).stdout


def make_dist(root, exe_source=None):
    os.makedirs(root, exist_ok=True)
    exe = os.path.join(root, EXE_NAME)
    if exe_source:
        shutil.copy(exe_source, exe)
    else:
        open(exe, "w").close()
    open(os.path.join(root, "Play.bat" if os.name == "nt" else "play.sh"), "w").close()
    return Dist(root)


@unittest.skipUnless(NODE, "node not installed")
class ModFlagsParity(unittest.TestCase):
    def test_horde_matches_js(self):
        cases = []
        for n, rnd, fill in itertools.product([None, 10, 24, 100, 101, 150, 151, 300, 301, 899, 900, 1024, 2000],
                                              [None, 1, 2, 30], [None, 12.5, 30.0]):
            for flags in ([False] * 7, [True, False, True, False, True, False, True], [False, True, False, True,
                                                                                      False, True, False]):
                napalm, sonic, zinfo, spawn_all, noperks, unjam, carryover = flags
                cases.append(dict(n=n, round=rnd, fillRate=fill, napalm=napalm, sonic=sonic, zinfo=zinfo,
                                  spawnAll=spawn_all, noperks=noperks, unjam=unjam, carryover=carryover))
        js = json.loads(node(
            "const h = require(process.argv[1]); const cases = JSON.parse(process.argv[2]);"
            "console.log(JSON.stringify(cases.map(c => h(Object.assign({}, c, {n: c.n === null ? undefined : c.n,"
            " round: c.round === null ? undefined : c.round, fillRate: c.fillRate === null ? undefined : "
            "c.fillRate})))))", os.path.join(TOOLS, "horde_flags.js"), json.dumps(cases)))
        for c, expected in zip(cases, js):
            got = horde_flags(n=c["n"], round_=c["round"], napalm=c["napalm"], sonic=c["sonic"], zinfo=c["zinfo"],
                              spawn_all=c["spawnAll"], fill_rate=c["fillRate"], noperks=c["noperks"],
                              unjam=c["unjam"], carryover=c["carryover"])
            self.assertEqual(got, expected, c)

    def test_nightmare_matches_js(self):
        bases = ["", "+set fs_mods zinfo", horde_flags(n=128, zinfo=True),
                 horde_flags(n=400, napalm=True, sonic=True, zinfo=True),
                 "+set fs_game mods/mapkit +set fs_mods mapkit zinfo", "+set foo 1 +set bo1_mod_zones zombie_temple"]
        cases = [(lvl, a, m) for lvl in (0, 1, 2, 3) for a in bases for m in ("zombie_theater", "zombie_pentagon")]
        js = json.loads(node(
            "const f = require(process.argv[1]); console.log(JSON.stringify(JSON.parse(process.argv[2])"
            ".map(c => f(c[0], c[1], c[2]))))", os.path.join(TOOLS, "nightmare_flags.js"), json.dumps(cases)))
        for c, expected in zip(cases, js):
            self.assertEqual(nightmare_flags(*c), expected, c)

    def test_solo_flags_match_the_hta(self):
        """tools/mod-launcher.hta's refresh() + args() under node against solo_mod_flags, map by map."""
        combos = []
        for horde, zinfo, noperks, quick, napalm, diff in [(False, True, False, True, True, 0),
                                                           (False, False, True, False, True, 0),
                                                           (True, True, False, True, True, 0),
                                                           (True, False, True, False, False, 2),
                                                           (True, True, True, True, True, 1),
                                                           (False, True, False, True, True, 1)]:
            combos.append(dict(horde=horde, zinfo=zinfo, noperks=noperks, quickrestart=quick, napalm=napalm,
                               sonic=not napalm, difficulty=diff, maxz="400", round="5", spawnall=horde,
                               unjam=not noperks, clearround=noperks))
        with tempfile.TemporaryDirectory() as tmp:
            probe = os.path.join(tmp, "probe.json")
            with open(probe, "w") as f:
                json.dump([{"map": "0", "ids": {}}], f)
            maps = json.loads(subprocess.run([NODE, os.path.join(HERE, "hta_args.js"), probe], capture_output=True,
                                             text=True, check=True).stdout)["maps"]
            cases = []
            for i in range(len(maps)):
                for c in combos:
                    ids = {"horde": c["horde"], "zinfo": c["zinfo"], "noperks": c["noperks"],
                           "quickrestart": c["quickrestart"], "napalm": c["napalm"], "sonic": c["sonic"],
                           "difficulty": str(c["difficulty"]), "maxz": c["maxz"], "round": c["round"],
                           "spawnall": c["spawnall"], "unjam": c["unjam"], "clearround": c["clearround"]}
                    cases.append({"map": str(i), "ids": ids, "combo": c})
            with open(probe, "w") as f:
                json.dump(cases, f)
            lines = json.loads(subprocess.run([NODE, os.path.join(HERE, "hta_args.js"), probe], capture_output=True,
                                              text=True, check=True).stdout)["lines"]
        root = os.path.join(TOOLS, "..")     # mods/mapkit/layouts of the tree: the .hta's kit maps
        base = ('+set fs_b "R" +set fs_h "P" +set logfile 2 +set net_ip 127.0.0.1 +set bo1_zombies 1 '
                '+set r_fullscreen 0 +set r_borderless 1 +set vid_xpos 0 +set vid_ypos 0 +set bo1_mod_sharedconfig 1')
        for case, line in zip(cases, lines):
            m = maps[int(case["map"])]
            c = case["combo"]
            p = P.Profile()
            p["map"] = f"mapkit:{m[3]}" if len(m) > 3 else m[0]
            for key, value in (("horde", c["horde"]), ("zinfo", c["zinfo"]), ("noperks", c["noperks"]),
                               ("quickrestart", c["quickrestart"]), ("horde_napalm", c["napalm"]),
                               ("horde_sonic", c["sonic"]), ("difficulty", c["difficulty"]),
                               ("horde_max", int(c["maxz"])), ("horde_round", int(c["round"])),
                               ("horde_spawn_all", c["spawnall"]), ("horde_unjam", c["unjam"]),
                               ("horde_clear_round", c["clearround"])):
                p[key] = value
            self.assertTrue(line.startswith(base + " ") and line.endswith(" +devmap " + m[0]), line)
            middle = line[len(base):-len(" +devmap " + m[0])].split()
            self.assertEqual(P.solo_mod_flags(p, root), middle, (m, c))
            self.assertEqual(P.resolve_map(p["map"], root)[0], m[0])


class CommandLine(unittest.TestCase):
    def test_solo_base(self):
        p = P.Profile()
        argv = P.build_command(p, "/g/BO1Zombies", "/g")
        self.assertEqual(argv[:15], ["/g/BO1Zombies", "+set", "bo1_zombies", "1", "+set", "fs_b", "/g", "+set",
                                     "fs_h", "/g", "+set", "logfile", "2", "+set", "bo1_mod_sharedconfig"])
        self.assertIn("127.0.0.1", argv)
        self.assertEqual(argv[-2:], ["+devmap", "zombie_theater"])

    def test_join(self):
        p = P.Profile()
        p["mode"], p["join_host"], p["join_port"], p["join_password"], p["name"] = "join", "10.0.0.5", 3075, "pw", \
            "Alpha"
        p["god"] = True
        argv = P.build_command(p, "x", "/g")
        line = " ".join(argv)
        self.assertNotIn("devmap", line)
        self.assertNotIn("net_ip", line)
        self.assertNotIn("bo1_testclient_god", line)       # the server decides
        self.assertNotIn("fs_mods", line)
        self.assertIn("+set password pw", line)
        self.assertIn("+set name Alpha", line)
        self.assertTrue(line.endswith("+set cl_allowWanConnect 1 +set cl_autoConnect 10.0.0.5:3075"))

    def test_host_joins_loopback(self):
        p = P.Profile()
        p["mode"], p["host_port"] = "host", 3080
        self.assertTrue(" ".join(P.build_command(p, "x", "/g")).endswith("+set cl_autoConnect 127.0.0.1:3080"))

    def test_extra_and_window(self):
        p = P.Profile()
        p["extra"] = [{"on": True, "text": "set r_fog 0"}, {"on": False, "text": "set no 1"}]
        p["window"], p["window_size"] = "window", "1600x900"
        p["aim_assist"], p["aim_lockon"] = "1", "0"
        line = " ".join(P.build_command(p, "x", "/g"))
        self.assertIn("+set r_customMode 1600x900", line)
        self.assertIn("+set r_fog 0 +devmap", line)
        self.assertNotIn("set no 1", line)
        self.assertIn("+set bo1_aimassist 1", line)
        self.assertIn("+set aim_lockon_enabled 0", line)
        self.assertNotIn("aim_slowdown_enabled", line)

    def test_refusals(self):
        p = P.Profile()
        p["extra"] = [{"on": True, "text": "set a 1 +set b 2"}]
        with self.assertRaises(ValueError):
            P.build_command(p, "x", "/g")
        p["extra"] = []
        with self.assertRaisesRegex(ValueError, "«"):
            P.build_command(p, "x", "/a+b")
        p["mode"] = "join"
        with self.assertRaises(ValueError):
            P.build_command(p, "x", "/g")          # no server
        p["join_host"], p["name"] = "host", "has space"
        with self.assertRaises(ValueError):
            P.build_command(p, "x", "/g")

    def test_bo2_nuketown(self):
        with tempfile.TemporaryDirectory() as tmp:
            layouts = os.path.join(tmp, "mods", "mapkit", "layouts")
            os.makedirs(layouts)
            for name, base in (("bo2_zm_nuked", "zombie_pentagon"), ("aldric_sanatorium", "zombie_pentagon"),
                               ("other", "zombie_pentagon")):
                with open(os.path.join(layouts, name + ".json"), "w") as f:
                    json.dump({"name": name, "base": base, "edits": []}, f)
            maps = [m for m in P.solo_maps(tmp) if m[3]]
            self.assertEqual([m[3] for m in maps], ["aldric_sanatorium", "bo2_zm_nuked", "other"])
            self.assertEqual(maps[1][1], "Mapkit : Nuketown (Black Ops II)")
            p = P.Profile()
            p["map"] = "mapkit:bo2_zm_nuked"
            line = " ".join(P.build_command(p, "x", tmp))
            self.assertIn("+set fs_game mods/mapkit +set bo1_mod_mapkit bo2_zm_nuked", line)
            self.assertTrue(line.endswith("+devmap zombie_pentagon"), line)
            # the native Nuketown is offered only when its zone and its BSP are in the package
            with open(os.path.join(layouts, "bo2_zm_nuked_bsp.json"), "w") as f:
                json.dump({"name": "bo2_zm_nuked_bsp", "base": "zombie_pentagon", "edits": []}, f)
            P.mapkit_layouts.cache_clear()
            self.assertNotIn("bo2_zm_nuked_bsp", [m[3] for m in P.solo_maps(tmp)])
            p["map"] = "mapkit:bo2_zm_nuked_bsp"
            self.assertTrue(any("zombie_bo2nuked.d3dbsp" in w for w in P.problems(p, tmp)))
            os.makedirs(os.path.join(tmp, "usermaps", "zombie_bo2nuked"))
            open(os.path.join(tmp, "usermaps", "zombie_bo2nuked", "zombie_bo2nuked.ff"), "w").close()
            os.makedirs(os.path.join(tmp, "main", "maps"))
            open(os.path.join(tmp, "main", "maps", "zombie_bo2nuked.d3dbsp"), "w").close()
            self.assertIn("Mapkit : Nuketown native (Black Ops II, monde BSP)",
                          [m[1] for m in P.solo_maps(tmp) if m[3] == "bo2_zm_nuked_bsp"])
            line = " ".join(P.build_command(p, "x", tmp))
            self.assertIn("+set bo1_mod_mapkit bo2_zm_nuked_bsp ", line)
            self.assertIn("+set bo1_mod_zones zombie_bo2nuked +set bo1_bspworld maps/zombie_bo2nuked.d3dbsp ", line)
            self.assertTrue(line.endswith("+devmap zombie_pentagon"), line)

    def test_bo2_weapons(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = P.Profile()
            p["map"], p["bo2_weapons"] = "zombie_pentagon", 3
            # mod.ff is the player's own build: without it the line is refused
            self.assertTrue(any("bo2weapons/mod.ff" in w for w in P.problems(p, tmp)))
            os.makedirs(os.path.join(tmp, "mods", "bo2weapons"))
            open(os.path.join(tmp, "mods", "bo2weapons", "mod.ff"), "w").close()
            self.assertEqual(P.problems(p, tmp), [])
            line = " ".join(P.build_command(p, "x", tmp))
            self.assertIn("+set fs_mods zinfo bo2weapons ", line)
            self.assertIn("+set bo2weapons_box 2", line)
            self.assertNotIn("fs_game", line)
            # a MapKit map's line only sets fs_game: its mod is named first, as the engine stacks it
            p["map"], p["zinfo"] = "mapkit:aldric_sanatorium", False
            line = " ".join(P.solo_mod_flags(p, tmp))
            self.assertIn("+set fs_game mods/mapkit ", line)
            self.assertIn("+set fs_mods mapkit bo2weapons ", line)
            p["horde"], p["zinfo"] = True, True
            self.assertIn("+set fs_mods mapkit horde zinfo bo2weapons ", " ".join(P.solo_mod_flags(p, tmp)))
            p["map"], p["horde"], p["difficulty"], p["bo2_weapons"] = "zombie_theater", False, 1, 2
            line = " ".join(P.solo_mod_flags(p, tmp))
            self.assertIn("+set fs_mods nightmare zinfo bo2weapons ", line)
            self.assertIn("+set bo2weapons_box 1", line)
            # no box on Dead Ops Arcade
            p["map"], p["difficulty"] = "zombietron", 0
            self.assertNotIn("bo2weapons", " ".join(P.solo_mod_flags(p, tmp)))
            p["bo2_weapons"] = 0
            p["map"] = "zombie_pentagon"
            self.assertNotIn("bo2weapons", " ".join(P.solo_mod_flags(p, tmp)))
            # a client of a server with the weapons loads the same mod.ff; the server sets the box
            p["mode"], p["host_bo2_weapons"] = "host", 1
            line = " ".join(P.build_command(p, "x", tmp))
            self.assertIn("+set fs_mods bo2weapons ", line)
            self.assertNotIn("bo2weapons_box", line)
            p["mode"], p["join_host"] = "join", "192.168.1.20"
            self.assertNotIn("bo2weapons", " ".join(P.build_command(p, "x", tmp)))
            p["join_bo2_weapons"] = True
            self.assertIn("+set fs_mods bo2weapons ", " ".join(P.build_command(p, "x", tmp)))
            shutil.rmtree(os.path.join(tmp, "mods", "bo2weapons"))
            self.assertTrue(any("bo2weapons/mod.ff" in w for w in P.problems(p, tmp)))
            # a saved value out of range falls back to off
            path = os.path.join(tmp, "profile.json")
            with open(path, "w") as f:
                json.dump({"bo2_weapons": 7, "host_bo2_weapons": 2}, f)
            q = P.Profile(path)
            self.assertEqual((q["bo2_weapons"], q["host_bo2_weapons"]), (0, 2))

    def test_missing_dlc_map(self):
        with tempfile.TemporaryDirectory() as tmp:
            for f in ("Common/zombie_theater.ff", "Common/zombie_coast_patch.ff", "Russian/ru_patch.ff"):
                os.makedirs(os.path.join(tmp, "zone", os.path.dirname(f)), exist_ok=True)
                open(os.path.join(tmp, "zone", f), "w").close()
            self.assertEqual(P.installed_maps(tmp), {"zombie_theater"})
            p = P.Profile()
            P.build_command(p, "x", tmp)
            p["map"] = "zombie_coast"
            with self.assertRaisesRegex(ValueError, "pas installée"):
                P.build_command(p, "x", tmp)
            p["mode"], p["host_map"] = "host", "zombie_moon"
            with self.assertRaisesRegex(ValueError, "pas installée"):
                P.build_command(p, "x", tmp)
            p["mode"] = "join"
            p["join_host"] = "10.0.0.1"
            P.build_command(p, "x", tmp)        # the server has its own maps

    def test_command_count_limit(self):
        p = P.Profile()
        p["horde"], p["horde_max"], p["difficulty"] = True, 1000, 2
        P.build_command(p, "x", "/g")
        p["extra"] = [{"on": True, "text": f"set d{i} 1"} for i in range(30)]
        with self.assertRaisesRegex(ValueError, "63"):
            P.build_command(p, "x", "/g")
        p["extra"] = []
        with self.assertRaisesRegex(ValueError, "63"):
            P.build_command(p, "x", "/g", launch_cfg="+set a 1 " * 40)

    def test_scene_models(self):
        def dvar(argv, name):
            values = [argv[i + 2] for i in range(len(argv) - 2) if argv[i] == "+set" and argv[i + 1] == name]
            self.assertLessEqual(len(values), 1, name)
            return values[0] if values else None
        p = P.Profile()
        argv = P.build_command(p, "x", "/g")
        self.assertIsNone(dvar(argv, "r_mod_sceneModels"))
        self.assertIsNone(dvar(argv, "r_mod_modelLightingEntries"))
        p["scene_models"] = 4096
        argv = P.build_command(p, "x", "/g")
        self.assertEqual(dvar(argv, "r_mod_sceneModels"), "4096")
        self.assertEqual(dvar(argv, "r_mod_modelLightingEntries"), "5120")
        p["scene_models"] = 2048
        self.assertEqual(dvar(P.build_command(p, "x", "/g"), "r_mod_modelLightingEntries"), "3072")
        # the horde's own pool (3392 at 1000 zombies) is larger: kept, and set once
        p["horde"], p["horde_max"] = True, 1000
        argv = P.build_command(p, "x", "/g")
        self.assertEqual(dvar(argv, "r_mod_modelLightingEntries"), "3392")
        self.assertEqual(dvar(argv, "r_mod_sceneModels"), "2048")
        # the client draws too: a join line carries it
        p["mode"], p["join_host"] = "join", "10.0.0.1"
        self.assertEqual(dvar(P.build_command(p, "x", "/g"), "r_mod_sceneModels"), "2048")

    def test_start_kit(self):
        """Start points and perks stack mods/admin on the line's fs_mods (or add one) and set its two dvars."""
        def mods_and_kit(p, root="/g"):
            argv = P.solo_mod_flags(p, root)
            i = argv.index("fs_mods") if "fs_mods" in argv else None
            mods = []
            if i is not None:
                for a in argv[i + 1:]:
                    if a.startswith("+"):
                        break
                    mods.append(a)
            kit = {argv[k + 1]: argv[k + 2] for k in range(len(argv) - 2) if argv[k] == "+set"
                   and argv[k + 1] in ("zm_startpoints", "zm_startperks", "zm_startpap", "zm_farrespawn")}
            return mods, kit
        p = P.Profile()
        self.assertEqual(mods_and_kit(p), (["zinfo"], {}))         # the default line is unchanged
        p["start_points"] = 50000
        self.assertEqual(mods_and_kit(p), (["zinfo", "admin"], {"zm_startpoints": "50000"}))
        p["start_perks"] = ["revive", "jugg"]                      # given in START_PERKS order
        self.assertEqual(mods_and_kit(p)[1], {"zm_startpoints": "50000", "zm_startperks": "jugg,revive"})
        p["start_points"], p["zinfo"] = 0, False
        self.assertEqual(mods_and_kit(p), (["admin"], {"zm_startperks": "jugg,revive"}))
        p["start_perks"], p["start_pap"] = [], True                # the start Pack-a-Punch alone stacks admin too
        self.assertEqual(mods_and_kit(p), (["admin"], {"zm_startpap": "1"}))
        p["start_pap"], p["far_respawn"] = False, 2500             # so does the far zombies' distance
        self.assertEqual(mods_and_kit(p), (["admin"], {"zm_farrespawn": "2500"}))
        p["start_perks"], p["far_respawn"] = ["revive", "jugg"], 0
        p["noperks"] = True
        self.assertEqual(mods_and_kit(p)[0], ["noperks", "admin"])
        p["noperks"], p["horde"] = False, True
        self.assertEqual(mods_and_kit(p)[0], ["horde", "zinfo", "admin"])
        p["horde"], p["difficulty"] = False, 2
        self.assertEqual(mods_and_kit(p)[0], ["nightmare", "admin"])
        with tempfile.TemporaryDirectory() as tmp:
            layouts = os.path.join(tmp, "mods", "mapkit", "layouts")
            os.makedirs(layouts)
            with open(os.path.join(layouts, "bo2_zm_nuked.json"), "w") as f:
                json.dump({"name": "bo2_zm_nuked", "base": "zombie_pentagon", "edits": []}, f)
            p = P.Profile()
            p["map"], p["zinfo"], p["start_points"] = "mapkit:bo2_zm_nuked", False, 50000
            self.assertEqual(mods_and_kit(p, tmp)[0], ["mapkit", "admin"])
            p["zinfo"] = True
            self.assertEqual(mods_and_kit(p, tmp)[0], ["mapkit", "zinfo", "admin"])
        # a join leaves the server to decide
        p = P.Profile()
        p["start_points"], p["mode"], p["join_host"] = 50000, "join", "10.0.0.1"
        self.assertNotIn("zm_startpoints", P.build_command(p, "x", "/g"))

    def test_profile_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "sub", "profile.json")
            p = P.Profile(path)
            p["mode"], p["horde_max"] = "join", 300
            p.save()
            if os.name != "nt":
                self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)
            with open(path, "w") as f:
                json.dump({"mode": "nope", "horde_max": "300", "god": True, "window": 3, "scene_models": 3000,
                           "start_points": 42, "start_perks": ["mule", "nope", "jugg", "jugg"],
                           "favorites": [{"host": "a"}, {"name": "no host"}, 5]}, f)
            q = P.Profile(path)
            self.assertEqual(q["mode"], "solo")
            self.assertEqual(q["horde_max"], 128)
            self.assertTrue(q["god"])
            self.assertEqual(q["window"], "borderless")
            self.assertEqual(q["scene_models"], 1024)
            self.assertEqual(q["start_points"], 0)
            self.assertEqual(q["start_perks"], ["jugg", "mule"])
            self.assertEqual(q["favorites"], [{"host": "a"}])

    def test_cli_print_cmd(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = make_dist(os.path.join(tmp, "pkg"))
            cfg = os.path.join(tmp, "cfg")
            p = P.Profile(os.path.join(cfg, "profile.json"))
            p["map"] = "zombie_moon"
            p.save()
            out = subprocess.run([sys.executable, os.path.join(TOOLS, "zmclient", "main.py"), "--dist", dist.root,
                                  "--config", cfg, "--print-cmd"], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertTrue(out.stdout.strip().endswith("+devmap zombie_moon"), out.stdout)
            self.assertEqual(cli_main(["--dist", dist.root, "--config", cfg, "--play"]), 1)   # no game files


class SplitScreen(unittest.TestCase):
    @staticmethod
    def sets(argv):
        """The +set dvar value pairs of a command line, the last one winning."""
        return {argv[i + 1]: argv[i + 2] for i in range(len(argv) - 2) if argv[i] == "+set"}

    def test_off_and_solo_give_one_window(self):
        p = P.Profile()
        self.assertFalse(P.split_on(p))
        p["split"] = "kbm_pad"
        self.assertFalse(P.split_on(p))              # solo is a listen server of one player
        self.assertNotIn("bo1_splitInput", P.build_command(p, "x", "/g"))
        with self.assertRaises(ValueError):
            P.build_command(p, "x", "/g", player=2)

    def test_modes_and_layouts(self):
        expected_inputs = {"kbm_pad": ("kbm", "pad0"), "pad_pad": ("pad0", "pad1")}
        expected_rects = {"side": ("0,0,0.5,1", "0.5,0,0.5,1"), "stack": ("0,0,1,0.5", "0,0.5,1,0.5")}
        for mode, split, layout in itertools.product(("host", "join"), expected_inputs, expected_rects):
            p = P.Profile()
            p["mode"], p["join_host"], p["host_port"] = mode, "10.0.0.5", 3080
            p["split"], p["split_layout"], p["name"] = split, layout, "Alpha"
            self.assertTrue(P.split_on(p))
            for player in (1, 2):
                argv = P.build_command(p, "x", "/g", player=player)
                d = self.sets(argv)
                case = (mode, split, layout, player)
                self.assertEqual(d["bo1_splitInput"], expected_inputs[split][player - 1], case)
                self.assertEqual(d["r_borderlessRect"], expected_rects[layout][player - 1], case)
                self.assertEqual((d["r_fullscreen"], d["r_borderless"], d["vid_xpos"], d["vid_ypos"]),
                                 ("0", "1", "0", "0"), case)
                self.assertEqual(d["fs_b"], "/g", case)
                self.assertEqual(d["fs_h"], "/g" if player == 1 else os.path.join("/g", "split2"), case)
                self.assertEqual(d["name"], "Alpha" if player == 1 else "Joueur2", case)
                self.assertEqual(d["cl_autoConnect"], "127.0.0.1:3080" if mode == "host" else "10.0.0.5:3074", case)
                self.assertEqual(d.get("snd_menu_master"), None if player == 1 else "0", case)
                self.assertNotIn("r_customMode", d, case)
                self.assertNotIn("devmap", argv, case)
                self.assertTrue(all(" " not in a for a in argv[1:]), case)   # a blank would split a +set

    def test_player2_name_and_sound(self):
        p = P.Profile()
        p["mode"], p["split"], p["name2"], p["split_sound2"] = "host", "pad_pad", "Bravo", True
        d = self.sets(P.build_command(p, "x", "/g", player=2))
        self.assertEqual((d["name"], d["snd_menu_master"]), ("Bravo", "1"))
        p["name2"] = "has space"
        with self.assertRaisesRegex(ValueError, "joueur 2"):
            P.build_command(p, "x", "/g")
        p["split"] = "off"
        P.build_command(p, "x", "/g")                # player 2's name only matters with a split screen

    def test_command_count_limit(self):
        p = P.Profile()
        p["mode"], p["split"] = "host", "kbm_pad"
        argv = P.build_command(p, "x", "/g", player=2)
        room = P.MAX_COMMAND_LINES - sum(1 for a in argv[1:] if a.startswith("+"))
        p["extra"] = [{"on": True, "text": f"set e{i} 1"} for i in range(room)]
        P.build_command(p, "x", "/g", player=2)
        p["extra"].append({"on": True, "text": "set one_more 1"})
        with self.assertRaisesRegex(ValueError, "63"):
            P.build_command(p, "x", "/g", player=2)

    def test_profile_values_are_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "profile.json")
            with open(path, "w") as f:
                json.dump({"split": "three", "split_layout": "diagonal", "split_sound2": "yes", "name2": 2}, f)
            q = P.Profile(path)
            self.assertEqual((q["split"], q["split_layout"], q["split_sound2"], q["name2"]),
                             ("off", "side", False, ""))

    def test_cli_print_cmd_gives_both_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = make_dist(os.path.join(tmp, "pkg"))
            cfg = os.path.join(tmp, "cfg")
            p = P.Profile(os.path.join(cfg, "profile.json"))
            p["mode"], p["join_host"], p["split"] = "join", "10.0.0.5", "pad_pad"
            p.save()
            out = subprocess.run([sys.executable, os.path.join(TOOLS, "zmclient", "main.py"), "--dist", dist.root,
                                  "--config", cfg, "--print-cmd"], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            lines = out.stdout.strip().splitlines()
            self.assertEqual(len(lines), 2, out.stdout)
            self.assertIn("+set bo1_splitInput pad0", lines[0])
            self.assertIn("+set bo1_splitInput pad1", lines[1])
            self.assertIn(os.path.join(dist.root, "split2"), lines[1])


class ConfigFile(unittest.TestCase):
    TEXT = ('// generated by Call of Duty, do not modify\nunbindall\nbind TAB "+scores"\nbind F "+activate"\n'
            'seta cg_blood "1"\nseta sensitivity "5"\ncon_hidechannel *\n')

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        os.makedirs(os.path.join(self.tmp.name, "players"))
        with open(os.path.join(self.tmp.name, "players", "config.cfg"), "w", newline="") as f:
            f.write(self.TEXT)
        self.cfg = GameConfig(self.tmp.name)
        self.cfg.load()

    def tearDown(self):
        self.tmp.cleanup()

    def read(self, name="config.cfg"):
        with open(os.path.join(self.tmp.name, "players", name), newline="") as f:
            return f.read()

    def test_edit_in_place_and_append(self):
        self.cfg.save(self.cfg.changes({"sensitivity": "7.5", "r_picmip": "1", "bo1_mod_fov": "90"}),
                      {"F": "+melee", "MOUSE4": "+frag"}, {"TAB"})
        text = self.read()
        self.assertEqual(text.splitlines(), [
            "// generated by Call of Duty, do not modify", "unbindall", 'bind F "+melee"', 'bind MOUSE4 "+frag"',
            'seta cg_blood "1"', 'seta sensitivity "7.5"', 'seta bo1_mod_fov "90"', 'seta r_picmip "1"',
            'seta r_picmip_bump "1"', 'seta r_picmip_manual "1"', 'seta r_picmip_spec "1"', "con_hidechannel *"])
        self.assertNotIn("\r", text)                        # the file's own line ends
        self.assertIn('seta bo1_mod_fov "90"', self.read("bo1_shared.cfg"))
        self.assertEqual(self.cfg.value("bo1_mod_fov"), "90")
        self.assertEqual(self.cfg.value("r_picmip"), "1")
        self.cfg.save(self.cfg.changes({"r_picmip": "auto"}))
        self.assertEqual(self.cfg.value("r_picmip"), "auto")

    def test_validate(self):
        self.assertEqual(validate("sensitivity", " 3 "), ("3", None))
        self.assertIsNotNone(validate("sensitivity", "500")[1])
        self.assertIsNotNone(validate("r_vsync", "2")[1])
        self.assertIsNotNone(validate("bo1_mod_fov", "40")[1])
        self.assertEqual(validate("bo1_mod_fov", "0"), ("0", None))
        self.assertIsNotNone(validate("r_mode", "big")[1])
        self.assertIsNotNone(validate("anything", 'a"b')[1])

    def test_missing_file(self):
        cfg = GameConfig(os.path.join(self.tmp.name, "nothing"))
        cfg.load()
        self.assertFalse(cfg.exists)
        cfg.save({"sensitivity": "4"})
        self.assertTrue(cfg.exists)
        self.assertEqual(cfg.values, {"sensitivity": "4"})


class FakeStatusServer(threading.Thread):
    def __init__(self, name, players=0):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.05)
        self.addr = self.sock.getsockname()
        self.name = name
        self.players = players
        self.queries = 0
        self.running = True
        self.start()

    def stop(self):
        self.running = False
        self.join()
        self.sock.close()

    def run(self):
        while self.running:
            try:
                data, src = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            if data == OOB + b"getstatus":
                self.queries += 1
                lines = "".join(f'{i} 500 20 5 "P{i}"\n' for i in range(self.players))
                self.sock.sendto(OOB + f"statusResponse\n\\sv_hostname\\{self.name}\\mapname\\zombie_moon"
                                       f"\\sv_maxclients\\4\n{lines}".encode(), src)


class ServerBrowser(unittest.TestCase):
    def test_lan_and_favorites(self):
        a, b = FakeStatusServer("A", 2), FakeStatusServer("B")
        dead = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        dead.bind(("127.0.0.1", 0))
        favs = [("127.0.0.1", b.addr[1]), ("127.0.0.1", dead.getsockname()[1]), ("no.such.host.invalid", 3074)]
        br = Browser(lambda: favs, lan_targets=[a.addr, b.addr], round_seconds=60, listen=0.3)
        try:
            br.round()
            entries, resolved = br.snapshot()
            self.assertEqual(entries[a.addr].status.get("sv_hostname"), "A")
            self.assertEqual(len(entries[a.addr].status.players), 2)
            self.assertTrue(entries[a.addr].lan)
            self.assertIsNotNone(entries[a.addr].ping)
            # B answered the broadcast: not asked again as a favourite in the same round (50 ms server limit)
            self.assertEqual(b.queries, 1)
            self.assertEqual(entries[dead.getsockname()].error, "pas de réponse")
            self.assertIsInstance(resolved[("no.such.host.invalid", 3074)], str)
        finally:
            br.sock.close()
            a.stop()
            b.stop()
            dead.close()

    def test_lan_servers_are_forgotten(self):
        a = FakeStatusServer("A")
        br = Browser(lambda: [], lan_targets=[a.addr], round_seconds=60, listen=0.2)
        try:
            br.round()
            self.assertIn(a.addr, br.snapshot()[0])
            br.entries[a.addr].seen -= 100
            br.lan_targets = []
            br.round()
            self.assertNotIn(a.addr, br.snapshot()[0])
        finally:
            br.sock.close()
            a.stop()


FAKE_FFCONV = r'''
import os, sys
args = sys.argv[1:]
out = args[args.index("--out") + 1]
if os.environ.get("FAKE_FFCONV_FAIL"):
    print("[1/1] boom"); sys.exit(1)
os.makedirs(os.path.join(out, "zone", "common"))
for i, n in enumerate(("a.ff", "b.ff")):
    print(f"[{i + 1}/2] {n}")
    open(os.path.join(out, "zone", "common", n), "w").write("ff")
open(os.path.join(out, "shaders.pack"), "w").write("pack")
'''


class GameSetup(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.install = os.path.join(t, "Call of Duty Black Ops")
        for d in ("zone/Common", "main/video", "players"):      # Steam's case
            os.makedirs(os.path.join(self.install, d))
        for f, text in (("localization.txt", "loc"), ("main/iw_00.iwd", "x" * 1000), ("main/iw_01.iwd", "y"),
                        ("main/video/intro.bik", "v"), ("zone/Common/a.ff", "retail"), ("BlackOps.exe", "")):
            with open(os.path.join(self.install, f), "w") as fh:
                fh.write(text)
        self.dist = make_dist(os.path.join(t, "pkg"))
        with open(os.path.join(self.dist.root, "ffconv.exe"), "w") as fh:
            fh.write("v1")
        self.fake = os.path.join(t, "fake_ffconv.py")
        with open(self.fake, "w") as fh:
            fh.write(FAKE_FFCONV)
        self.saved = gamesetup.converter_command
        gamesetup.converter_command = lambda lay, install, out: [sys.executable, self.fake, "--game", install, "--out",
                                                                 out]

    def tearDown(self):
        gamesetup.converter_command = self.saved
        os.environ.pop("FAKE_FFCONV_FAIL", None)
        self.tmp.cleanup()

    def run_setup(self, **kw):
        job = gamesetup.Setup(self.dist.root, self.install, **kw)
        job.run()
        return job

    def test_install_then_up_to_date(self):
        self.assertIsNotNone(gamesetup.ready(self.dist.root))
        job = self.run_setup()
        self.assertIsNone(job.error, job.lines)
        self.assertEqual((job.zone_at, job.zone_count), (2, 2))
        self.assertIsNone(gamesetup.ready(self.dist.root))
        st = gamesetup.state(self.dist.root)
        self.assertTrue(all(ok for ok, _ in st.values()), st)
        self.assertTrue(os.path.isfile(os.path.join(self.dist.root, "main", "video", "intro.bik")))
        self.assertFalse(os.path.exists(os.path.join(self.dist.root, "players", "config.cfg")))
        self.assertEqual(os.path.isfile(os.path.join(self.dist.root, "shaders.pack")), os.name != "nt")
        again = self.run_setup(convert=False)
        self.assertIn("0 fichiers à copier", "\n".join(again.lines))
        # a new ffconv in the package: the zones it did not make are stale
        with open(os.path.join(self.dist.root, "ffconv.exe"), "w") as fh:
            fh.write("v2")
        self.assertFalse(gamesetup.state(self.dist.root)["zones"][0])

    def test_relative_install_path(self):
        here = os.getcwd()
        os.chdir(os.path.dirname(self.install))
        try:
            job = gamesetup.Setup(self.dist.root, os.path.basename(self.install), video=False)
        finally:
            os.chdir(here)
        self.assertEqual(job.install, self.install)
        job.run()
        self.assertIsNone(job.error, job.lines)

    def test_since_survives_the_line_cap(self):
        job = gamesetup.Setup(self.dist.root, self.install)
        seen, total = [], 0
        for i in range(450):
            job.note(f"l{i}")
            if i % 7 == 0:
                lines, total = job.since(total)
                seen += lines
        lines, total = job.since(total)
        seen += lines
        self.assertEqual(seen, [f"l{i}" for i in range(450)])

    def test_failed_conversion_keeps_old_zones(self):
        self.run_setup()
        os.environ["FAKE_FFCONV_FAIL"] = "1"
        job = self.run_setup(video=False)
        self.assertIn("ffconv a échoué", job.error)
        self.assertTrue(os.path.isfile(os.path.join(self.dist.root, "zone", "common", "a.ff")))
        self.assertFalse(os.path.exists(os.path.join(self.dist.root, "zone.zmclient-new")))

    def test_refusals(self):
        job = gamesetup.Setup(self.dist.root, os.path.join(self.tmp.name, "nothing"))
        job.run()
        self.assertIn("n'est pas une installation", job.error)
        job = gamesetup.Setup(self.install, self.install)
        job.run()
        self.assertIn("le même", job.error)

    def test_converter_command(self):
        lay = gamesetup.Layout(self.dist.root)
        out = os.path.join(self.dist.root, "zone.new")
        cmd = self.saved(lay, self.install, out)
        if os.name == "nt":
            self.assertEqual(cmd, [lay.ffconv, "--verify", "--game", self.install, "--out", out])
        elif os.path.exists("/proc/sys/fs/binfmt_misc/WSLInterop") and shutil.which("wslpath"):
            # WSL runs the .exe itself, with Windows paths
            self.assertEqual(cmd[0], lay.ffconv)
            self.assertTrue(cmd[3].startswith("\\\\") and cmd[5].endswith("\\zone.new"), cmd)
        os.remove(lay.ffconv)
        self.assertIn("pas de ffconv.exe", self.saved(lay, self.install, out))

    def test_steam_library_file(self):
        steam = os.path.join(self.tmp.name, "Steam")
        os.makedirs(os.path.join(steam, "steamapps"))
        with open(os.path.join(steam, "steamapps", "libraryfolders.vdf"), "w") as fh:
            fh.write('"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"D:\\\\Games\\\\Steam"\n\t}\n'
                     '\t"1"\n\t{\n\t\t"path"\t\t"/mnt/games"\n\t}\n}\n')
        libs = gamesetup.library_folders(steam)
        self.assertEqual(libs[0], steam)
        self.assertIn(os.path.normpath("D:\\Games\\Steam"), libs)
        self.assertIn(os.path.normpath("/mnt/games"), libs)


class FolderBrowser(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.game = os.path.join(t, "Steam", "steamapps", "common", "Call of Duty Black Ops")
        for d in ("zone/Common", "main"):
            os.makedirs(os.path.join(self.game, d))
        for f in ("localization.txt", "main/iw_00.iwd"):
            open(os.path.join(self.game, f), "w").close()
        for d in ("Steam/steamapps/common/Another Game", "Steam/steamapps/common/aardvark", "Steam/.hidden",
                  "Steam/$Recycle.Bin", "Steam/steamapps/common/Zebra"):
            os.makedirs(os.path.join(t, d))
        open(os.path.join(t, "Steam", "steamapps", "common", "a_file.txt"), "w").close()
        self.common = os.path.dirname(self.game)

    def tearDown(self):
        self.tmp.cleanup()

    def test_listing(self):
        entries, error = dirbrowser.listing(self.common)
        self.assertEqual(error, "")
        self.assertEqual([e.name for e in entries],
                         ["..", "aardvark", "Another Game", "Call of Duty Black Ops", "Zebra"])
        self.assertTrue(entries[0].up and entries[0].path == os.path.dirname(self.common))
        self.assertEqual([e.name for e in entries if e.install], ["Call of Duty Black Ops"])
        names = [e.name for e in dirbrowser.listing(os.path.join(self.tmp.name, "Steam"))[0]]
        self.assertEqual(names, ["..", "steamapps"])          # hidden and system folders left out
        self.assertIn("illisible", dirbrowser.listing(os.path.join(self.tmp.name, "nope"))[1])

    def test_parent_at_the_top(self):
        if os.name == "nt":
            self.assertEqual(dirbrowser.parent("C:\\"), dirbrowser.COMPUTER)
        else:
            self.assertEqual(dirbrowser.parent("/"), "/")
            self.assertNotEqual(dirbrowser.listing("/")[0][:1] and dirbrowser.listing("/")[0][0].name, "..")
        self.assertEqual(dirbrowser.parent(os.path.join(self.common, "Zebra")), self.common)

    def test_dialog_walk_and_pick(self):
        picked = []

        def on_pick(path):
            if not gamesetup.is_install(path):
                return "pas une installation"
            picked.append(path)
            return None
        d = dirbrowser.FolderDialog("t", self.common, on_pick)
        self.assertFalse(d.here_install)
        self.assertFalse(d.key(" "))                          # not an install: stays open with the reason
        self.assertIn("pas une installation", d.error)
        d.key("c")                                            # type-ahead to "Call of Duty Black Ops"
        self.assertEqual(d.entries[d.sel].name, "Call of Duty Black Ops")
        d.key("ENTER")
        self.assertEqual(d.path, self.game)
        self.assertTrue(d.here_install)
        self.assertTrue(d.key(" "))                           # picked: the dialog closes
        self.assertEqual(picked, [self.game])
        d.key("BACKSPACE")                                    # back up: the folder we came from is selected
        self.assertEqual(d.path, self.common)
        self.assertEqual(d.entries[d.sel].name, "Call of Duty Black Ops")
        self.assertTrue(d.key("ESC"))

    def test_start_folder(self):
        self.assertEqual(dirbrowser.start_folder(self.game, []), self.game)
        self.assertEqual(dirbrowser.start_folder("", [self.game]), self.common)
        self.assertTrue(os.path.isdir(dirbrowser.start_folder(os.path.join(self.common, "gone"), [])))


@unittest.skipIf(os.name == "nt" or not shutil.which("sleep"), "posix only: a copy of sleep plays the game")
class ClientProcess(unittest.TestCase):
    def test_start_track_kill(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = make_dist(os.path.join(tmp, "pkg"), exe_source=shutil.which("sleep"))
            os.chmod(dist.exe, 0o755)
            c = Client(dist)
            self.assertFalse(c.running())
            c.start([dist.exe, "30"], "mods/horde")
            self.assertTrue(c.running())
            again = Client(dist)                       # a later ZmClient finds it through the state file
            self.assertTrue(again.running())
            self.assertEqual(again.log_candidates()[0], os.path.join(dist.root, "mods", "horde", "console_mp.log"))
            c.kill()
            deadline = time.monotonic() + 5
            while again.running() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertFalse(again.running())

    def test_player2_is_tracked_apart(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = make_dist(os.path.join(tmp, "pkg"), exe_source=shutil.which("sleep"))
            os.chmod(dist.exe, 0o755)
            one, two = Client(dist), Client(dist, 2)
            two.start([dist.exe, "30"], "")
            self.assertTrue(os.path.isdir(os.path.join(dist.root, "split2")))   # player 2's fs_h
            self.assertTrue(Client(dist, 2).running())
            self.assertFalse(one.running())
            self.assertFalse(Client(dist).running())
            self.assertEqual(two.log_candidates()[0], os.path.join(dist.root, "split2", "main", "console_mp.log"))
            two.kill()
            deadline = time.monotonic() + 5
            while two.running() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertFalse(two.running())

    def test_log_path_picks_the_newest(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = make_dist(os.path.join(tmp, "pkg"))
            c = Client(dist)
            self.assertEqual(c.log_path(), os.path.join(dist.root, "main", "console_mp.log"))
            with open(os.path.join(dist.root, "console_mp.log"), "w") as fh:
                fh.write("x\n")
            self.assertEqual(c.log_path(), os.path.join(dist.root, "console_mp.log"))
            # after a start, a log older than the start does not count: the game's fs_game folder comes first
            c.started, c.fs_game = time.time() + 10, "mods/horde"
            self.assertEqual(c.log_path(), os.path.join(dist.root, "mods", "horde", "console_mp.log"))


if __name__ == "__main__":
    unittest.main()
