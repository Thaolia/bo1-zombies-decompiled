"""The curses application: the tab bar, the status lines, the event loop, connections and the local server."""
import curses
import os
import queue
import socket
import time

from . import help as H
from .launcher import LocalServer
from .link import ServerLink
from .logtail import LogTail
from .model import merge_players
from .protocol import strip_colors
from .settings import Form, update_server_cfg
from .views import ConsoleView, GameView, LogView, PlayersView, ServersView, SettingsView
from .widgets import (BTAB, CTRL_C, C_ERR, C_INFO, C_OK, C_TITLE, C_WARN, ESC, TAB, ChecklistDialog, ConfirmDialog,
                      InputDialog, MessageDialog, color, cursor, fill, init_colors, normalize_key, put)

STOP_TIMEOUT = 15.0
MIN_H, MIN_W = 24, 80


class App:
    def __init__(self, scr, config, dist, initial=None, admin_every=5.0):
        self.scr = scr
        self.config = config
        self.dist = dist
        self.local = LocalServer(dist) if dist else None
        self.admin_every = admin_every
        self.history = config.load_history()
        self.link = None
        self.connected_to = None    # ("local", None) or ("saved", entry) or ("cli", entry)
        self.status = None
        self.report = None
        self.rows = []
        self.form = Form()
        self.modals = []
        self.message = ("", 0, 0.0)
        self.running = True
        self.stopping = None        # deadline of a local server stop
        self.quit_after_stop = False
        self.logtail = LogTail(dist.log) if dist else None
        self.next_log_poll = 0.0
        self.views = [ServersView(self), PlayersView(self), GameView(self), SettingsView(self), ConsoleView(self),
                      LogView(self)]
        self.current = 0
        if initial:
            self.connect(*initial)
            self.show(1)
        elif self.local and self.local.running():
            self.connect_local()
            self.show(1)

    # ----------------------------------------------------------------------------------------------- helpers
    @property
    def view(self):
        return self.views[self.current]

    @property
    def console(self):
        return self.views[4]

    @property
    def settings_view(self):
        return self.views[3]

    def msg(self, text, pair=C_INFO):
        self.message = (text, pair, time.monotonic())

    def open(self, dialog):
        self.modals.append(dialog)

    def show(self, index):
        self.current = index % len(self.views)
        if self.link:
            self.link.admin_wanted = self.view.uses_admin
            if self.view.uses_admin:
                self.link.refresh_admin()
        self.view.on_show()

    def require_link(self):
        if not self.link:
            self.msg("Pas connecté : choisir un serveur dans la vue Serveurs (F1).", C_WARN)
            return False
        return True

    def send(self, command, tag="console"):
        self.link.send(command, tag)
        if tag in ("console", "action"):
            self.console.add(f"> {command}", C_INFO)

    def remember(self, command):
        if not self.history or self.history[-1] != command:
            self.history.append(command)
            del self.history[:-100]
        try:
            self.config.save_history(self.history)
        except OSError:
            pass

    def is_local_link(self):
        return bool(self.link and self.connected_to and self.connected_to[0] == "local")

    # ------------------------------------------------------------------------------------------- connections
    def connect(self, host, port, password, target):
        self.disconnect()
        try:
            self.link = ServerLink(host, port, password, admin_every=self.admin_every)
        except (OSError, socket.gaierror) as e:
            self.msg(f"Adresse {host} introuvable : {e}", C_ERR)
            return
        self.connected_to = target
        self.link.admin_wanted = self.view.uses_admin
        self.form = Form()
        self.settings_view.loaded = False
        self.settings_view.saving = None
        self.console.add(f"--- connexion à {host}:{port} ---", C_INFO)
        self.msg(f"Connexion à {host}:{port}…", C_INFO)
        if self.view is self.settings_view:
            self.settings_view.on_show()

    def disconnect(self):
        if self.link:
            self.link.close()
        self.link = None
        self.connected_to = None
        self.status = self.report = None
        self.rows = []

    def connect_saved(self, entry):
        if not entry.get("password"):
            self.msg("Ce serveur n'a pas de mot de passe rcon : « e » pour l'ajouter. Statut seulement.", C_WARN)
        self.connect(entry["host"], int(entry.get("port", 3074)), entry.get("password", ""), ("saved", entry))
        self.show(1)

    def connect_local(self):
        local = self.config.local_settings(self.dist.root)
        port = self.local.port or local["port"]
        self.connect("127.0.0.1", int(port), local["password"], ("local", None))
        self.show(1)

    def password_changed(self, password):
        """rcon_password changed by Save: use it from now on, and remember it where it came from."""
        self.link.set_password(password)
        kind, entry = self.connected_to
        if kind == "local":
            self.config.local_settings(self.dist.root)["password"] = password
        elif kind == "saved":
            entry["password"] = password
        try:
            self.config.save()
        except OSError as e:
            self.msg(f"Mot de passe changé, mais servers.json n'a pas pu être écrit : {e}", C_ERR)

    # ------------------------------------------------------------------------------------------- local server
    def launch_local(self, map_name, port, password):
        try:
            self.local.start(map_name, port, password)
        except (OSError, RuntimeError) as e:
            return f"lancement impossible : {e}"
        self.console.add(f"--- serveur local lancé : {H.map_label(map_name)}, port {port}, PID {self.local.pid} ---",
                         C_OK)
        self.msg(f"Serveur lancé (PID {self.local.pid}) : chargement de {H.map_label(map_name)}…", C_OK)
        self.connect_local()
        return None

    def stop_local(self):
        if not self.local.running():
            self.msg("Le serveur local ne tourne pas.", C_INFO)
            return

        def go(_):
            if self.is_local_link() and self.link.online:
                self.send("quit", tag="quit")
            else:
                self.local.kill()
            self.stopping = time.monotonic() + STOP_TIMEOUT
            self.msg("Arrêt du serveur local…", C_WARN)
        self.open(ConfirmDialog("Arrêter le serveur local", ["Arrêter le serveur local ?",
                                                             "Les joueurs sont déconnectés et la partie est perdue."],
                                go))

    def check_local(self, now):
        if self.stopping is None:
            if self.is_local_link() and not self.local.running() and self.local.pid:
                tail = self.local.exit_text()
                self.console.add("--- le serveur local s'est arrêté ---", C_ERR)
                for line in tail:
                    self.console.add(line, C_ERR)
                self.msg("Le serveur local s'est arrêté (dernières lignes dans la Console).", C_ERR)
                self.local.pid = None
                self.disconnect()
            return
        if not self.local.running():
            self.stopping = None
            self.local.pid = None
            if self.is_local_link():
                self.disconnect()
            self.msg("Serveur local arrêté.", C_OK)
            if self.quit_after_stop:
                self.running = False
        elif now > self.stopping:
            self.local.kill()
            self.msg("Le serveur ne s'arrêtait pas : arrêt forcé.", C_WARN)
            self.stopping = now + 5

    def write_server_cfg(self, values):
        path = self.dist.server_cfg
        try:
            with open(path, encoding="latin-1") as f:
                text = f.read()
        except FileNotFoundError:
            text = "// written by ZmAdmin; see docs/SERVER.md, \"Settings and their order\"\n"
        except OSError as e:
            self.msg(f"server.cfg illisible : {e}", C_ERR)
            return
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="latin-1", newline="\n") as f:
                f.write(update_server_cfg(text, values))
        except OSError as e:
            self.msg(f"server.cfg non écrit : {e}", C_ERR)
            return
        self.console.add(f"--- server.cfg : {', '.join(values)} écrits dans {path} ---", C_OK)

    def edit_mods(self, current, on_done):
        mods = self.dist.mods() if self.dist and self.is_local_link() else []
        intro = ["mods/admin est toujours chargé par le serveur."]
        if mods:
            self.open(ChecklistDialog("Mods chargés", [(m, m) for m in mods], current,
                                      lambda ticked: on_done(" ".join(ticked)), intro=intro))
        else:
            def ok(v):
                if '"' in v or ";" in v:
                    return "pas de guillemet ni de point-virgule"
                on_done(" ".join(v.split()))
                return None
            self.open(InputDialog("Mods chargés", "fs_mods", " ".join(current),
                                  "noms des dossiers de mods/ du serveur, séparés par des espaces", ok,
                                  intro=intro + ["Serveur distant : ZmAdmin ne voit pas ses dossiers, tapez les noms."]))

    # ------------------------------------------------------------------------------------------------- events
    def pump(self):
        if not self.link:
            return
        while True:
            try:
                event = self.link.events.get_nowait()
            except queue.Empty:
                break
            kind = event[0]
            if kind == "status":
                _, status, error = event
                was = self.status is not None
                self.status = status
                if status is None and was:
                    self.msg(f"Le serveur ne répond plus ({error}) : nouvel essai chaque seconde.", C_ERR)
                elif status is not None and not was:
                    self.msg(f"Connecté : {status.get('sv_hostname')} sur {H.map_label(status.get('mapname'))}.",
                             C_OK)
                    if self.view is self.settings_view:
                        self.settings_view.on_show()
            elif kind == "admin":
                self.report = event[1]
            elif kind == "auth":
                self.msg(f"rcon : {event[1]}. Vue Serveurs (F1), « e » pour corriger le mot de passe.", C_ERR)
            elif kind == "reply":
                _, tag, command, text, error = event
                if tag in ("console", "action"):
                    if text is None:
                        self.console.add(f"! {error}", C_ERR)
                    else:
                        self.console.add(strip_colors(text).rstrip("\n") or "(pas de sortie)")
                if tag == "action":
                    first = strip_colors(text or "").strip().splitlines()
                    self.msg(error if text is None else (first[0] if first else f"{command} : envoyé"),
                             C_ERR if text is None else C_OK)
                elif tag.startswith(("settings-", "save-")):
                    self.settings_view.on_reply(tag, text, error)
            self.rows = merge_players(self.status, self.report)

    def poll_log(self, now):
        if not self.logtail or now < self.next_log_poll:
            return
        self.next_log_poll = now + 0.25
        lines = self.logtail.poll()
        if not lines:
            return
        self.views[5].add(lines)
        if self.is_local_link():
            for line in lines:
                # the server also logs every rcon command it gets: ZmAdmin's own polling would flood the Console
                if line.startswith(("Rcon from", "zm_admin", "  ")) or not line.strip():
                    continue
                if "admin:" in line:
                    self.console.add(line)

    # --------------------------------------------------------------------------------------------------- draw
    def draw(self):
        scr = self.scr
        scr.erase()
        h, w = scr.getmaxyx()
        if h < MIN_H or w < MIN_W:
            put(scr, 0, 0, f"Fenêtre trop petite ({w}x{h}) : il faut au moins {MIN_W}x{MIN_H}.")
            scr.refresh()
            return
        fill(scr, 0, color(C_TITLE))
        x = 1
        for i, v in enumerate(self.views):
            label = f" F{i + 1} {v.title} "
            put(scr, 0, x, label, (curses.A_REVERSE | curses.A_BOLD) if i == self.current else color(C_TITLE))
            x += len(label) + 1
        put(scr, 0, x + 1, "ZmAdmin", color(C_TITLE) | curses.A_BOLD)
        self.draw_status_line(scr, w)
        text, pair, at = self.message
        if text and time.monotonic() - at < 12:
            put(scr, h - 2, 1, text, color(pair), width=w - 2)
        fill(scr, h - 1, color(C_TITLE))
        put(scr, h - 1, 1, self.view.hints, color(C_TITLE), width=w - 2)
        cursor(False)
        # the view draws last: a view with a command line leaves the cursor on it
        try:
            self.view.draw(scr, 2, h - 4, w)
        except curses.error:
            pass
        if self.view.captures_text and not self.modals:
            cursor(True)
        for d in self.modals[-1:]:
            d.draw(scr)
            if d.cursor:
                cursor(True)
        scr.refresh()

    def draw_status_line(self, scr, w):
        if not self.link:
            put(scr, 1, 1, "Pas connecté. F1 : choisir ou lancer un serveur.", color(C_WARN))
            return
        parts = []
        if self.link.online is None:
            parts.append(("● connexion…", C_WARN))
        elif not self.link.online:
            starting = self.is_local_link() and self.local.running()
            parts.append(("● démarrage du serveur…" if starting else "● hors ligne", C_WARN if starting else C_ERR))
        else:
            parts.append(("● en ligne", C_OK))
        where = "local " if self.is_local_link() else ""
        parts.append((f"{where}{self.link.host}:{self.link.port}", 0))
        if self.status:
            parts.append((self.status.get("sv_hostname"), 0))
            parts.append((H.MAP_NAMES.get(self.status.get("mapname"), self.status.get("mapname")), 0))
            parts.append((f"{len(self.status.players)}/{self.status.max_clients} joueurs", 0))
        if self.link.auth_ok is False:
            parts.append(("rcon refusé", C_ERR))
        if self.link.queued:
            parts.append((f"{self.link.queued} commande(s) en file", C_WARN))
        x = 1
        for i, (text, pair) in enumerate(parts):
            if i:
                put(scr, 1, x, " | ", color(C_INFO))
                x += 3
            put(scr, 1, x, text, color(pair) if pair else 0)
            x += len(text)

    # ---------------------------------------------------------------------------------------------------- keys
    def handle(self, k):
        if k == curses.KEY_RESIZE:
            try:
                curses.resize_term(0, 0)
            except curses.error:
                pass
            return
        if self.modals:
            dialog = self.modals[-1]
            # a dialog may open the next one before it closes: remove this one, not the top
            if dialog.key(k):
                self.modals.remove(dialog)
            return
        for i in range(6):
            if k == curses.KEY_F1 + i:
                self.show(i)
                return
        if k == TAB:
            self.show(self.current + 1)
            return
        if k == BTAB:
            self.show(self.current - 1)
            return
        if k == curses.KEY_F9 or (k == "?" and not self.view.captures_text):
            self.open(MessageDialog(f"Aide : {self.view.title}", H.VIEW_HELP[self.view.name] + [""] + H.GLOBAL_HELP))
            return
        if k == curses.KEY_F10 or (k == "q" and not self.view.captures_text):
            self.ask_quit()
            return
        if self.view.key(k):
            return
        if k == ESC and self.view.captures_text:
            self.console.edit.set("")

    def ask_quit(self):
        if self.local and self.local.running():
            def go(stop):
                if stop:
                    self.quit_after_stop = True
                    if self.is_local_link() and self.link.online:
                        self.send("quit", tag="quit")
                    else:
                        self.local.kill()
                    self.stopping = time.monotonic() + STOP_TIMEOUT
                    self.msg("Arrêt du serveur local avant de quitter…", C_WARN)
                else:
                    self.running = False
            self.open(ConfirmDialog("Quitter ZmAdmin", ["Quitter ZmAdmin ?",
                                                        "Le serveur local continue de tourner, sauf si la case",
                                                        "ci-dessous est cochée. Un prochain ZmAdmin le retrouve."],
                                    go, checkbox="Arrêter aussi le serveur local"))
        else:
            self.running = False

    # ---------------------------------------------------------------------------------------------------- loop
    def run(self):
        init_colors()
        # raw: Ctrl+S (Save) would be the terminal's XOFF in cbreak mode; Ctrl+C then comes as a key
        curses.raw()
        self.scr.timeout(100)
        self.scr.keypad(True)
        cursor(False)
        while self.running:
            now = time.monotonic()
            self.pump()
            self.poll_log(now)
            if self.local:
                self.check_local(now)
            self.draw()
            try:
                k = self.scr.get_wch()
            except curses.error:
                continue
            except KeyboardInterrupt:
                self.ask_quit()
                continue
            k = normalize_key(k)
            if k == CTRL_C and self.modals:
                self.modals.pop()
            elif k == CTRL_C:
                self.ask_quit()
            elif k is not None:
                self.handle(k)
        self.disconnect()
        if self.logtail:
            self.logtail.close()
