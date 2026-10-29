# Dedicated server / multiplayer tests — stop your processes before every rebuild

Applies to the dedicated-server work (worktree `../BO-Zombies_switch_server`, branch `server`) and any test that runs
a headless server and headless clients (`tools/headless.ps1`, run folders under `build\run\`).

## Rule
- **Before every rebuild** (win64-msvc, win32-msvc, linux64-clang, switch), stop every headless server and client
  this session started, then check none is left. Asked by the user on 2026-10-26.
- Stop both layers:
  1. the background wrapper tasks (`TaskStop` on their task id);
  2. the game processes themselves: stopping a `headless.ps1` wrapper does **not** stop the `BO1Zombies.exe` it
     launched. It keeps running until its `bo1_autoquit` and keeps the run folder's mutex, so the next launch from
     that folder is `REFUSED` (seen three times on 2026-10-26).
- Select game processes **by their run-folder path**, never by name alone (another worktree or the user's own game
  may be running):
  ```bash
  powershell.exe -NoProfile -Command "Get-Process BO1Zombies -ErrorAction SilentlyContinue | Where-Object { \$_.Path -like '*\BO-Zombies_switch_server\build\run\*' } | ForEach-Object { 'stop ' + \$_.Id; Stop-Process -Id \$_.Id -Force }; 'remaining:'; Get-Process BO1Zombies -ErrorAction SilentlyContinue | Select -Expand Path"
  ```
  `remaining:` must list none of this session's run folders before the build starts.
- Linux servers (ext4 mirror, e.g. `/home/james/build/bo1-server/run`) count too: select them by their executable,
  not by the PID `$!` of a `setsid nohup` launch (that PID was not the server's three times on 2026-10-26, and three
  stale servers kept port 29070, so a client joined an old binary):
  ```bash
  for p in $(pgrep -f '[.]/BO1Zombies'); do [ "$(readlink /proc/$p/exe)" = "/home/james/build/bo1-server/run/BO1Zombies" ] && kill $p; done
  ```
  Check the port is free before the next launch (`ss -lun | grep 29070`): a second server silently takes the next
  port (`net_port` + 1..9) and the client still reaches the old one.

## Waiting on test processes
- Never wait on a `powershell.exe` whose command line contains a pattern that the waiting command also contains
  (the query matches itself and the loop never ends; `~/.claude/rules/process-matching.md`).
- A wait loop for a server must also give up on `REFUSED` in the wrapper's output and after a timeout, instead of
  waiting for a log line that will never come.
