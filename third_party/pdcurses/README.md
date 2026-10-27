# PDCurses 3.9 (vendored)

Curses for the Windows console, used by the dedicated server's console (`src/server_tui`) on Windows builds; posix
builds use the system ncurses. Public domain (see `README.upstream.md`, `pdcurses/README.md`, `wincon/README.md`).

- Source: https://github.com/wmcbrine/PDCurses/archive/refs/tags/3.9.tar.gz
  (sha256 `590dbe0f5835f66992df096d3602d0271103f90cf8557a5d124f693c2b40d7ec`)
- Kept: `curses.h`, `curspriv.h`, `panel.h`, `pdcurses/` (the portable core), `wincon/` (the Windows console port) and
  `common/acs437.h` (its line-drawing table), unmodified.
  Left out: the other ports (dos, os2, sdl1, sdl2, x11), demos and docs.
