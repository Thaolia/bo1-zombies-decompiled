"""The six views. Each draws itself in the body area and handles the keys the app does not take."""
import collections
import curses
import re

from . import help as H
from .model import parse_dvars
from .settings import APPLIED, BY_NAME, REFUSED, SETTINGS, validate
from .widgets import (BAD_MARK, BTAB, C_DIM, C_ERR, C_INFO, C_OK, C_WARN, CTRL_S, ENTER, OK_MARK, TAB, ChecklistDialog,
                      ChoiceDialog, ConfirmDialog, FormDialog, InputDialog, LineEdit, color, put, wrap)

_SAFE_TOKEN = re.compile(r'^[^\s";]+$')


class View:
    name = ""
    title = ""
    captures_text = False
    uses_admin = False
    hints = ""

    def __init__(self, app):
        self.app = app

    def on_show(self):
        pass

    def draw(self, scr, top, height, width):
        raise NotImplementedError

    def key(self, k):
        return False

    def wheel(self, step):
        """The mouse wheel, -1 up, 1 down: moves the selection by default."""
        self.key(curses.KEY_UP if step < 0 else curses.KEY_DOWN)

    def zone(self, y, x, width, click, double=None):
        self.app.hits.add(y, x, width, click, double)

    def button(self, scr, y, x, text, attr, click):
        """A clickable label; returns the column after it."""
        put(scr, y, x, text, attr)
        self.zone(y, x, len(text), click)
        return x + len(text) + 2


def _int_field(text, minimum=None):
    text = text.strip()
    if not re.fullmatch(r"-?\d+", text):
        raise ValueError("un nombre entier")
    n = int(text)
    if minimum is not None and n < minimum:
        raise ValueError(f"au moins {minimum}")
    return n


# ---------------------------------------------------------------------------------------------------------- Servers
class ServersView(View):
    name, title = "servers", "Serveurs"
    hints = "Entrée/double-clic connecter  a ajouter  e modifier  x retirer  l lancer local  S arrêter local  ? aide"

    def __init__(self, app):
        super().__init__(app)
        self.sel = 0

    def entries(self):
        items = [("local", None)] if self.app.dist else []
        items += [("saved", s) for s in self.app.config.servers]
        return items

    def draw(self, scr, top, height, width):
        app = self.app
        entries = self.entries()
        self.sel = max(0, min(self.sel, len(entries) - 1))
        put(scr, top, 1, f"{'':2}{'Nom':<26}{'Adresse':<28}État", curses.A_BOLD)
        y = top + 1
        if not entries:
            put(scr, y + 1, 3, "Aucun serveur enregistré : « a » pour en ajouter un.", color(C_INFO))
        for i, (kind, entry) in enumerate(entries):
            if kind == "local":
                local = app.local
                port = local.port if local.running() else app.config.local_settings(app.dist.root)["port"]
                name, addr = "Serveur local", f"127.0.0.1:{port}"
                state = f"en marche (PID {local.pid}, {H.map_label(local.map or '')})" if local.running() \
                    else "arrêté"
            else:
                name, addr = entry.get("name") or entry["host"], f"{entry['host']}:{entry.get('port', 3074)}"
                state = ""
            if app.link and app.connected_to == (kind, entry):
                state = ("connecté" if app.link.online else "hors ligne") + (f", {state}" if state else "")
            attr = curses.A_REVERSE if i == self.sel else 0
            put(scr, y, 1, f"{'':2}{name[:25]:<26}{addr[:27]:<28}{state}".ljust(width - 2), attr)
            self.zone(y, 1, width - 2, lambda i=i: self.select(i), lambda i=i: self.select(i, ENTER))
            y += 1
        y += 1
        rows = [[("Connecter", ENTER, bool(entries)), ("Ajouter", "a", True), ("Modifier", "e", bool(entries)),
                 ("Retirer", "x", bool(entries))],
                [("Lancer le serveur local", "l", bool(app.dist)), ("Arrêter le serveur local", "S", bool(app.dist))]]
        for row in rows:
            x = 3
            for label, k, shown in row:
                if shown:
                    x = self.button(scr, y, x, f"[ {label} ]", 0, lambda k=k: self.key(k))
            if x > 3:
                y += 1
        y += 1
        if app.dist:
            text = [f"Dossier BO1Zombies : {app.dist.root}",
                    "Le serveur local se lance depuis ce dossier, comme server.sh / Server.bat, avec un mot de passe "
                    "rcon que ZmAdmin choisit et garde (« l » pour le voir ou le changer). Son log s'affiche en direct "
                    "dans les vues Console et Log."]
        else:
            text = ["Pas de dossier BO1Zombies ici : ZmAdmin pilote des serveurs à distance. Posé à côté de "
                    "BO1Zombies et de server.sh / Server.bat (un dossier de make_dist.sh), il peut aussi en lancer un."]
        for paragraph in text:
            for line in wrap(paragraph, width - 6):
                put(scr, y, 3, line, color(C_DIM), width=width - 4)
                y += 1

    def select(self, i, then=None):
        self.sel = i
        if then:
            self.key(then)

    def key(self, k):
        entries = self.entries()
        if k == curses.KEY_UP:
            self.sel -= 1
        elif k == curses.KEY_DOWN:
            self.sel += 1
        elif k == ENTER and entries:
            kind, entry = entries[self.sel]
            if kind == "local":
                if self.app.local.running():
                    self.app.connect_local()
                else:
                    self.launch_dialog()
            else:
                self.app.connect_saved(entry)
        elif k == "a":
            self.edit_dialog(None)
        elif k == "e" and entries and entries[self.sel][0] == "saved":
            self.edit_dialog(entries[self.sel][1])
        elif k in ("x", curses.KEY_DC) and entries and entries[self.sel][0] == "saved":
            entry = entries[self.sel][1]

            def remove(_):
                self.app.config.servers.remove(entry)
                self.app.config.save()
            self.app.open(ConfirmDialog("Retirer", [f"Retirer {entry.get('name') or entry['host']} de la liste ?"],
                                        remove))
        elif k == "l" and self.app.dist:
            self.launch_dialog()
        elif k == "S" and self.app.dist:
            self.app.stop_local()
        else:
            return False
        return True

    def edit_dialog(self, entry):
        e = entry or {}

        def ok(v):
            host = v["host"].strip()
            if not host:
                return "l'adresse est vide"
            try:
                port = _int_field(v["port"], 1)
            except ValueError as err:
                return f"port : {err}"
            if v["password"] and not _SAFE_TOKEN.match(v["password"]):
                return "le mot de passe ne peut contenir ni espace, ni guillemet, ni point-virgule"
            new = {"name": v["name"].strip(), "host": host, "port": port, "password": v["password"]}
            if entry is None:
                self.app.config.servers.append(new)
            else:
                entry.update(new)
            self.app.config.save()
            return None
        self.app.open(FormDialog("Ajouter un serveur" if entry is None else "Modifier le serveur", [
            ("name", "Nom", e.get("name", ""), "pour s'y retrouver dans la liste", False),
            ("host", "Adresse", e.get("host", ""), "IP ou nom de la machine, ex. 192.168.1.20", False),
            ("port", "Port", e.get("port", 3074), "net_port du serveur (3074 par défaut)", False),
            ("password", "Mot de passe rcon", e.get("password", ""), "le rcon_password du serveur", True),
        ], ok, intro=["Enregistré dans servers.json (dossier de configuration de ZmAdmin, lisible par vous seul)."]))

    def launch_dialog(self):
        app = self.app
        if app.local.running():
            app.msg(f"Le serveur local tourne déjà (PID {app.local.pid}).", C_WARN)
            return
        local = app.config.local_settings(app.dist.root)
        maps = [(code, f"{name:<20} {code}", "") for code, name in H.MAPS]

        def picked(map_name):
            def ok(v):
                try:
                    port = _int_field(v["port"], 1)
                except ValueError as err:
                    return f"port : {err}"
                if not re.fullmatch(r"[A-Za-z0-9_.-]+", v["password"]):
                    return "mot de passe : lettres, chiffres, - _ . seulement, et pas vide"
                local.update(map=map_name, port=port, password=v["password"])
                app.config.save()
                return app.launch_local(map_name, port, v["password"])
            app.open(FormDialog(f"Lancer {H.map_label(map_name)}", [
                ("port", "Port UDP", local["port"], "3074 par défaut ; à ouvrir dans le pare-feu", False),
                ("password", "Mot de passe rcon", local["password"],
                 "choisi par ZmAdmin, gardé pour ce dossier", False),
            ], ok, intro=["Le serveur démarre sans fenêtre et charge la map (une minute environ).",
                          "Son log : server/main/console_mp.log, visible dans la vue Log."]))
        app.open(ChoiceDialog("Map du serveur local", maps, picked, current=local["map"]))


# ---------------------------------------------------------------------------------------------------------- Players
class PlayersView(View):
    name, title = "players", "Joueurs"
    uses_admin = True
    hints = "Haut/Bas, clic : un joueur  lettre ou clic sur la légende : action  a : tous  ? : aide détaillée"

    def __init__(self, app):
        super().__init__(app)
        self.sel_slot = None
        self.all_mode = False

    def draw(self, scr, top, height, width):
        app = self.app
        rows = app.rows
        if rows and self.sel_slot not in [r.slot for r in rows]:
            self.sel_slot = rows[0].slot
        put(scr, top, 1, f"{'#':>3}  {'Nom':<22}{'État':<12}{'Vie':>5}{'Points':>8}{'Ping':>6}  Drapeaux", curses.A_BOLD)
        y = top + 1
        if not app.link:
            put(scr, y + 1, 3, "Pas connecté : F1 pour choisir un serveur.", color(C_INFO))
        elif not rows:
            put(scr, y + 1, 3, "Aucun joueur sur le serveur." if app.link.online else "Le serveur ne répond pas.",
                color(C_INFO))
        flag_letters = {"god": "G", "demigod": "D", "notarget": "N", "noclip": "C", "frozen": "F"}
        for r in rows[:max(0, height - 12)]:
            state = r.display_state
            state_attr = {"down": color(C_WARN), "dead": color(C_ERR), "spectator": color(C_ERR),
                          "playing": color(C_OK)}.get(state, color(C_INFO))
            flags = " ".join(flag_letters[f] for f in r.flags if f in flag_letters)
            health = "" if r.health is None else str(r.health)
            sel = r.slot == self.sel_slot
            attr = curses.A_REVERSE if sel else 0
            put(scr, y, 1, " " * (width - 2), attr)
            put(scr, y, 1, f"{r.slot:>3}  {r.name[:21]:<22}", attr)
            put(scr, y, 28, f"{state:<12}", attr | (0 if sel else state_attr))
            put(scr, y, 40, f"{health:>5}{r.score:>8}{r.ping:>6}  {flags}", attr)
            self.zone(y, 1, width - 2, lambda slot=r.slot: self.select(slot))
            y += 1
        y = top + height - 10
        # the legend doubles as buttons: a click runs the action on the target below
        for i, (key, label, _, _) in enumerate(H.PLAYER_ACTIONS):
            ly, lx = y + i // 4, 1 + 16 * (i % 4)
            put(scr, ly, lx, f"{key} {label}"[:15], color(C_INFO))
            self.zone(ly, lx, 15, lambda key=key: self.key(key))
        y += (len(H.PLAYER_ACTIONS) + 3) // 4 + 1
        if app.status:
            put(scr, y, 1, f"{len(rows)}/{app.status.max_clients} places occupées", color(C_DIM))
        target = "TOUS les joueurs (all)" if self.all_mode else (
            f"joueur {self.sel_slot}" if self.sel_slot is not None else "aucun joueur")
        line = f"Cible de la prochaine action : {target}"
        put(scr, y + 1, 1, line, color(C_WARN) if self.all_mode else 0)
        self.button(scr, y + 1, len(line) + 3, f"[{'x' if self.all_mode else ' '}] tous (a)", 0,
                    lambda: self.key("a"))
        note = ("Drapeaux : G god, D demigod, N notarget, C noclip, F gelé. État et vie viennent de zm_admin, relu "
                "toutes les quelques secondes : un changement apparaît après l'action du mod admin.")
        for i, line in enumerate(wrap(note, width - 3)[:3]):
            put(scr, y + 2 + i, 1, line, color(C_DIM), width=width - 2)

    def select(self, slot):
        self.sel_slot = slot

    def key(self, k):
        rows = self.app.rows
        slots = [r.slot for r in rows]
        if k in (curses.KEY_UP, curses.KEY_DOWN) and slots:
            i = slots.index(self.sel_slot) if self.sel_slot in slots else 0
            i = (i + (-1 if k == curses.KEY_UP else 1)) % len(slots)
            self.sel_slot = slots[i]
            return True
        if k == "a":
            self.all_mode = not self.all_mode
            self.app.msg("La prochaine action vise tous les joueurs." if self.all_mode else "Action sur le joueur choisi.",
                         C_WARN if self.all_mode else C_INFO)
            return True
        action = next((a for a in H.PLAYER_ACTIONS if a[0] == k), None)
        if action is None:
            return False
        if not self.app.require_link():
            return True
        if not self.all_mode and self.sel_slot is None:
            self.app.msg("Aucun joueur choisi.", C_WARN)
            return True
        target = "all" if self.all_mode else str(self.sel_slot)
        who = "tous les joueurs" if self.all_mode else next((f"{r.name} (#{r.slot})" for r in rows
                                                              if r.slot == self.sel_slot), target)
        _, label, cmd, text = action
        self.all_mode = False
        if cmd == "clientkick" and target == "all":
            self.app.msg("Kick : un joueur à la fois.", C_WARN)
        elif cmd in ("zm_points", "zm_setpoints"):
            def ok(v):
                try:
                    n = _int_field(v, None if cmd == "zm_points" else 0)
                except ValueError as err:
                    return str(err)
                self.run(f"{cmd} {target} {n}")
                return None
            self.app.open(InputDialog(f"{label} : {who}", "Montant", "", text, ok))
        elif cmd == "zm_perk":
            items = [("all", "Tous les perks de la map")] + H.PERKS

            def ok(perks):
                if perks:
                    self.run(f"zm_perk {target} {' '.join(perks)}")
            self.app.open(ChecklistDialog(f"Perks : {who}", items, [], ok, intro=[text]))
        elif cmd in H.DESTRUCTIVE:
            self.app.open(ConfirmDialog(label.capitalize(), [f"{label.capitalize()} : {who} ?", text],
                                        lambda _: self.run(f"{cmd} {target}")))
        else:
            self.run(f"{cmd} {target}")
        return True

    def run(self, command):
        self.app.send(command, tag="action")
        self.app.link.refresh_admin(1.0)


# ------------------------------------------------------------------------------------------------------------- Game
class GameView(View):
    name, title = "game", "Partie"
    uses_admin = True
    hints = "Haut/Bas, clic : choisir une action  Entrée, double-clic, [ Exécuter ] : exécuter  ? aide"

    def __init__(self, app):
        super().__init__(app)
        self.sel = 0

    def draw(self, scr, top, height, width):
        app = self.app
        st, rep = app.status, app.report
        info = [
            ("Serveur", st.get("sv_hostname") if st else "-"),
            ("Map", H.map_label(st.get("mapname")) if st else "-"),
            ("Joueurs", f"{len(app.rows)}/{st.max_clients}" if st else "-"),
            ("Mods", (rep.settings["fs_mods"] or "aucun") + " (+ admin, toujours)" if rep else "-"),
            ("Départ", f"points {rep.settings['zm_startpoints']}, perks « {rep.settings['zm_startperks']} », "
                       f"joueur qui rejoint : {rep.settings['zm_jip_mode']}" if rep else "-"),
        ]
        y = top
        for label, value in info:
            put(scr, y, 2, f"{label:<9}", curses.A_BOLD)
            put(scr, y, 11, value, width=width - 12)
            y += 1
        y += 1
        put(scr, y, 2, "Actions", curses.A_BOLD)
        y += 1
        for i, (_, label, _) in enumerate(H.GAME_ACTIONS):
            put(scr, y, 2, f" {label} ".ljust(36), curses.A_REVERSE if i == self.sel else 0)
            self.zone(y, 2, 36, lambda i=i: self.select(i), lambda i=i: self.select(i, ENTER))
            if i == self.sel:
                self.button(scr, y, 40, "[ Exécuter ]", curses.A_BOLD, lambda: self.key(ENTER))
            y += 1
        y += 1
        for line in wrap(H.GAME_ACTIONS[self.sel][2], width - 6):
            put(scr, y, 4, line, color(C_DIM))
            y += 1

    def select(self, i, then=None):
        self.sel = i
        if then:
            self.key(then)

    def key(self, k):
        if k == curses.KEY_UP:
            self.sel = (self.sel - 1) % len(H.GAME_ACTIONS)
        elif k == curses.KEY_DOWN:
            self.sel = (self.sel + 1) % len(H.GAME_ACTIONS)
        elif k == ENTER:
            if self.app.require_link():
                self.run(H.GAME_ACTIONS[self.sel])
        else:
            return False
        return True

    def run(self, action):
        app = self.app
        key, label, text = action
        if key == "round":
            def ok(v):
                try:
                    n = _int_field(v, 1)
                except ValueError as err:
                    return str(err)
                app.send(f"zm_round {n}", tag="action")
                return None
            app.open(InputDialog(label.rstrip("…"), "Round", "", text, ok))
        elif key == "power":
            app.send("zm_power", tag="action")
        elif key == "reset":
            app.open(ConfirmDialog(label, ["Recharger la map et recommencer la partie au round 1 ?", text],
                                   lambda _: app.send("zm_reset", tag="action")))
        elif key == "map":
            current = app.status.get("mapname") if app.status else None
            maps = [(code, f"{name:<20} {code}", "") for code, name in H.MAPS]

            def picked(code):
                app.open(ConfirmDialog("Changer de map", [f"Charger {H.map_label(code)} ?", text],
                                       lambda _: app.send(f"zm_map {code}", tag="action")))
            app.open(ChoiceDialog("Changer de map", maps, picked, current=current))
        elif key == "mods":
            current = app.report.settings["fs_mods"].split() if app.report else []
            app.edit_mods(current, lambda mods: app.open(ConfirmDialog(
                "Changer les mods", [f"fs_mods « {mods} » et recharger la map ?", text],
                lambda _: app.send(f'zm_mods "{mods}"', tag="action"))))
        elif key == "admin":
            app.send("zm_admin", tag="console")
            app.msg("La réponse s'affiche dans la Console (F5).", C_INFO)


# --------------------------------------------------------------------------------------------------------- Settings
class SettingsView(View):
    name, title = "settings", "Réglages"
    hints = "Haut/Bas, clic : choisir  Entrée, double-clic : modifier  Espace basculer  Ctrl+S sauvegarder  ? aide"
    BUTTON_HELP = {
        "save": ("Sauvegarder", "Envoie au serveur toutes les modifications marquées *, en une seule commande rcon, "
                 "puis relit chaque réglage pour vérifier qu'il est appliqué. Une fenêtre montre d'abord les commandes."),
        "cancel": ("Annuler", "Oublie les modifications pas encore envoyées : chaque réglage revient à la valeur "
                   "du serveur."),
        "reload": ("Relire", "Relit toutes les valeurs sur le serveur (par exemple après une commande tapée dans la "
                   "Console ou un autre admin)."),
        "cfg": ("Écrire aussi dans server.cfg", "Cochée : Sauvegarder écrit aussi les valeurs appliquées dans "
                "server/main/server.cfg du dossier local, pour qu'elles restent au prochain lancement du serveur. "
                "Les autres lignes du fichier ne changent pas."),
    }

    def __init__(self, app):
        super().__init__(app)
        self.focus = 0
        self.loaded = False
        self.write_cfg = False
        self.saving = None          # dict while a Save runs

    def focus_items(self):
        items = [("setting", s.name) for s in SETTINGS] + [("button", "save"), ("button", "cancel"),
                                                            ("button", "reload")]
        if self.app.is_local_link():
            items.append(("button", "cfg"))
        return items

    def on_show(self):
        if self.app.link and not self.loaded:
            self.read_all()

    def read_all(self):
        if self.app.require_link():
            self.app.send(";".join(s.name for s in SETTINGS), tag="settings-read")

    def on_reply(self, tag, text, error):
        form = self.app.form
        if tag == "settings-read":
            if text is not None:
                form.load(parse_dvars(text))
                self.loaded = True
            return
        saving = self.saving
        if saving is None:
            return
        if tag == "save-set":
            saving["output"] = "" if text is None else text.strip()
            if error:
                self.saving = None
                self.app.msg(f"Sauvegarde interrompue : {error}", C_ERR)
                return
            if "rcon_password" in saving["edits"]:
                self.app.password_changed(saving["edits"]["rcon_password"])
            names = [n for n in saving["edits"] if n != "fs_mods"]
            if names:
                self.app.send(";".join(names), tag="save-read")
            else:
                self.on_reply("save-read", "", "")
        elif tag == "save-read":
            output = saving["output"]
            names = [n for n in saving["edits"] if n != "fs_mods"]
            form.check(parse_dvars(text or ""), {n: output for n in names} if output else {}, names)
            applied = {n: saving["edits"][n] for n in names if form.status.get(n, ("",))[0] == APPLIED}
            refused = [n for n in names if form.status.get(n, ("",))[0] == REFUSED]
            if "fs_mods" in saving["edits"]:
                # zm_mods only sets the dvar and reloads: its reply confirms it (save-reload)
                applied["fs_mods"] = saving["edits"]["fs_mods"]
            if saving["write_cfg"] and applied:
                self.app.write_server_cfg(applied)
            for cmd in saving["reload"]:
                self.app.send(cmd, tag="save-reload")
            self.saving = None if not saving["reload"] else saving
            if refused:
                self.app.msg(f"Refusé par le serveur : {', '.join(refused)} (voir {BAD_MARK}).", C_ERR)
            else:
                self.app.msg(f"{len(applied)} réglage(s) appliqué(s)."
                             + (" La map recharge." if saving["reload"] else ""), C_OK)
        elif tag == "save-reload":
            self.saving = None
            if "fs_mods" in saving["edits"]:
                mods = saving["edits"]["fs_mods"]
                if text is not None and 'zm_admin: fs_mods "' in text:
                    form.server["fs_mods"] = mods
                    form.edits.pop("fs_mods", None)
                    form.status["fs_mods"] = (APPLIED, "")
                else:
                    form.status["fs_mods"] = (REFUSED, (text or error or "pas de réponse").strip())

    def draw(self, scr, top, height, width):
        app = self.app
        form = app.form
        items = self.focus_items()
        self.focus = max(0, min(self.focus, len(items) - 1))
        if not app.link:
            put(scr, top + 1, 3, "Pas connecté : F1 pour choisir un serveur.", color(C_INFO))
            return
        y = top
        put(scr, y, 1, f"  {'Réglage':<36}{'Valeur':<21}Dvar", curses.A_BOLD)
        if not self.loaded:
            put(scr, y, 66, "lecture…", color(C_WARN))
        y += 1
        for s in SETTINGS:
            label = H.SETTINGS_HELP[s.name][0]
            value = form.value(s.name)
            if s.kind == "bool":
                # a script dvar nobody set yet reads as empty: the script takes it as 0
                value = "oui" if value == "1" else "non"
            if s.kind == "password" and value:
                value = "*" * len(value)
            mark = "*" if s.name in form.edits else " "
            st = form.status.get(s.name)
            sel = items[self.focus] == ("setting", s.name)
            attr = curses.A_REVERSE if sel else 0
            put(scr, y, 1, " " * (width - 2), attr)
            put(scr, y, 1, f"{mark} {label[:35]:<36}", attr | (color(C_WARN) if mark == "*" and not sel else 0))
            put(scr, y, 39, f"{value[:20]:<21}", attr)
            if st:
                ok = st[0] == APPLIED
                put(scr, y, 60, f"{OK_MARK if ok else BAD_MARK} {s.name}", attr | color(C_OK if ok else C_ERR))
            else:
                put(scr, y, 60, f"  {s.name}", attr | (0 if sel else color(C_DIM)))
            item = ("setting", s.name)
            self.zone(y, 1, width - 2, lambda item=item: self.select(item),
                      lambda item=item: self.select(item, ENTER))
            y += 1
        y += 1
        x = 2
        for kind, b in items:
            if kind != "button":
                continue
            label = self.BUTTON_HELP[b][0]
            text = f"[{'x' if self.write_cfg else ' '}] {label}" if b == "cfg" else f"[ {label} ]"
            if b == "save" and form.dirty:
                text = f"[ {label} ({len(form.edits)}) ]"
            sel = items[self.focus] == ("button", b)
            x = self.button(scr, y, x, text,
                            curses.A_REVERSE if sel else (color(C_WARN) if b == "save" and form.dirty else 0),
                            lambda b=b: self.select(("button", b), ENTER))
        y += 2
        kind, name = items[self.focus]
        if kind == "setting":
            help_lines = list(H.SETTINGS_HELP[name])
            title = f"{help_lines[0]}  ({name})"
            body = [part for p in H.paragraphs(help_lines[1:]) for part in wrap(p, width - 6)]
            st = form.status.get(name)
            if st and st[0] == REFUSED:
                body.append(f"Refusé : {st[1]}")
        else:
            title, text = self.BUTTON_HELP[name]
            body = wrap(text, width - 6)
        put(scr, y, 2, title, curses.A_BOLD | color(C_INFO))
        for line in body[:max(0, top + height - y - 2)]:
            y += 1
            put(scr, y, 4, line, color(C_ERR) if line.startswith("Refusé") else 0, width=width - 6)

    def key(self, k):
        items = self.focus_items()
        if k in (curses.KEY_UP, BTAB):
            self.focus = (self.focus - 1) % len(items)
        elif k in (curses.KEY_DOWN, TAB):
            self.focus = (self.focus + 1) % len(items)
        elif k == CTRL_S:
            self.save()
        elif k in (ENTER, " "):
            kind, name = items[self.focus]
            if kind == "button":
                {"save": self.save, "cancel": self.cancel, "reload": self.read_all, "cfg": self.toggle_cfg}[name]()
            elif k == " ":
                self.toggle(BY_NAME[name])
            else:
                self.edit(BY_NAME[name])
        else:
            return False
        return True

    def select(self, item, then=None):
        items = self.focus_items()
        if item in items:
            self.focus = items.index(item)
            if then:
                self.key(then)

    def toggle_cfg(self):
        self.write_cfg = not self.write_cfg

    def cancel(self):
        self.app.form.cancel()
        self.app.msg("Modifications annulées.", C_INFO)

    def toggle(self, s):
        form = self.app.form
        if s.kind == "bool":
            form.set(s.name, "0" if form.value(s.name) == "1" else "1")
        elif s.kind == "choice":
            cur = form.value(s.name)
            i = s.choices.index(cur) if cur in s.choices else -1
            form.set(s.name, s.choices[(i + 1) % len(s.choices)])
        else:
            self.edit(s)

    def edit(self, s):
        app, form = self.app, self.app.form
        help_lines = H.paragraphs(H.SETTINGS_HELP[s.name][1:])
        if s.kind == "bool":
            self.toggle(s)
            return
        if s.kind == "choice":
            descs = {c: line for c in s.choices for line in help_lines if line.startswith(c + " ")}
            app.open(ChoiceDialog(H.SETTINGS_HELP[s.name][0], [(c, c, descs.get(c, "")) for c in s.choices],
                                  lambda v: form.set(s.name, v), current=form.value(s.name)))
            return
        if s.kind == "perks":
            app.open(ChecklistDialog(H.SETTINGS_HELP[s.name][0], H.PERKS, form.value(s.name).split(),
                                     lambda perks: form.set(s.name, " ".join(perks)), intro=help_lines[:2]))
            return
        if s.kind == "mods":
            app.edit_mods(form.value(s.name).split(), lambda mods: form.set(s.name, mods))
            return

        def ok(v):
            try:
                validate(s, v)
            except ValueError as err:
                return str(err)
            form.set(s.name, v)
            return None
        app.open(InputDialog(H.SETTINGS_HELP[s.name][0], s.name, form.value(s.name), "", ok, intro=help_lines))

    def save(self):
        app, form = self.app, self.app.form
        if not app.require_link():
            return
        if self.saving:
            app.msg("Une sauvegarde est déjà en cours.", C_WARN)
            return
        if not form.dirty:
            app.msg("Rien à sauvegarder : aucune ligne marquée *.", C_INFO)
            return
        reload_box = None
        if form.needs_reload() and "fs_mods" not in form.edits:
            reload_box = "Recharger la map ensuite (zm_reset) : la partie recommence au round 1"

        def go(checked):
            cmds = form.commands(reset=checked)
            sets = [c for c in cmds if c.startswith("set ")]
            self.saving = {"edits": dict(form.edits), "reload": [c for c in cmds if not c.startswith("set ")],
                           "write_cfg": self.write_cfg and app.is_local_link(), "output": ""}
            for name in form.edits:
                form.status.pop(name, None)
            if sets:
                app.send(";".join(sets), tag="save-set")
            else:
                self.on_reply("save-set", "", "")

        preview = form.commands(reset=True)
        lines = ["Ces commandes vont être envoyées au serveur, dans cet ordre :", ""] + [f"  {c}" for c in preview]
        if "fs_mods" in form.edits:
            lines += ["", "zm_mods recharge la map tout de suite : la partie en cours est perdue."]
        if self.write_cfg and app.is_local_link():
            lines += ["", "Les valeurs appliquées seront aussi écrites dans server.cfg."]
        app.open(ConfirmDialog("Sauvegarder", lines, go, checkbox=reload_box, checked=bool(reload_box)))


# ---------------------------------------------------------------------------------------------------------- Console
class ConsoleView(View):
    name, title = "console", "Console"
    captures_text = True
    hints = "Entrée envoyer  Haut/Bas historique  PgPréc/PgSuiv, molette : défiler  F9 aide"

    def __init__(self, app):
        super().__init__(app)
        self.lines = collections.deque(maxlen=5000)
        self.edit = LineEdit(history=app.history)
        self.scroll = 0

    def add(self, text, pair=0):
        lines = text.splitlines() or [""]
        for line in lines:
            low = line.lower()
            auto = C_OK if "admin:" in line else C_ERR if ("error" in low or "erreur" in low) else 0
            self.lines.append((line, pair or auto))
        if self.scroll:
            self.scroll += len(lines)

    def draw(self, scr, top, height, width):
        rows = height - 2
        self.scroll = max(0, min(self.scroll, len(self.lines) - rows))
        end = len(self.lines) - self.scroll
        shown = list(self.lines)[max(0, end - rows):end]
        for i, (line, pair) in enumerate(shown):
            put(scr, top + i, 1, line, color(pair) if pair else 0, width=width - 2)
        if self.scroll:
            put(scr, top + rows - 1, width - 28, f" ↑ {self.scroll} lignes plus bas ", color(C_WARN))
        prompt = "rcon> " if self.app.link else "(pas connecté) "
        put(scr, top + height - 1, 1, prompt, curses.A_BOLD)
        self.edit.draw(scr, top + height - 1, 1 + len(prompt), width - len(prompt) - 3)

    def wheel(self, step):
        self.scroll = max(0, self.scroll - 3 * step)

    def key(self, k):
        if k == curses.KEY_PPAGE:
            self.scroll += 10
        elif k == curses.KEY_NPAGE:
            self.scroll = max(0, self.scroll - 10)
        elif k == ENTER:
            command = self.edit.text.strip()
            if not command:
                return True
            if self.app.require_link():
                self.app.send(command, tag="console")
                self.app.remember(command)
                self.edit.set("")
                self.scroll = 0
        else:
            return self.edit.key(k)
        return True


# -------------------------------------------------------------------------------------------------------------- Log
class LogView(View):
    name, title = "log", "Log"
    hints = "/ filtrer  Espace pause  PgPréc/PgSuiv, molette : défiler  Fin dernières lignes  ? aide"

    def __init__(self, app):
        super().__init__(app)
        self.lines = collections.deque(maxlen=20000)
        self.filter = ""
        self.paused = False
        self.scroll = 0

    def add(self, lines):
        self.lines.extend(lines)
        if self.paused or self.scroll:
            self.scroll += len([l for l in lines if self.filter.lower() in l.lower()])

    def draw(self, scr, top, height, width):
        if not self.app.dist:
            for i, line in enumerate(["Pas de log en direct : ZmAdmin n'est pas dans le dossier d'un serveur.",
                                      "Le moteur ne renvoie pas son log en rcon ; à distance, la Console montre la",
                                      "réponse de chaque commande. Posez ZmAdmin dans le dossier BO1Zombies du",
                                      "serveur pour suivre son log ici."]):
                put(scr, top + 1 + i, 3, line, color(C_INFO))
            return
        shown = [l for l in self.lines if self.filter.lower() in l.lower()] if self.filter else list(self.lines)
        rows = height - 1
        self.scroll = max(0, min(self.scroll, len(shown) - rows))
        end = len(shown) - self.scroll
        for i, line in enumerate(shown[max(0, end - rows):end]):
            low = line.lower()
            pair = C_OK if "admin:" in line else C_ERR if "error" in low else 0
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
        put(scr, top + height - 1, 1, " | ".join(state) or self.app.dist.log, color(C_WARN if state else C_DIM),
            width=width - 3 - bw)
        x = width - bw
        for label, k in buttons:
            put(scr, top + height - 1, x, label, curses.A_BOLD)
            self.zone(top + height - 1, x, len(label), lambda k=k: self.key(k))
            x += len(label) + 1

    def wheel(self, step):
        self.scroll = max(0, self.scroll - 3 * step)

    def key(self, k):
        if k == "/":
            def ok(v):
                self.filter = v.strip()
                self.scroll = 0
                return None
            self.app.open(InputDialog("Filtrer le log", "Texte", self.filter,
                                      "seules les lignes qui le contiennent restent (sans tenir compte de la casse)",
                                      ok))
        elif k == " ":
            self.paused = not self.paused
            if not self.paused:
                self.scroll = 0
        elif k == curses.KEY_PPAGE:
            self.scroll += 10
        elif k == curses.KEY_NPAGE:
            self.scroll = max(0, self.scroll - 10)
        elif k == curses.KEY_END:
            self.scroll = 0
            self.paused = False
        else:
            return False
        return True
