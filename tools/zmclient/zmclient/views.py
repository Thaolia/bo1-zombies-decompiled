"""The eight views. Each draws itself in the body area and handles the keys the app does not take."""
import curses
import os
import time

from zmadmin.views import LogView as AdminLogView
from zmadmin.views import View
from zmadmin.widgets import (BAD_MARK, C_DIM, C_ERR, C_INFO, C_OK, C_WARN, CTRL_S, ENTER, ESC, OK_MARK, ChoiceDialog,
                             ConfirmDialog, FormDialog, InputDialog, color, put, wrap)

from . import gamesetup
from . import help as H
from .dirbrowser import FolderDialog, start_folder
from .gamecfg import BY_NAME, SETTINGS, valid_bind_key, validate
from .profile import (BO2_WEAPON_CHOICES, DEFAULT_PORT, MAP_LABELS, START_PERKS, bo2_weapons_available,
                      bo2_weapons_installed, START_POINT_CHOICES, check_extra, map_installed, map_label, nightmare_available,
                      parse_size, quote_command, resolve_map, solo_maps, specials_available, valid_host, valid_name,
                      valid_password)


class Row:
    def __init__(self, key, label, value="", action=None, disabled=None, help_text=None, section=False):
        self.key = key
        self.label = label
        self.value = value
        self.action = action
        self.disabled = disabled        # why the row does not apply, or None
        self.help = help_text if help_text is not None else H.ROW_HELP.get(key, "")
        self.section = section


def section(label):
    return Row(None, label, section=True)


def _int(text, lo, hi):
    text = text.strip()
    if not text.lstrip("-").isdigit():
        raise ValueError("un nombre entier")
    n = int(text)
    if not lo <= n <= hi:
        raise ValueError(f"entre {lo} et {hi}")
    return n


class RowsView(View):
    """A list of settings: one row each, its value, its help below. Rows a mode or a map does not use are greyed
    with the reason."""

    label_w = 34

    def __init__(self, app):
        super().__init__(app)
        self.sel = 0
        self.top = 0

    @property
    def p(self):
        return self.app.profile

    def rows(self):
        raise NotImplementedError

    def draw_header(self, scr, top, width):
        return top

    def buttons(self):
        """(label, key) of the buttons under the list."""
        return []

    def draw_footer(self, scr, y, bottom, width):
        pass

    def list_height(self, height):
        return max(4, height - 9)

    def _fix_sel(self, rows, step=1):
        self.sel = max(0, min(self.sel, len(rows) - 1))
        n = 0
        while rows and rows[self.sel].section and n < len(rows):
            self.sel = (self.sel + step) % len(rows)
            n += 1

    def draw(self, scr, top, height, width):
        rows = self.rows()
        self._fix_sel(rows)
        y = self.draw_header(scr, top, width)
        bottom = top + height
        room = min(self.list_height(height), bottom - y - 5)
        self.top = max(0, min(self.top, self.sel), self.sel - room + 1)
        for i, row in enumerate(rows[self.top:self.top + room]):
            n = self.top + i
            if row.section:
                put(scr, y, 1, f" {row.label} ", curses.A_BOLD | curses.A_UNDERLINE)
            else:
                attr = curses.A_REVERSE if n == self.sel else 0
                dim = color(C_DIM) if row.disabled else 0
                put(scr, y, 1, f"  {row.label:<{self.label_w}}".ljust(width - 2), attr | dim)
                put(scr, y, 3 + self.label_w, str(row.value), attr | (dim or curses.A_BOLD), width=width - 5 - self.label_w)
                self.zone(y, 1, width - 2, lambda n=n: self.select(n), lambda n=n: self.select(n, ENTER))
            y += 1
        if len(rows) > room:
            put(scr, y, 3, f"({self.top + 1}-{min(len(rows), self.top + room)} sur {len(rows)} : Haut/Bas, molette)",
                color(C_DIM))
        y += 1
        x = 3
        for label, k in self.buttons():
            x = self.button(scr, y, x, f"[ {label} ]", curses.A_BOLD, lambda k=k: self.key(k))
        if self.buttons():
            y += 2
        if rows and not rows[self.sel].section:
            row = rows[self.sel]
            lines = []
            if row.disabled:
                lines.append(("Sans effet ici : " + row.disabled, C_WARN))
            lines += [(l, C_DIM) for l in wrap(row.help, width - 6)] if row.help else []
            for text, pair in lines[:max(0, bottom - y - 1)]:
                put(scr, y, 3, text, color(pair), width=width - 4)
                y += 1
        self.draw_footer(scr, y + 1, bottom, width)

    def select(self, n, then=None):
        self.sel = n
        if then:
            self.key(then)

    def key(self, k):
        rows = self.rows()
        if k == curses.KEY_UP:
            self.sel = (self.sel - 1) % max(1, len(rows))
            self._fix_sel(rows, -1)
        elif k == curses.KEY_DOWN:
            self.sel = (self.sel + 1) % max(1, len(rows))
            self._fix_sel(rows, 1)
        elif k in (ENTER, " ") and rows:
            row = rows[self.sel]
            if row.section or not row.action:
                return True
            if row.disabled:
                self.app.msg(f"{row.label} : sans effet ici ({row.disabled}).", C_WARN)
                return True
            row.action()
        else:
            return False
        return True

    # ---- the profile's setting editors
    def toggle(self, key):
        def go():
            self.p[key] = not self.p[key]
            self.app.save_profile()
        return go

    def choose(self, key, title, items, convert=None):
        """items: (value, label, description)."""
        def go():
            def picked(v):
                self.p[key] = convert(v) if convert else v
                self.app.save_profile()
            self.app.open(ChoiceDialog(title, items, picked, current=self.p[key]))
        return go

    def toggle_perk(self, name):
        def go():
            perks = self.p["start_perks"]
            self.p["start_perks"] = [p for p in perks if p != name] if name in perks else perks + [name]
            self.app.save_profile()
        return go

    def ask(self, key, title, label, help_text, parse, mask=False):
        """parse(text) returns the value to keep, or raises ValueError with why not."""
        def go():
            def ok(v):
                try:
                    self.p[key] = parse(v)
                except ValueError as e:
                    return str(e)
                self.app.save_profile()
                return None
            self.app.open(InputDialog(title, label, self.p[key], help_text, ok, mask=mask))
        return go


def _yes(b):
    return "oui" if b else "non"


def _points(n):
    return f"{n:,}".replace(",", " ") if n else "ceux du jeu (500)"


def _name(text):
    text = text.strip()
    if text and not valid_name(text):
        raise ValueError("1 à 15 lettres, chiffres, « _ », « - » ou « . »")
    return text


# -------------------------------------------------------------------------------------------------------------- Play
class PlayView(RowsView):
    name, title = "play", "Jouer"
    hints = "Entrée modifier  L lancer  S arrêter le jeu  K arrêter le serveur local  ? aide  q quitter"

    def list_height(self, height):
        return 6

    def rows(self):
        p, app = self.p, self.app
        mode = p["mode"]
        rows = [Row("mode", "Mode", H.MODE_LABELS[mode], self.choose(
            "mode", "Mode", [(m, H.MODE_LABELS[m], H.MODE_HELP[m]) for m in H.MODE_LABELS]),
            help_text=H.MODE_HELP[mode])]
        if mode == "solo":
            rows.append(Row("map", "Map", map_label(p["map"], app.root), lambda: self.app.show(2),
                            help_text="La map et les mods se choisissent dans la vue Solo (F3) : Entrée y va."))
            rows.append(Row("mods", "Mods", app.solo_summary(), lambda: self.app.show(2),
                            help_text="Vue Solo (F3)."))
        elif mode == "join":
            target = f"{p['join_host']}:{p['join_port']}" if p["join_host"] else "(aucun : vue Serveurs, F2)"
            rows.append(Row("server", "Serveur", target, self.app.edit_join_target))
            rows.append(Row("join_bo2_weapons", "Armes de Black Ops II", _yes(p["join_bo2_weapons"]),
                            self.toggle("join_bo2_weapons"),
                            disabled=None if bo2_weapons_installed(app.root) else
                            "pas de mods/bo2weapons/mod.ff : à construire depuis votre Black Ops II"))
        else:
            rows.append(Row("host_map", "Map du serveur local", MAP_LABELS.get(p["host_map"], p["host_map"]),
                            self.choose("host_map", "Map du serveur local",
                                        self.app.host_maps())))
            rows.append(Row("host_port", "Port du serveur local", str(p["host_port"]),
                            self.ask("host_port", "Port du serveur local", "Port", "3074 par défaut",
                                     lambda t: _int(t, 1, 65535))))
            rows.append(bo2_row(self, "host_bo2_weapons", app.root, local_running=app.local.running()))
        rows.append(Row("name", "Nom du joueur", p["name"] or "(celui de config.cfg)",
                        self.ask("name", "Nom du joueur", "Nom", "vide : celui de config.cfg", _name)))
        return rows

    def buttons(self):
        out = [("Lancer", "L")]
        if self.app.client and self.app.client.running():
            out.append(("Arrêter le jeu", "S"))
        if self.app.local and self.app.local.running():
            out.append(("Arrêter le serveur local", "K"))
        return out

    def draw_footer(self, scr, y, bottom, width):
        app = self.app
        lines = []
        if not app.dist:
            lines.append((f"{BAD_MARK} Pas de dossier du jeu : ZmClient doit être posé à côté de BO1Zombies "
                          "(un dossier de make_dist.sh), ou lancé avec --dist.", C_ERR))
        else:
            missing = app.setup_missing()
            lines.append((f"{OK_MARK} Fichiers du jeu installés" if not missing
                          else f"{BAD_MARK} Fichiers du jeu : {missing} (vue Installation, F7)",
                          C_OK if not missing else C_ERR))
            if app.client.running():
                lines.append((f"● Jeu en cours (PID {app.client.pid})", C_OK))
            else:
                lines.append(("○ Jeu arrêté", 0))
            if app.local.running():
                lines.append((f"● Serveur local en marche : PID {app.local.pid}, port {app.local.port}, "
                              f"{MAP_LABELS.get(app.local.map, app.local.map)}", C_OK))
            if app.launch_cfg_text():
                lines.append((f"! {os.path.join(app.root, 'launch.cfg')} existe : le jeu ajoute son contenu avant "
                              "cette ligne de commande.", C_WARN))
        for text, pair in lines:
            if y >= bottom:
                return
            put(scr, y, 3, text, color(pair) if pair else 0, width=width - 4)
            y += 1
        if y + 2 >= bottom:
            return
        put(scr, y + 1, 3, "Ligne de commande :", curses.A_BOLD)
        argv, err = app.command()
        text = err or quote_command([os.path.basename(argv[0])] + argv[1:])
        for i, line in enumerate(wrap(text, width - 6)):
            if y + 2 + i >= bottom:
                break
            put(scr, y + 2 + i, 3, line, color(C_ERR if err else C_DIM), width=width - 4)

    def key(self, k):
        if k in ("L", "l"):
            self.app.launch()
        elif k in ("S", "s"):
            self.app.stop_client()
        elif k in ("K", "k"):
            self.app.stop_local()
        else:
            return super().key(k)
        return True


# ----------------------------------------------------------------------------------------------------------- Servers
class ServersView(View):
    name, title = "servers", "Serveurs"
    hints = "Entrée rejoindre  a ajouter  e modifier  x retirer  f en favori  r actualiser  ? aide"

    def __init__(self, app):
        super().__init__(app)
        self.sel = 0

    def entries(self):
        """(kind, favorite or None, (ip, port) or None, Entry or None, error text)."""
        app = self.app
        found, resolved = app.browser.snapshot() if app.browser else ({}, {})
        out, fav_addrs = [], set()
        for f in app.profile["favorites"]:
            res = resolved.get((f["host"], int(f.get("port", DEFAULT_PORT))))
            addr = res if isinstance(res, tuple) else None
            if addr:
                fav_addrs.add(addr)
            out.append(("fav", f, addr, found.get(addr) if addr else None, res if isinstance(res, str) else ""))
        for addr, e in sorted(found.items()):
            if e.lan and addr not in fav_addrs:
                out.append(("lan", None, addr, e, ""))
        return out

    def draw(self, scr, top, height, width):
        entries = self.entries()
        self.sel = max(0, min(self.sel, len(entries) - 1))
        put(scr, top, 1, f"  {'Nom':<24}{'Adresse':<22}{'Map':<20}{'Joueurs':<9}{'Ping':<6}", curses.A_BOLD)
        y = top + 1
        if not entries:
            put(scr, y + 1, 3, "Aucun serveur : « a » pour ajouter un favori. Les serveurs du réseau local "
                               "apparaissent seuls.", color(C_INFO))
            y += 2
        rows = max(3, height - 10)
        start = max(0, self.sel - rows + 1)
        for i, (kind, f, addr, e, err) in enumerate(entries[start:start + rows]):
            n = start + i
            st = e.status if e else None
            if kind == "fav":
                name = "★ " + (f.get("name") or (st.get("sv_hostname") if st else "") or f["host"])
                where = f"{f['host']}:{f.get('port', DEFAULT_PORT)}"
            else:
                name = "  " + (st.get("sv_hostname") if st else "?")
                where = f"{addr[0]}:{addr[1]}"
            if st:
                mapn = MAP_LABELS.get(st.get("mapname"), st.get("mapname"))
                players = f"{len(st.players)}/{st.max_clients}"
                ping = f"{e.ping}"
                pair = 0
            else:
                mapn, players, ping = (err or (e.error if e else "") or "recherche…"), "", ""
                pair = C_DIM
            attr = curses.A_REVERSE if n == self.sel else (color(pair) if pair else 0)
            put(scr, y, 1, f"{name[:23]:<24}{where[:21]:<22}{mapn[:19]:<20}{players:<9}{ping:<6}".ljust(width - 2),
                attr)
            self.zone(y, 1, width - 2, lambda n=n: self.select(n), lambda n=n: self.select(n, ENTER))
            y += 1
        y += 1
        x = 3
        for label, k in (("Rejoindre", ENTER), ("Ajouter", "a"), ("Modifier", "e"), ("Retirer", "x"),
                         ("En favori", "f"), ("Actualiser", "r")):
            x = self.button(scr, y, x, f"[ {label} ]", curses.A_BOLD, lambda k=k: self.key(k))
        y += 2
        if entries:
            kind, f, addr, e, _ = entries[self.sel]
            st = e.status if e else None
            if st and st.players:
                put(scr, y, 3, "Joueurs : " + ", ".join(f"{p.name} ({p.score})" for p in st.players),
                    color(C_INFO), width=width - 4)
                y += 1
        info = ("Réseau local : broadcast getstatus sur les ports 3074 à 3083, toutes les 3 secondes. Un serveur sur "
                "internet s'ajoute à la main ; le client le rejoint avec cl_allowWanConnect 1.")
        for line in wrap(info, width - 6):
            if y >= top + height:
                break
            put(scr, y, 3, line, color(C_DIM))
            y += 1

    def select(self, n, then=None):
        self.sel = n
        if then:
            self.key(then)

    def key(self, k):
        entries = self.entries()
        cur = entries[self.sel] if entries else None
        favs = self.app.profile["favorites"]
        if k == curses.KEY_UP:
            self.sel = max(0, self.sel - 1)
        elif k == curses.KEY_DOWN:
            self.sel = min(len(entries) - 1, self.sel + 1)
        elif k == ENTER and cur:
            kind, f, addr, _, _ = cur
            if kind == "fav":
                self.app.join(f["host"], int(f.get("port", DEFAULT_PORT)), f.get("password", ""))
            else:
                self.app.join(addr[0], addr[1], "")
        elif k == "a":
            self.edit_dialog(None)
        elif k == "e" and cur and cur[0] == "fav":
            self.edit_dialog(cur[1])
        elif k == "f" and cur and cur[0] == "lan":
            st = cur[3].status if cur[3] else None
            favs.append({"name": st.get("sv_hostname") if st else "", "host": cur[2][0], "port": cur[2][1],
                         "password": ""})
            self.app.save_profile()
            self.app.browser.refresh()
            self.app.msg("Ajouté aux favoris.", C_OK)
        elif k in ("x", curses.KEY_DC) and cur and cur[0] == "fav":
            f = cur[1]

            def remove(_):
                favs.remove(f)
                self.app.save_profile()
            self.app.open(ConfirmDialog("Retirer", [f"Retirer {f.get('name') or f['host']} des favoris ?"], remove))
        elif k == "r":
            self.app.browser.refresh()
            self.app.msg("Interrogation des serveurs…", C_INFO)
        else:
            return False
        return True

    def edit_dialog(self, fav):
        f = fav or {}

        def ok(v):
            host = v["host"].strip()
            if not valid_host(host):
                return "adresse : une IP ou un nom de machine (lettres, chiffres, « . », « - »)"
            try:
                port = _int(v["port"], 1, 65535)
            except ValueError as e:
                return f"port : {e}"
            if v["password"] and not valid_password(v["password"]):
                return "mot de passe : ni espace, ni guillemet, ni « ; », ni « + »"
            new = {"name": v["name"].strip(), "host": host, "port": port, "password": v["password"]}
            if fav is None:
                self.app.profile["favorites"].append(new)
            else:
                fav.update(new)
            self.app.save_profile()
            self.app.browser.refresh()
            return None
        self.app.open(FormDialog("Ajouter un favori" if fav is None else "Modifier le favori", [
            ("name", "Nom", f.get("name", ""), "pour s'y retrouver (vide : le nom que le serveur donne)", False),
            ("host", "Adresse", f.get("host", ""), "IP ou nom de la machine, ex. 192.168.1.20", False),
            ("port", "Port", f.get("port", DEFAULT_PORT), "net_port du serveur (3074 par défaut)", False),
            ("password", "Mot de passe", f.get("password", ""), "celui des joueurs (g_password) ; vide s'il n'y en a "
                                                                "pas", True),
        ], ok, intro=["Enregistré dans profile.json (dossier de configuration de ZmClient, lisible par vous seul)."]))


# -------------------------------------------------------------------------------------------------------------- Solo
class SoloView(RowsView):
    name, title = "solo", "Solo"
    hints = "Entrée/Espace modifier  ? aide  q quitter"

    def rows(self):
        p, root = self.p, self.app.root
        bsp, layout = resolve_map(p["map"], root)
        maps = [(v, label + ("" if map_installed(bsp_, root) else "  (non installée)"),
                 (f"{bsp_} — {lay}" if lay else bsp_) + ("" if map_installed(bsp_, root) else
                                                         " : pas de zone dans zone/, votre Black Ops n'a pas ce DLC"))
                for v, label, bsp_, lay in solo_maps(root)]
        no_horde = None if p["horde"] else "le mode horde est désactivé"
        specials = None
        if p["horde"]:
            if layout:
                specials = "pas sur une map Mapkit (Kino et Five seulement)"
            elif bsp == "zombie_temple":
                specials = "Shangri-La les a d'origine"
            elif not specials_available(bsp, layout):
                specials = "Kino et Five seulement"
        else:
            specials = no_horde
        nightmare = None if nightmare_available(bsp, layout) else "Kino et Five seulement"
        if self.p["mode"] != "solo":
            mode_note = "le mode (F1) n'est pas Solo : ces réglages servent à la prochaine partie solo"
        else:
            mode_note = None
        rows = [
            Row("map", "Map", map_label(p["map"], root) + ("" if map_installed(bsp, root) else "  (non installée)"),
                self.choose("map", "Map", maps), disabled=mode_note),
            Row("god", "Invincible (god mode)", _yes(p["god"]), self.toggle("god")),
            Row("zinfo", "Compteur de zombies (Tab)", _yes(p["zinfo"]), self.toggle("zinfo")),
            Row("noperks", "Sans machines à atouts", _yes(p["noperks"]), self.toggle("noperks")),
            Row("quickrestart", "R relance après la fin", _yes(p["quickrestart"]), self.toggle("quickrestart")),
            bo2_row(self, "bo2_weapons", root, bsp=bsp),
            section("Départ"),
            Row("start_points", "Points de départ", _points(p["start_points"]),
                self.choose("start_points", "Points de départ",
                            [(n, _points(n), H.ROW_HELP["start_points"]) for n in START_POINT_CHOICES])),
        ]
        rows += [Row("start_power", "Courant dès le départ", _yes(p["start_power"]), self.toggle("start_power"))]
        no_machines = "sans machines à atouts (noperks) : rien à donner" if p["noperks"] else None
        rows += [Row("start_perk", f"Atout : {label}", _yes(key in p["start_perks"]), self.toggle_perk(key),
                     disabled=no_machines) for key, label in START_PERKS]
        rows += [
            section("Horde"),
            Row("horde", "Mode horde", _yes(p["horde"]), self.toggle("horde")),
            Row("horde_max", "Zombies en même temps", str(p["horde_max"]),
                self.ask("horde_max", "Zombies en même temps", "Nombre", "24 à 1024 ; au-delà de 300 c'est lourd",
                         lambda t: _int(t, 24, 1024)), disabled=no_horde),
            Row("horde_round", "Manche de départ", str(p["horde_round"]),
                self.ask("horde_round", "Manche de départ", "Manche", "1 à 255", lambda t: _int(t, 1, 255)),
                disabled=no_horde),
            Row("horde_napalm", "Zombies napalm", _yes(p["horde_napalm"]), self.toggle("horde_napalm"),
                disabled=specials),
            Row("horde_sonic", "Zombies hurleurs", _yes(p["horde_sonic"]), self.toggle("horde_sonic"),
                disabled=specials),
            Row("horde_spawn_all", "Sortir de toutes les fenêtres", _yes(p["horde_spawn_all"]),
                self.toggle("horde_spawn_all"), disabled=no_horde),
            Row("horde_unjam", "Décoincer les zombies", _yes(p["horde_unjam"]), self.toggle("horde_unjam"),
                disabled=no_horde),
            Row("horde_clear_round", "Manche suivante : tous morts", _yes(p["horde_clear_round"]),
                self.toggle("horde_clear_round"), disabled=no_horde),
            section("Difficulté"),
            Row("difficulty", "Difficulté", H.DIFFICULTY_LABELS[p["difficulty"]],
                self.choose("difficulty", "Difficulté", [
                    (0, "Normal", "le jeu d'origine"),
                    (1, "Nightmare", "ennemis spéciaux de plusieurs maps dès la manche 1"),
                    (2, "Hell", "plusieurs de chaque à la fois")]), disabled=nightmare),
        ]
        return rows

    def draw_footer(self, scr, y, bottom, width):
        if y + 1 >= bottom:
            return
        flags = " ".join(self.app.solo_flags())
        put(scr, y, 3, "Drapeaux des mods :", curses.A_BOLD)
        for i, line in enumerate(wrap(flags or "(aucun)", width - 6)):
            if y + 1 + i >= bottom:
                break
            put(scr, y + 1 + i, 3, line, color(C_DIM), width=width - 4)


def bo2_row(view, key, root, bsp=None, local_running=False):
    """The BO2 weapons row (solo: bo2_weapons, local server: host_bo2_weapons), greyed without mods/bo2weapons/mod.ff,
    on Dead Ops Arcade, or while the local server runs (it took its mods when it started)."""
    labels = dict(BO2_WEAPON_CHOICES)
    if not bo2_weapons_installed(root):
        why = "pas de mods/bo2weapons/mod.ff : à construire depuis votre Black Ops II (docs/BO2_WEAPONS.md)"
    elif bsp is not None and not bo2_weapons_available(bsp):
        why = "Dead Ops Arcade n'a pas de boîte"
    elif local_running:
        why = "le serveur local tourne déjà : il a pris ses mods au lancement"
    else:
        why = None
    return Row(key, "Armes de Black Ops II", labels[view.p[key]],
               view.choose(key, "Armes de Black Ops II", [(v, label, H.BO2_WEAPON_HELP[v])
                                                          for v, label in BO2_WEAPON_CHOICES]),
               disabled=why, help_text=H.ROW_HELP["bo2_weapons"])


# ------------------------------------------------------------------------------------------------------------ Launch
class LaunchView(RowsView):
    name, title = "launch", "Lancement"
    hints = "Entrée/Espace modifier  ? aide  q quitter"

    def tristate(self, key, title):
        return self.choose(key, title, [("", "réglage du jeu", "rien n'est ajouté à la ligne de commande"),
                                        ("1", "oui", ""), ("0", "non", "")])

    def rows(self):
        p, app = self.p, self.app
        aim_off = "l'aide à la visée est désactivée" if p["aim_assist"] == "0" else None
        password = app.local_password()
        rows = [
            Row("name", "Nom du joueur", p["name"] or "(celui de config.cfg)",
                self.ask("name", "Nom du joueur", "Nom", "vide : celui de config.cfg", _name)),
            Row("window", "Affichage", H.WINDOW_LABELS[p["window"]],
                self.choose("window", "Affichage", [(k, v, H.ROW_HELP["window"]) for k, v in H.WINDOW_LABELS.items()])),
            Row("window_size", "Taille de la fenêtre", p["window_size"],
                self.ask("window_size", "Taille de la fenêtre", "Taille", "LARGEURxHAUTEUR, ex. 1600x900",
                         self._size), disabled=None if p["window"] == "window" else "seulement en mode Fenêtre"),
            Row("fps_counter", "Compteur FPS du portage", _yes(p["fps_counter"]), self.toggle("fps_counter")),
            Row("scene_models", "Modèles affichés par image", str(p["scene_models"]),
                self.choose("scene_models", "Modèles affichés par image", [
                    (1024, "1024", "le jeu d'origine"),
                    (2048, "2048", "maps importées (Nuketown) : plus de décor au loin"),
                    (4096, "4096", "maps importées : tout le décor ; un peu plus lourd")])),
            section("Manette : aide à la visée"),
            Row("aim_assist", "Aide à la visée", H.TRISTATE_LABELS[p["aim_assist"]],
                self.tristate("aim_assist", "Aide à la visée")),
            Row("aim_slowdown", "Ralentir sur un zombie", H.TRISTATE_LABELS[p["aim_slowdown"]],
                self.tristate("aim_slowdown", "Ralentir sur un zombie"), disabled=aim_off),
            Row("aim_lockon", "Suivre un zombie", H.TRISTATE_LABELS[p["aim_lockon"]],
                self.tristate("aim_lockon", "Suivre un zombie"), disabled=aim_off),
            Row("aim_autoaim", "Visée automatique (ADS)", H.TRISTATE_LABELS[p["aim_autoaim"]],
                self.tristate("aim_autoaim", "Visée automatique"), disabled=aim_off),
            section("Serveur local (mode Serveur local)"),
            Row("host_map", "Map", MAP_LABELS.get(p["host_map"], p["host_map"]),
                self.choose("host_map", "Map du serveur local", self.app.host_maps())),
            Row("host_port", "Port", str(p["host_port"]),
                self.ask("host_port", "Port du serveur local", "Port", "3074 par défaut", lambda t: _int(t, 1, 65535))),
            bo2_row(self, "host_bo2_weapons", app.root, local_running=app.local.running()),
            Row("host_password", "Mot de passe rcon", "•" * len(password) if password else "(aucun dossier)",
                self.edit_password, disabled=None if app.dist else "pas de dossier du jeu"),
        ]
        return rows

    @staticmethod
    def _size(text):
        if not parse_size(text):
            raise ValueError("LARGEURxHAUTEUR, ex. 1280x720")
        return text.strip()

    def edit_password(self):
        def ok(v):
            if not valid_password(v):
                return "ni espace, ni guillemet, ni « ; », ni « + »"
            self.app.set_local_password(v)
            return None
        self.app.open(InputDialog("Mot de passe rcon du serveur local", "Mot de passe", self.app.local_password(),
                                  "partagé avec ZmAdmin ; pris en compte au prochain démarrage du serveur", ok))


# ------------------------------------------------------------------------------------------------------- Game config
class ConfigView(RowsView):
    name, title = "config", "Config du jeu"
    hints = "Entrée modifier  Ctrl+S sauvegarder  u annuler  n autre dvar  b touches  r relire  ? aide"

    def __init__(self, app):
        super().__init__(app)
        self.edits = {}             # setting or dvar -> new value
        self.bind_edits = {}        # key -> new command ("" removes the bind)
        self.loaded = False

    @property
    def cfg(self):
        return self.app.gamecfg

    def on_show(self):
        if not self.loaded and self.cfg:
            self.cfg.load()
            self.loaded = True

    def rows(self):
        if not self.cfg:
            return [Row(None, "Pas de dossier du jeu", "", None, disabled="ZmClient n'est pas à côté de BO1Zombies")]
        rows = []
        for name, label, kind, opts, help_text in SETTINGS:
            edited = name in self.edits
            value = self.edits[name] if edited else self.cfg.value(name)
            shown = {"0": "0 (non)", "1": "1 (oui)"}.get(value, value) if kind == "bool" else value
            rows.append(Row(name, ("* " if edited else "  ") + label, shown if value != "" else "(défaut du jeu)",
                            lambda n=name: self.edit(n), help_text=f"{name} : {help_text}"))
        others = [n for n in self.edits if n not in BY_NAME]
        if others or self.bind_edits:
            rows.append(section("Autres modifications"))
        for n in others:
            rows.append(Row(n, f"* {n}", self.edits[n], lambda n=n: self.edit(n),
                            help_text=f"Valeur actuelle dans le fichier : « {self.cfg.values.get(n, '')} »."))
        for k, v in self.bind_edits.items():
            rows.append(Row(f"bind:{k}", f"* touche {k}", v or "(retirée)", lambda k=k: self.edit_bind(k),
                            help_text=f"Commande actuelle : « {self.cfg.binds.get(k, '')} »."))
        return rows

    def buttons(self):
        return [("Sauvegarder", CTRL_S), ("Annuler les modifications", "u"), ("Autre dvar", "n"), ("Touches", "b"),
                ("Relire", "r")]

    def draw_header(self, scr, top, width):
        if not self.cfg:
            return top
        state = self.cfg.path if self.cfg.exists else f"{self.cfg.path} (pas encore créé : le jeu l'écrit en quittant)"
        put(scr, top, 1, state, color(C_DIM), width=width - 2)
        if self.app.client and self.app.client.running():
            put(scr, top + 1, 1, "Le jeu tourne : il réécrira ce fichier en quittant. Quittez-le avant de sauvegarder.",
                color(C_WARN), width=width - 2)
        return top + 2

    def edit(self, name):
        setting = BY_NAME.get(name)
        current = self.edits.get(name, self.cfg.value(name))

        def keep(v):
            value, why = validate(name, v)
            if why:
                return why
            if value == self.cfg.value(name):
                self.edits.pop(name, None)
            else:
                self.edits[name] = value
            return None
        if setting and setting[2] == "bool":
            keep("0" if current == "1" else "1")
        elif setting and setting[2] in ("choice", "picmip"):
            self.app.open(ChoiceDialog(setting[1], [(o, o, "") for o in setting[3]], keep, current=current))
        else:
            self.app.open(InputDialog(setting[1] if setting else name, "Valeur", current,
                                      setting[4][:70] if setting else "texte, sans guillemet", keep))

    def other_dvar(self):
        def ok(v):
            name = v["name"].strip()
            if not name or not name.replace("_", "").isalnum():
                return "un nom de dvar : lettres, chiffres, « _ »"
            value, why = validate(name, v["value"])
            if why:
                return why
            self.edits[name] = value
            return None
        self.app.open(FormDialog("Autre dvar", [
            ("name", "Nom", "", "ex. cg_fov_default, r_aspectRatio", False),
            ("value", "Valeur", "", "écrit en « seta nom \"valeur\" »", False),
        ], ok, intro=[f"{len(self.cfg.values)} dvars dans le fichier. Un dvar absent est ajouté."]))

    def edit_bind(self, key=None):
        def set_command(k):
            def ok(v):
                if '"' in v:
                    return "pas de guillemet"
                self.bind_edits[k] = v.strip()
                return None
            self.app.open(InputDialog(f"Touche {k}", "Commande", self.bind_edits.get(k, self.cfg.binds.get(k, "")),
                                      "ex. +attack, weapnext ; vide : retirer la touche", ok))
        if key:
            set_command(key)
            return
        items = [(k, f"{k:<14} {v}", "") for k, v in sorted(self.cfg.binds.items())]
        items.append(("__new__", "(une autre touche…)", "taper son nom : F1, KP_ENTER, MOUSE4, a…"))

        def picked(k):
            if k != "__new__":
                set_command(k)
                return

            def ok(v):
                if not valid_bind_key(v.strip()):
                    return "un nom de touche : lettres, chiffres, « _ »"
                set_command(v.strip())
                return None
            self.app.open(InputDialog("Nouvelle touche", "Touche", "", "son nom pour le jeu, ex. F5, MOUSE4", ok))
        self.app.open(ChoiceDialog("Touches", items, picked))

    def save(self):
        if not self.edits and not self.bind_edits:
            self.app.msg("Rien à sauvegarder.", C_INFO)
            return
        if self.app.client and self.app.client.running():
            self.app.msg("Le jeu tourne : il réécrirait config.cfg en quittant. Quittez-le, puis sauvegardez.", C_ERR)
            return
        values = self.cfg.changes(self.edits)
        binds = {k: v for k, v in self.bind_edits.items() if v}
        unbound = {k for k, v in self.bind_edits.items() if not v}
        try:
            self.cfg.save(values, binds, unbound)
        except OSError as e:
            self.app.msg(f"config.cfg non écrit : {e}", C_ERR)
            return
        n = len(self.edits) + len(self.bind_edits)
        self.edits.clear()
        self.bind_edits.clear()
        self.app.msg(f"config.cfg sauvegardé ({n} modification(s)).", C_OK)

    def key(self, k):
        if not self.cfg:
            return False
        if k == CTRL_S:
            self.save()
        elif k == "u":
            self.edits.clear()
            self.bind_edits.clear()
            self.app.msg("Modifications annulées.", C_INFO)
        elif k == "n":
            self.other_dvar()
        elif k == "b":
            self.edit_bind()
        elif k == "r":
            def reload(_=None):
                self.cfg.load()
                self.edits.clear()
                self.bind_edits.clear()
                self.app.msg("config.cfg relu.", C_INFO)
            if self.edits or self.bind_edits:
                self.app.open(ConfirmDialog("Relire", ["Relire config.cfg et perdre les modifications ?"], reload))
            else:
                reload()
        else:
            return super().key(k)
        return True


# ------------------------------------------------------------------------------------------------------------- Extra
class ExtraView(View):
    name, title = "extra", "Dvars libres"
    hints = "a ajouter  e modifier  x retirer  Espace activer  ? aide"

    def __init__(self, app):
        super().__init__(app)
        self.sel = 0

    @property
    def lines(self):
        return self.app.profile["extra"]

    def draw(self, scr, top, height, width):
        lines = self.lines
        self.sel = max(0, min(self.sel, len(lines) - 1))
        put(scr, top, 1, "Commandes ajoutées à la ligne de commande, dans l'ordre, avant la map ou la connexion :",
            curses.A_BOLD)
        y = top + 2
        if not lines:
            put(scr, y, 3, "Aucune : « a » pour en ajouter une (ex. « set bo1_frameperf 1 »).", color(C_INFO))
            y += 1
        for i, e in enumerate(lines[:max(0, height - 8)]):
            mark = "[x]" if e.get("on") else "[ ]"
            why = check_extra(e["text"])
            attr = curses.A_REVERSE if i == self.sel else (color(C_ERR) if why else 0)
            put(scr, y, 1, f"  {mark} +{e['text']}".ljust(width - 2), attr)
            self.zone(y, 1, 6, lambda i=i: self.select(i, " "))
            self.zone(y, 7, width - 8, lambda i=i: self.select(i), lambda i=i: self.select(i, "e"))
            y += 1
        y += 1
        x = 3
        for label, k in (("Ajouter", "a"), ("Modifier", "e"), ("Retirer", "x"), ("Activer/désactiver", " ")):
            x = self.button(scr, y, x, f"[ {label} ]", curses.A_BOLD, lambda k=k: self.key(k))
        y += 2
        for line in wrap("Chaque ligne devient « +ligne ». Pas de « + » dans une ligne : le jeu coupe sa ligne de "
                         "commande à chaque « + » et n'en garde que 63 ; ZmClient refuse de lancer au-delà.",
                         width - 6):
            put(scr, y, 3, line, color(C_DIM))
            y += 1

    def select(self, i, then=None):
        self.sel = i
        if then:
            self.key(then)

    def edit(self, entry):
        def ok(v):
            why = check_extra(v)
            if why:
                return why
            if entry is None:
                self.lines.append({"on": True, "text": v.strip()})
                self.sel = len(self.lines) - 1
            else:
                entry["text"] = v.strip()
            self.app.save_profile()
            return None
        self.app.open(InputDialog("Ajouter une commande" if entry is None else "Modifier la commande", "Commande",
                                  entry["text"] if entry else "set ",
                                  "sans le « + » : « set r_fog 0 », « set bo1_frameperf 1 »", ok))

    def key(self, k):
        lines = self.lines
        cur = lines[self.sel] if lines else None
        if k == curses.KEY_UP:
            self.sel = max(0, self.sel - 1)
        elif k == curses.KEY_DOWN:
            self.sel = min(len(lines) - 1, self.sel + 1)
        elif k == "a":
            self.edit(None)
        elif k in ("e", ENTER) and cur:
            self.edit(cur)
        elif k == " " and cur:
            cur["on"] = not cur.get("on")
            self.app.save_profile()
        elif k in ("x", curses.KEY_DC) and cur:
            def remove(_):
                lines.remove(cur)
                self.app.save_profile()
            self.app.open(ConfirmDialog("Retirer", [f"Retirer « +{cur['text']} » ?"], remove))
        else:
            return False
        return True


# ------------------------------------------------------------------------------------------------------------- Setup
class SetupView(View):
    name, title = "setup", "Installation"
    hints = "c choisir l'installation  v vidéos  z zones  i installer  Échap annuler  r actualiser  ? aide"

    def __init__(self, app):
        super().__init__(app)
        self.state = None
        self.state_at = 0.0
        self.installs = None

    def on_show(self):
        self.state_at = 0.0
        if self.installs is None:
            self.installs = gamesetup.find_installs()
            if not self.app.profile["steam_install"] and self.installs:
                self.app.profile["steam_install"] = self.installs[0]
                self.app.save_profile()

    def draw(self, scr, top, height, width):
        app = self.app
        if not app.dist:
            put(scr, top + 1, 3, "Pas de dossier du jeu : ZmClient doit être à côté de BO1Zombies (ou --dist).",
                color(C_ERR))
            return
        now = time.monotonic()
        job = app.setup_job
        if self.state is None or now - self.state_at > 3 or (job and job.finished and self.state_at < job.ended):
            self.state = gamesetup.state(app.root)
            self.state_at = now
        p = app.profile
        install = p["steam_install"]
        y = top
        put(scr, y, 1, "Installation de Black Ops :", curses.A_BOLD)
        valid = gamesetup.is_install(install)
        put(scr, y, 29, install or "(aucune trouvée : « c » pour la taper)",
            color(C_OK if valid else C_ERR), width=width - 30)
        y += 1
        put(scr, y, 29, "lue seulement, jamais modifiée" if valid else
            "pas une installation (zone/, main/*.iwd, localization.txt attendus)", color(C_DIM), width=width - 30)
        y += 1
        put(scr, y, 1, f"Dossier du paquet :          {app.root}", color(C_DIM), width=width - 2)
        y += 2
        labels = {"localization": "localization.txt", "iwd": "main/*.iwd", "video": "main/video/",
                  "zones": "zone/ (converties)", "pack": "shaders.pack"}
        for part, (ok, text) in self.state.items():
            mark, pair = (OK_MARK, C_OK) if ok else (BAD_MARK, C_WARN if part == "video" else C_ERR)
            put(scr, y, 3, f"{mark} {labels[part]:<20}", color(pair))
            put(scr, y, 27, text, 0, width=width - 28)
            y += 1
        y += 1
        put(scr, y, 3, f"[{'x' if p['setup_video'] else ' '}] copier les vidéos (cinématiques, ~1 Go)",
            curses.A_BOLD if not job else color(C_DIM))
        self.zone(y, 3, 45, lambda: self.key("v"))
        y += 1
        put(scr, y, 3, f"[{'x' if p['setup_convert'] else ' '}] convertir les zones (ffconv, ~6 minutes)",
            curses.A_BOLD if not job else color(C_DIM))
        self.zone(y, 3, 45, lambda: self.key("z"))
        y += 2
        x = 3
        running = job and not job.finished
        buttons = [("Annuler l'installation", ESC)] if running else [("Choisir l'installation", "c"),
                                                                        ("Installer", "i"), ("Actualiser", "r")]
        for label, k in buttons:
            x = self.button(scr, y, x, f"[ {label} ]", curses.A_BOLD, lambda k=k: self.key(k))
        y += 2
        if job:
            if job.phase == "copie" and job.total_bytes:
                pct = 100 * job.done_bytes // job.total_bytes
                status = f"Copie : {job.done_bytes >> 20} / {job.total_bytes >> 20} Mo ({pct} %)"
            elif job.phase == "conversion des zones" and job.zone_count:
                status = f"Conversion : zone {job.zone_at} / {job.zone_count}"
            else:
                status = job.phase.capitalize()
            if job.finished:
                status = f"Échec : {job.error}" if job.error else "Installation terminée."
            put(scr, y, 3, status, color(C_ERR if job.finished and job.error else C_OK if job.finished else C_INFO),
                width=width - 4)
            y += 1
            for line in job.lines[-max(0, top + height - y - 1):]:
                put(scr, y, 5, line, color(C_DIM), width=width - 6)
                y += 1

    def choose_install(self):
        def keep(path):
            path = path.strip().strip('"')
            if not gamesetup.is_install(path):
                return "pas une installation de Black Ops (zone/, main/*.iwd, localization.txt)"
            self.app.profile["steam_install"] = os.path.normpath(os.path.abspath(path))
            self.app.save_profile()
            return None

        def typed():
            self.app.open(InputDialog("Installation de Black Ops", "Dossier", self.app.profile["steam_install"],
                                      "le dossier qui contient BlackOps.exe", keep))

        def browse():
            start = start_folder(self.app.profile["steam_install"], self.installs or [])
            self.app.open(FolderDialog("Choisir le dossier de Black Ops", start, keep))
        items = [(p, p, "trouvée dans une bibliothèque Steam") for p in self.installs or []]
        items.append(("__browse__", "(parcourir les dossiers…)", "naviguer dans les dossiers avec les flèches"))
        items.append(("__typed__", "(taper le chemin…)", "un autre dossier"))

        def picked(v):
            if v == "__browse__":
                browse()
            elif v == "__typed__":
                typed()
            else:
                self.app.profile["steam_install"] = v
                self.app.save_profile()
        self.app.open(ChoiceDialog("Installation de Black Ops", items, picked,
                                   current=self.app.profile["steam_install"]))

    def key(self, k):
        app = self.app
        running = app.setup_job and not app.setup_job.finished
        if k == ESC:
            if running:
                app.setup_job.cancel()
                app.msg("Annulation…", C_WARN)
                return True
            return False
        if running:
            if k in ("c", "i", "v", "z"):
                app.msg("Une installation est en cours.", C_WARN)
                return True
            return False
        if k == "c":
            self.choose_install()
        elif k == "v":
            app.profile["setup_video"] = not app.profile["setup_video"]
            app.save_profile()
        elif k == "z":
            app.profile["setup_convert"] = not app.profile["setup_convert"]
            app.save_profile()
        elif k == "r":
            self.installs = gamesetup.find_installs()
            self.state_at = 0.0
        elif k == "i":
            app.start_setup()
        else:
            return False
        return True


# --------------------------------------------------------------------------------------------------------------- Log
class LogView(AdminLogView):
    name, title = "log", "Journal"

    def draw(self, scr, top, height, width):
        path = self.app.log_path
        if not path:
            put(scr, top + 1, 3, "Pas de dossier du jeu : rien à suivre.", color(C_INFO))
            return
        shown = [l for l in self.lines if self.filter.lower() in l.lower()] if self.filter else list(self.lines)
        rows = height - 1
        self.scroll = max(0, min(self.scroll, len(shown) - rows))
        end = len(shown) - self.scroll
        if not shown:
            put(scr, top + 1, 3, "Rien encore : le journal apparaît quand le jeu écrit son console_mp.log.",
                color(C_INFO))
        for i, line in enumerate(shown[max(0, end - rows):end]):
            low = line.lower()
            pair = C_ERR if "error" in low else C_WARN if "warning" in low else 0
            put(scr, top + i, 1, line, color(pair) if pair else 0, width=width - 2)
        state = []
        if self.filter:
            state.append(f"filtre « {self.filter} » : {len(shown)} lignes")
        if self.paused:
            state.append("EN PAUSE (Espace pour reprendre)")
        elif self.scroll:
            state.append(f"{self.scroll} lignes plus bas (Fin)")
        buttons = (("[ Filtrer ]", "/"), ("[ Reprendre ]" if self.paused else "[ Pause ]", " "),
                   ("[ Fin ]", curses.KEY_END))
        bw = sum(len(b) + 1 for b, _ in buttons)
        put(scr, top + height - 1, 1, " | ".join(state) or path, color(C_WARN if state else C_DIM),
            width=width - 3 - bw)
        x = width - bw
        for label, k in buttons:
            put(scr, top + height - 1, x, label, curses.A_BOLD)
            self.zone(top + height - 1, x, len(label), lambda k=k: self.key(k))
            x += len(label) + 1

