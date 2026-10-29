#!/usr/bin/env python3
"""Entry point for `python3 tools/zmadmin/main.py` and for the Nuitka build (tools/zmadmin/build.sh)."""
import sys

from zmadmin.cli import main

if __name__ == "__main__":
    sys.exit(main())
