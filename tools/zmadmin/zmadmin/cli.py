"""Command line: the curses UI by default, --status / --rcon for scripts and smoke tests of the built binary."""
import argparse
import curses
import locale
import os
import sys

from . import __version__
from . import help as H
from .config import Config
from .launcher import find_dist
from .protocol import DEFAULT_PORT, ProtocolError, RconClient, parse_target, query_status, strip_colors

DESCRIPTION = """ZmAdmin: administration console for the BO1 zombies dedicated server (docs/SERVER.md).

  zmadmin                              the interface; the Servers view lists the saved servers
  zmadmin 192.168.1.20 -p secret       connect at once (port 3074 unless given as host:port)
  zmadmin --status 192.168.1.20        print the server's status and exit (no rcon needed)
  zmadmin --rcon "zm_admin" HOST -p PW print one rcon command's output and exit

Next to BO1Zombies and server.sh / Server.bat (a make_dist.sh folder) it can also start that folder's server."""


def print_status(target, timeout):
    host, port = parse_target(target, DEFAULT_PORT)
    try:
        st = query_status((host, port), timeout=timeout)
    except (ProtocolError, OSError) as e:
        print(f"no answer: {e}")
        return 1
    print(f"server: {st.get('sv_hostname')}")
    print(f"map: {H.map_label(st.get('mapname'))}")
    print(f"players: {len(st.players)}/{st.max_clients}")
    for p in st.players:
        print(f"  #{p.slot} {p.name}: {p.score} points, ping {p.ping}, {p.state_name}")
    return 0


def run_rcon(target, password, command, timeout):
    host, port = parse_target(target, DEFAULT_PORT)
    client = RconClient((host, port), password, timeout=timeout)
    try:
        sys.stdout.write(strip_colors(client.command(command)))
    except (ProtocolError, OSError) as e:
        print(f"rcon: {e}", file=sys.stderr)
        return 1
    finally:
        client.close()
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="zmadmin", description=DESCRIPTION,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", nargs="?", help="server address, host[:port]")
    parser.add_argument("-p", "--password", default="", help="the server's rcon_password")
    parser.add_argument("--status", action="store_true", help="print the status of TARGET and exit")
    parser.add_argument("--rcon", metavar="COMMAND", help="send COMMAND to TARGET over rcon, print the reply, exit")
    parser.add_argument("--timeout", type=float, default=2.0, help="seconds to wait for --status / --rcon")
    parser.add_argument("--dist", help="the BO1Zombies folder whose server ZmAdmin may start (default: its own "
                                       "folder, or the current one)")
    parser.add_argument("--admin-poll", type=float, default=5.0, metavar="SECONDS",
                        help="how often the Players and Game views read zm_admin (0: never; each read is one rcon "
                             "command, logged by the server)")
    parser.add_argument("--no-mouse", action="store_true",
                        help="keyboard only: leave the mouse to the terminal (text selection without Shift)")
    parser.add_argument("--config", help="ZmAdmin's settings folder (servers.json, history)")
    parser.add_argument("--version", action="version", version=f"zmadmin {__version__}")
    args = parser.parse_args(argv)

    if args.status or args.rcon:
        if not args.target:
            parser.error("--status and --rcon need a TARGET")
        if args.rcon:
            return run_rcon(args.target, args.password, args.rcon, args.timeout)
        return print_status(args.target, args.timeout)

    initial = None
    if args.target:
        try:
            host, port = parse_target(args.target, DEFAULT_PORT)
        except ValueError as e:
            parser.error(str(e))
        initial = (host, port, args.password, ("cli", {"host": host, "port": port}))
    dist = find_dist(args.dist)
    if args.dist and not dist:
        parser.error(f"{args.dist} is not a BO1Zombies folder (BO1Zombies, server script and zone/ needed)")
    config = Config(args.config)

    locale.setlocale(locale.LC_ALL, "")
    os.environ.setdefault("ESCDELAY", "25")  # Esc closes a dialog at once instead of after 1 s
    from .app import App
    return curses.wrapper(lambda scr: App(scr, config, dist, initial, args.admin_poll, not args.no_mouse).run()) or 0
