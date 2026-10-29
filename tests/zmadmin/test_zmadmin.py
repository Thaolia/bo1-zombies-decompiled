"""ZmAdmin's protocol, parsers, Settings form, log follower and launcher, against a fake server.

    python3 -m unittest discover -s tests/zmadmin -v

The fake server answers as the real one does (formats checked on a win64 server, 2026-10-29): getstatus, rcon with
'"name" is: "value^7" default: ...' lines, several dvars in one rcon ("a;b"), "Invalid password.", one command per
500 ms (a sooner one gets no reply), and replies split over several print packets.
"""
import os
import socket
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "zmadmin"))

from zmadmin import launcher, protocol  # noqa: E402
from zmadmin.link import ServerLink  # noqa: E402
from zmadmin.logtail import LogTail  # noqa: E402
from zmadmin.model import merge_players, parse_dvars, parse_zm_admin  # noqa: E402
from zmadmin.settings import APPLIED, REFUSED, Form, update_server_cfg  # noqa: E402
from zmadmin.widgets import ChecklistDialog, ConfirmDialog, HitMap, mouse_kind  # noqa: E402

OOB = b"\xff\xff\xff\xff"
ZM_ADMIN = ('zm_admin: start points 0, start perks "jugg", join in progress immediate, fs_mods ""\n'
            "   0 Alpha                    playing    health  100 god\n"
            "   1 Bravo the second         down       health   12 notarget frozen\n"
            "   2 Charlie                  loading    health    0\n")


class FakeServer(threading.Thread):
    def __init__(self, password="test", split=False):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.05)
        self.addr = self.sock.getsockname()
        self.password = password
        self.split = split
        self.dvars = {"sv_hostname": "Fake^1Server", "g_password": "", "sv_maxclients": "4", "zm_jip_mode": "immediate"}
        self.commands = []
        self.last_rcon = 0.0
        self.dropped = 0
        self.running = True
        self.start()

    def stop(self):
        self.running = False
        self.join()
        self.sock.close()

    def reply(self, text, to):
        chunks = [text[:20], text[20:]] if self.split and len(text) > 20 else [text]
        for c in chunks:
            self.sock.sendto(OOB + b"\x01print\n" + c.encode("latin-1"), to)

    def run_command(self, cmd):
        out = []
        for part in cmd.split(";"):
            words = part.strip().split(" ", 2)
            if not words[0]:
                continue
            if words[0] == "set" and len(words) == 3:
                self.dvars[words[1]] = words[2].strip('"')
            elif words[0] in self.dvars and len(words) == 1:
                out.append(f'"{words[0]}" is: "{self.dvars[words[0]]}^7" default: "x^7"\n    Domain is any text\n')
            elif words[0] == "zm_admin":
                out.append(ZM_ADMIN)
            else:
                out.append(f'Unknown command "{words[0]}"\n')
        return "".join(out)

    def run(self):
        while self.running:
            try:
                data, src = self.sock.recvfrom(65536)
            except socket.timeout:
                continue
            except OSError:
                return
            if data == OOB + b"getstatus":
                info = "\\sv_hostname\\Fake^1Server\\mapname\\zombie_theater\\sv_maxclients\\4"
                body = f"statusResponse\n{info}\n0 500 31 5 \"Alpha\"\n1 1200 40 5 \"Bravo the second\"\n"
                self.sock.sendto(OOB + body.encode(), src)
            elif data.startswith(OOB + b"rcon "):
                now = time.monotonic()
                if now - self.last_rcon < 0.5:
                    self.dropped += 1
                    continue
                self.last_rcon = now
                _, password, cmd = data[len(OOB):].decode("latin-1").split(" ", 2)
                if password != self.password:
                    self.reply("Invalid password.\n", src)
                    continue
                self.commands.append(cmd)
                self.reply(self.run_command(cmd), src)


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.server = FakeServer(split=True)

    def tearDown(self):
        self.server.stop()

    def test_status(self):
        st = protocol.query_status(self.server.addr)
        self.assertEqual(st.get("sv_hostname"), "FakeServer")
        self.assertEqual(st.max_clients, 4)
        self.assertEqual([(p.slot, p.name, p.score, p.state_name) for p in st.players],
                         [(0, "Alpha", 500, "active"), (1, "Bravo the second", 1200, "active")])

    def test_rcon_spacing_and_split_replies(self):
        client = protocol.RconClient(self.server.addr, "test")
        self.addCleanup(client.close)
        start = time.monotonic()
        for _ in range(3):
            text = client.command("sv_hostname;sv_maxclients")
            self.assertEqual(parse_dvars(text), {"sv_hostname": "FakeServer", "sv_maxclients": "4"})
        self.assertGreaterEqual(time.monotonic() - start, 1.0)
        self.assertEqual(self.server.dropped, 0)

    def test_bad_password(self):
        client = protocol.RconClient(self.server.addr, "nope")
        with self.assertRaises(protocol.RconAuthError):
            client.command("status")
        client.close()

    def test_no_server(self):
        free = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        free.bind(("127.0.0.1", 0))
        addr = free.getsockname()
        free.close()
        with self.assertRaises(protocol.RconTimeout):
            protocol.query_status(addr, timeout=0.3)
        client = protocol.RconClient(addr, "test", timeout=0.3)
        with self.assertRaises(protocol.RconTimeout):
            client.command("status")
        client.close()

    def test_parse_target(self):
        self.assertEqual(protocol.parse_target("10.0.0.5"), ("10.0.0.5", 3074))
        self.assertEqual(protocol.parse_target("10.0.0.5:29070"), ("10.0.0.5", 29070))
        with self.assertRaises(ValueError):
            protocol.parse_target("10.0.0.5:x")


class ParserTest(unittest.TestCase):
    def test_zm_admin(self):
        rep = parse_zm_admin(ZM_ADMIN)
        self.assertEqual(rep.settings["zm_startperks"], "jugg")
        self.assertEqual(rep.players[0].flags, ["god"])
        self.assertEqual((rep.players[1].name, rep.players[1].state, rep.players[1].health), ("Bravo the second", "down", 12))
        self.assertEqual(rep.players[1].flags, ["notarget", "frozen"])
        self.assertEqual(rep.players[2].state, "loading")
        self.assertIsNone(parse_zm_admin('Unknown command "zm_admin"\n'))

    def test_dvars_latched(self):
        text = '"sv_maxclients" is: "4^7" default: "18^7"\nlatched: "8"\n    Domain is any integer from 1 to 32\n'
        self.assertEqual(parse_dvars(text), {"sv_maxclients": "8"})

    def test_merge_drops_left_players(self):
        st = protocol.parse_status(OOB + b'statusResponse\n\\sv_maxclients\\4\n0 500 31 5 "Alpha"\n')
        rows = merge_players(st, parse_zm_admin(ZM_ADMIN))
        self.assertEqual([(r.slot, r.display_state, r.health) for r in rows], [(0, "playing", 100)])


class FormTest(unittest.TestCase):
    def test_pending_commands_and_check(self):
        form = Form()
        form.load({"sv_hostname": "A", "rcon_password": "old", "sv_maxclients": "4", "fs_mods": "",
                   "zm_startperks": "jugg revive"})
        form.set("sv_hostname", "B")
        form.set("zm_startperks", "revive jugg")      # same perks in another order: no change
        form.set("rcon_password", "new")
        form.set("sv_maxclients", "8")
        self.assertEqual(set(form.edits), {"sv_hostname", "rcon_password", "sv_maxclients"})
        self.assertEqual(form.commands(reset=True), ['set sv_hostname "B"', 'set sv_maxclients "8"',
                                                     'set rcon_password "new"', "zm_reset"])
        form.set("fs_mods", "horde")
        self.assertEqual(form.commands(reset=True)[-1], 'zm_mods "horde"')
        form.check({"sv_hostname": "B", "sv_maxclients": "4", "rcon_password": "new"},
                   names=["sv_hostname", "sv_maxclients", "rcon_password"])
        self.assertEqual(form.status["sv_hostname"][0], APPLIED)
        self.assertEqual(form.status["sv_maxclients"][0], REFUSED)
        self.assertEqual(set(form.edits), {"sv_maxclients", "fs_mods"})

    def test_validation(self):
        form = Form()
        for name, value in [("sv_maxclients", "40"), ("zm_startpoints", "abc"), ("rcon_password", ""),
                            ("rcon_password", "a b"), ("sv_hostname", 'a"b'), ("zm_jip_mode", "later")]:
            with self.assertRaises(ValueError, msg=(name, value)):
                form.set(name, value)

    def test_server_cfg(self):
        text = ('// my server\nset sv_hostname "Old"\nset g_password ""\nseta sv_hostname "Older"\n'
                "devmap zombie_theater\n")
        out = update_server_cfg(text, {"sv_hostname": "New", "zm_startpoints": "1500"})
        self.assertEqual(out, '// my server\nset sv_hostname "New"\nset g_password ""\n'
                              'set zm_startpoints "1500"\ndevmap zombie_theater\n')


class LinkTest(unittest.TestCase):
    def test_events(self):
        server = FakeServer()
        link = ServerLink("127.0.0.1", server.addr[1], "test", status_every=0.2, admin_every=0.6)
        link.admin_wanted = True
        link.send("zm_god 0", tag="action")
        seen = {}
        end = time.monotonic() + 5
        while time.monotonic() < end and len(seen) < 3:
            try:
                ev = link.events.get(timeout=0.1)
            except Exception:
                continue
            seen.setdefault(ev[0], ev)
        link.close()
        server.stop()
        self.assertIsNotNone(seen["status"][1])
        self.assertEqual(seen["reply"][1:3], ("action", "zm_god 0"))
        self.assertEqual(seen["admin"][1].players[0].name, "Alpha")
        self.assertIn("zm_admin", server.commands)

    def test_offline_then_back(self):
        server = FakeServer()
        port = server.addr[1]
        server.stop()
        link = ServerLink("127.0.0.1", port, "test", status_every=0.2)
        time.sleep(1.2)
        self.assertFalse(link.online)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("127.0.0.1", port))
        sock.settimeout(2)
        data, src = sock.recvfrom(100)
        self.assertEqual(data, OOB + b"getstatus")
        sock.sendto(OOB + b'statusResponse\n\\sv_hostname\\X\n', src)
        time.sleep(0.3)
        self.assertTrue(link.online)
        sock.close()
        link.close()


class LogTailTest(unittest.TestCase):
    def test_follow_and_truncate(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "console_mp.log")
            with open(path, "w") as f:
                f.write("x" * 100 + "\nold line\n")
            tail = LogTail(path, history=12)
            self.assertEqual(tail.poll(), ["old line"])   # the cut line before it is dropped
            with open(path, "a") as f:
                f.write("new 1\nnew")
            self.assertEqual(tail.poll(), ["new 1"])
            with open(path, "a") as f:
                f.write(" 2\n")
            self.assertEqual(tail.poll(), ["new 2"])
            with open(path, "w") as f:
                f.write("restart\n")
            self.assertEqual(tail.poll(), ["restart"])
            tail.close()


class LauncherTest(unittest.TestCase):
    def test_command_matches_server_script(self):
        with tempfile.TemporaryDirectory() as d:
            for name in (launcher.EXE_NAME, launcher.SCRIPT_NAME):
                open(os.path.join(d, name), "w").close()
            os.mkdir(os.path.join(d, "zone"))
            dist = launcher.find_dist(d)
            self.assertIsNotNone(dist)
            cmd = launcher.server_command(dist, "zombie_moon", 3080, "pw")
        root = os.path.abspath(d)
        # as server.sh / Server.bat (tools/make_dist.sh), plus the rcon password and no curses console
        self.assertEqual(cmd, [os.path.join(root, launcher.EXE_NAME), "+set", "fs_b", root,
                               "+set", "fs_h", os.path.join(root, "server"), "+set", "bo1_server", "1",
                               "+set", "net_ip", "0.0.0.0", "+set", "net_port", "3080", "+set", "bo1_tui", "0",
                               "+set", "rcon_password", "pw", "+rcon_password", "pw", "+devmap", "zombie_moon"])

    def test_not_a_dist(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(launcher.find_dist(d))


class MouseTest(unittest.TestCase):
    def test_kinds(self):
        import curses
        self.assertEqual(mouse_kind(curses.BUTTON1_CLICKED), "click")
        self.assertEqual(mouse_kind(curses.BUTTON1_RELEASED), "click")
        self.assertEqual(mouse_kind(curses.BUTTON1_DOUBLE_CLICKED), "double")
        # a press alone does nothing: its release (or the merged click) acts, so a click never acts twice
        self.assertIsNone(mouse_kind(curses.BUTTON1_PRESSED))
        if getattr(curses, "BUTTON4_PRESSED", 0):
            self.assertEqual(mouse_kind(curses.BUTTON4_PRESSED), "up")

    def test_hitmap(self):
        hits, calls = HitMap(), []
        hits.add(3, 10, 5, lambda: calls.append("a"), lambda: calls.append("A"))
        hits.add(3, 12, 2, lambda: calls.append("b"))
        hits.add(4, 0, 0, lambda: calls.append("empty"))
        self.assertIsNone(hits.dispatch(3, 9, "click"))
        self.assertIsNone(hits.dispatch(3, 15, "click"))
        hits.dispatch(3, 10, "click")
        hits.dispatch(3, 11, "double")
        hits.dispatch(3, 12, "click")      # the zone added last wins
        hits.dispatch(3, 13, "double")     # no double action: its click runs
        hits.dispatch(4, 0, "click")
        self.assertEqual(calls, ["a", "A", "b", "b"])
        hits.clear()
        self.assertIsNone(hits.find(3, 10))

    def test_dialog_zones(self):
        said = []
        d = ConfirmDialog("t", ["?"], said.append, checkbox="box")
        d.origin = (5, 7)
        d.zone(2, 2, 7, d.yes)
        d.zone(1, 2, 7, d.toggle)
        self.assertFalse(d.mouse(6, 9, "click"))   # the checkbox: ticked, dialog stays
        self.assertFalse(d.mouse(0, 0, "click"))   # outside every zone
        self.assertTrue(d.mouse(7, 12, "click"))   # Oui
        self.assertEqual(said, [True])
        c = ChecklistDialog("t", [("x", "X"), ("y", "Y")], [], said.append)
        c.tick(1)
        c.tick(0)
        c.tick(1)
        self.assertTrue(c.submit())
        self.assertEqual(said[-1], ["x"])


if __name__ == "__main__":
    unittest.main()
