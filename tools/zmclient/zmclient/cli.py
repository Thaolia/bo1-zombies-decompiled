"""Command line: the curses UI by default; --print-cmd, --play and --setup for scripts and smoke tests."""
import argparse
import curses
import locale
import os
import sys
import time

from zmadmin.launcher import EXE_NAME, IS_WINDOWS, Dist, program_dir

from . import __version__, gamesetup
from .client import Client
from .profile import Profile, build_command, fs_game, quote_command

DESCRIPTION = """ZmClient: launcher for the BO1 zombies client, next to BO1Zombies (a make_dist.sh folder).

  zmclient                     the interface: play solo, join a server, or start a local server and join it;
                               the solo mods, config.cfg, free dvars, and the game files' installation
  zmclient --print-cmd         print the command line the saved profile gives, and exit
  zmclient --play              start the game with the saved profile, and exit
  zmclient --setup [DIR]       copy the game files from the Black Ops install DIR (found in Steam when left out)
                               and convert the zones, in text mode"""

PLAY_SCRIPT = "Play.bat" if IS_WINDOWS else "play.sh"


def config_dir():
    if os.name == "nt":
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(base, "zmclient")


def is_client_dist(path):
    """A package folder: the program and its play script (zone/ may be missing: the Installation view adds it)."""
    return os.path.isfile(os.path.join(path, EXE_NAME)) and os.path.isfile(os.path.join(path, PLAY_SCRIPT))


def find_dist(explicit=None):
    for c in [explicit] if explicit else [program_dir(), os.getcwd()]:
        if c and is_client_dist(c):
            return Dist(c)
    return None


def run_setup(dist, profile, install):
    install = install or profile["steam_install"] or next(iter(gamesetup.find_installs()), "")
    if not install:
        print("no Black Ops install found in Steam's libraries: pass its folder (--setup DIR)", file=sys.stderr)
        return 1
    job = gamesetup.Setup(dist.root, install, video=profile["setup_video"], convert=profile["setup_convert"])
    job.start()
    shown = 0

    def print_new():
        lines, total = job.since(shown)
        for line in lines:
            print(line, flush=True)
        return total
    while not job.finished:
        time.sleep(0.2)
        shown = print_new()
    print_new()
    if job.error:
        print(f"setup failed: {job.error}", file=sys.stderr)
        return 1
    profile["steam_install"] = job.install
    profile.save()
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="zmclient", description=DESCRIPTION,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dist", help="the BO1Zombies folder (default: ZmClient's own folder, or the current one)")
    parser.add_argument("--config", help="ZmClient's settings folder (profile.json)")
    parser.add_argument("--admin-config", help="ZmAdmin's settings folder, where the local server's rcon password is "
                                               "kept (default: ZmAdmin's own)")
    parser.add_argument("--no-mouse", action="store_true",
                        help="keyboard only: leave the mouse to the terminal (text selection without Shift)")
    parser.add_argument("--print-cmd", action="store_true", help="print the game's command line and exit")
    parser.add_argument("--play", action="store_true", help="start the game with the saved profile and exit")
    parser.add_argument("--setup", nargs="?", const="", metavar="DIR",
                        help="install the game files from DIR (default: the saved or the detected install)")
    parser.add_argument("--version", action="version", version=f"zmclient {__version__}")
    args = parser.parse_args(argv)

    dist = find_dist(args.dist)
    if args.dist and not dist:
        parser.error(f"{args.dist} is not a BO1Zombies folder ({EXE_NAME} and {PLAY_SCRIPT} needed)")
    profile = Profile(os.path.join(args.config or config_dir(), "profile.json"))

    if args.setup is not None:
        if not dist:
            parser.error("--setup needs a BO1Zombies folder (--dist)")
        return run_setup(dist, profile, args.setup)
    if args.print_cmd or args.play:
        if not dist:
            parser.error("no BO1Zombies folder here (--dist)")
        if args.play and profile["mode"] == "host":
            parser.error("--play does not start the local server: use the interface, or the solo or join mode")
        try:
            with open(os.path.join(dist.root, "launch.cfg"), encoding="latin-1") as f:
                launch_cfg = f.read()
        except OSError:
            launch_cfg = ""
        try:
            command = build_command(profile, dist.exe, dist.root, launch_cfg)
        except ValueError as e:
            print(f"zmclient: {e}", file=sys.stderr)
            return 1
        if args.print_cmd:
            print(quote_command(command))
            return 0
        missing = gamesetup.ready(dist.root)
        if missing:
            print(f"zmclient: {missing} (zmclient --setup)", file=sys.stderr)
            return 1
        client = Client(dist)
        try:
            client.start(command, fs_game(profile, dist.root))
        except (OSError, RuntimeError) as e:
            print(f"zmclient: {e}", file=sys.stderr)
            return 1
        print(f"started, PID {client.pid}; log: {client.log_path()}")
        return 0

    locale.setlocale(locale.LC_ALL, "")
    os.environ.setdefault("ESCDELAY", "25")  # Esc closes a dialog at once instead of after 1 s
    from .app import App
    return curses.wrapper(lambda scr: App(scr, profile, dist, not args.no_mouse, args.admin_config).run()) or 0
