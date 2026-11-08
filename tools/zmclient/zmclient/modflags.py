"""The horde and nightmare mods' + commands: a Python copy of tools/horde_flags.js and tools/nightmare_flags.js.

The JS files stay the reference (tools/mod-launcher.hta and the merge checks run them). tests/zmclient compares this
copy with them through node on a matrix of inputs, so the two cannot drift silently.
"""
import math
import re

NIGHTMARE_ZONES = "zombie_temple zombie_coast zombie_moon"


def _js_number(x):
    """A number as JS's string concatenation writes it (30, 12.5)."""
    if isinstance(x, float) and x.is_integer():
        return str(int(x))
    return str(x)


def horde_flags(n=None, round_=None, napalm=False, sonic=False, zinfo=False, spawn_all=False, fill_rate=None,
                noperks=False, unjam=True, carryover=True):
    """hordeFlags(o) of tools/horde_flags.js. n, round_ and fill_rate: None is the JS NaN (not given)."""
    n = 128 if n is None else int(n)
    n = max(24, min(1024, n))
    a = ("+set fs_game mods/horde +set fs_mods horde zinfo" + (" noperks" if noperks else "")
         + f" +set horde_hud 0 +set horde_max_zombies {n} +set bo1_mod_maxactors {min(1024, max(32, n + 40))}")
    if n > 100:
        a += f" +set r_mod_skinCacheMB {max(32, 16 * math.ceil((n * 70 * 1.5 / 600 + 0.15625) / 16))}"
    if n > 150:
        a += f" +set r_mod_gfxEnts {min(2048, 256 * math.ceil((n + 128) / 256))}"
    if n > 300:
        a += f" +set r_mod_modelLightingEntries {min(5120, 32 * math.ceil(3 * (n + 128) / 32))}"
    a += " +set bo1_mod_scriptperf 1"
    a += " +set bo1_mod_pcore 1"
    if n > 300:
        a += (f" +set bo1_mod_scriptvars 2 +set bo1_mod_netents {2 if n >= 900 else 1} +set r_mod_scsShare 1"
              " +set r_mod_fxElems 4096 +set r_dobjLimit 1024")
    if n > 300:
        a += " +set bo1_mod_svfast 1"
    if n > 300:
        a += " +set cg_mod_asyncServer 1"
    if round_ is not None and int(round_) > 1:
        a += f" +set horde_start_round {int(round_)}"
    if napalm or sonic:
        a += " +set bo1_mod_zones zombie_temple"
    if napalm:
        a += " +set horde_enemy_napalm 1"
    if sonic:
        a += " +set horde_enemy_sonic 1"
    a += f" +set zinfo {1 if zinfo else 0}"
    if spawn_all:
        a += " +set horde_spawn_all 1"
    if unjam is False:
        a += " +set horde_unjam 0"
    f = fill_rate
    if f is None and n >= 900:
        f = 30
    if f is not None:
        a += f" +set horde_fill_rate {_js_number(f)}"
    if noperks:
        a += " +set noperks 1"
    if carryover is False:
        a += " +set horde_carryover 0"
    return a


def nightmare_flags(level, a, map_name):
    """nightmareFlags(level, a, map) of tools/nightmare_flags.js: level 0 Normal (a unchanged), 1 Nightmare, 2 Hell."""
    level = int(level)
    if level <= 0:
        return a
    a = a or ""
    if "+set fs_mods horde " in a:
        a = a.replace("+set fs_mods horde ", "+set fs_mods horde nightmare ", 1)
    elif "+set fs_mods " in a:
        a = a.replace("+set fs_mods ", "+set fs_mods nightmare ", 1)
    else:
        a += " +set fs_mods nightmare"
    if "+set fs_game " not in a:
        a += " +set fs_game mods/nightmare"
    # JS's '$' (no m flag) is the end of the string only: \Z, not Python's '$'
    a = re.sub(r" ?\+set bo1_mod_zones [a-z_ ]*?(?= \+|\Z)", "", a, count=1)
    a += (" +set bo1_mod_zones " + NIGHTMARE_ZONES
          + (" zombie_pentagon zombie_cosmodrome" if map_name == "zombie_theater" else "")
          + f" +set nightmare_level {2 if level >= 2 else 1}")
    return re.sub(r"^ ", "", a, count=1)
