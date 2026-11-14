#!/bin/bash
# setup_switch.sh - completes the Switch package (BO1Zombies_switch_nodata) with the player's own Call of Duty:
# Black Ops: localization.txt, main/*.iwd, the zones converted by ffconv, shaders.pack and shaders.dksh (uam).
#
#   ./setup_switch.sh "/path/to/Call of Duty Black Ops"
#
# ffconv is a Windows program: it runs through WSL's interop on WSL, else under wine (wine and winepath needed), as
# ZmClient does. shaders.dksh needs python3. The install is only read. The result is switch/bo1zm/ in this folder,
# ready to copy to the SD card's root. The new zones replace zone/ only once ffconv succeeded.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SD="$ROOT/switch/bo1zm"
TOOLS="$ROOT/tools"
LOG="$ROOT/setup_switch.log"

step() { printf '\n== %s\n' "$1"; }
fail() { printf '\nERREUR : %s\n' "$1" >&2; exit 1; }

GAME="${1:-}"
[ -n "$GAME" ] || fail "usage : ./setup_switch.sh \"/chemin/vers/Call of Duty Black Ops\" (le dossier de BlackOps.exe)"
GAME="$(cd "$GAME" 2> /dev/null && pwd)" || fail "dossier introuvable : $1"
[ -f "$SD/BO1Zombies.nro" ] || fail "pas de switch/bo1zm/BO1Zombies.nro à côté de ce script"
for t in windows/ffconv.exe linux/uam linux/make_dksh_pack.py; do
    [ -f "$TOOLS/$t" ] || fail "outil manquant : tools/$t"
done
command -v python3 > /dev/null || fail "python3 introuvable (il compile shaders.dksh)"
if [ ! -d "$GAME/zone" ] || [ ! -f "$GAME/localization.txt" ] || ! ls "$GAME"/main/*.iwd > /dev/null 2>&1; then
    fail "pas d'installation de Black Ops dans '$GAME' (zone/, main/*.iwd, localization.txt)"
fi
# ffconv.exe: through WSL's interop, else wine; it takes Windows paths
if [ -e /proc/sys/fs/binfmt_misc/WSLInterop ] && command -v wslpath > /dev/null; then
    RUN=() WINPATH=(wslpath -w)
elif command -v wine > /dev/null && command -v winepath > /dev/null; then
    RUN=(wine) WINPATH=(winepath -w)
else
    fail "ffconv est un programme Windows : installez wine (avec winepath), ou lancez Setup-Switch.bat sous Windows"
fi
chmod +x "$TOOLS/linux/uam"
echo "setup_switch $(date -Iseconds) from $GAME" > "$LOG"

step "1/3 copie des fichiers du jeu (localization.txt, main/*.iwd)"
mkdir -p "$SD/main"
cp -f "$GAME/localization.txt" "$SD/"
cp -f "$GAME"/main/*.iwd "$SD/main/"

step "2/3 conversion des zones (ffconv, environ 6 minutes)"
new="$SD/zone.setup-new"
rm -rf "$new"
mkdir -p "$new"
# < /dev/null: a Windows program run from WSL otherwise reads the caller's stdin
if ! "${RUN[@]}" "$TOOLS/windows/ffconv.exe" --verify --game "$("${WINPATH[@]}" "$GAME")" \
        --out "$("${WINPATH[@]}" "$new")" < /dev/null 2>&1 | tee -a "$LOG"; then
    fail "ffconv a échoué : les zones précédentes sont gardées ; détails dans setup_switch.log"
fi
count="$(find "$new/zone" -name '*.ff' 2> /dev/null | wc -l)"
[ "$count" -gt 0 ] && [ -f "$new/shaders.pack" ] || fail "ffconv n'a produit ni zones ni shaders.pack ; détails dans setup_switch.log"
rm -rf "$SD/zone"
mv "$new/zone" "$SD/zone"
mv -f "$new/shaders.pack" "$SD/shaders.pack"
rm -rf "$new"
echo "$count zones converties"

step "3/3 compilation de shaders.dksh (uam, quelques minutes)"
# A shader uam rejects makes the deko3d backend skip every draw that needs it: missing walls on the console.
if ! python3 "$TOOLS/linux/make_dksh_pack.py" "$SD/shaders.pack" "$SD/shaders.dksh.tmp" --uam "$TOOLS/linux/uam" \
        2>&1 | tee -a "$LOG" || ! grep -q ' 0 rejected by uam' "$LOG"; then
    fail "uam a rejeté des shaders ou a échoué ; détails dans setup_switch.log"
fi
mv -f "$SD/shaders.dksh.tmp" "$SD/shaders.dksh"

step "terminé"
echo "Copiez le dossier « switch » de $ROOT à la RACINE de la carte SD (il se fusionne avec /switch)."
echo "Puis lancez BO1 Zombies en MAINTENANT R sur un jeu installé (mode application). Voir LISEZMOI.txt."
