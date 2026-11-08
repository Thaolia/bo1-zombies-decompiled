#!/usr/bin/env python3
"""Entry point for `python3 tools/zmclient/main.py` and for the Nuitka build (tools/zmclient/build.sh)."""
import sys

from zmclient.cli import main

if __name__ == "__main__":
    sys.exit(main())
