"""ZmClient: curses launcher for the BO1 zombies client (solo, joining a server, a local server) and its settings.

It uses ZmAdmin's package as a library (widgets, status query, local server). In the source tree that package is
tools/zmadmin/zmadmin; a Nuitka build includes it (tools/zmclient/build.sh).
"""
import importlib.util
import os
import sys

__version__ = "1.0.0"

if importlib.util.find_spec("zmadmin") is None:
    sys.path.append(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                 "zmadmin"))
