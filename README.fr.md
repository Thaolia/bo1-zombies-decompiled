# BO1 Zombies — portage Nintendo Switch 1

Ce dépôt est un fork de [`MisaDev4/bo1-zombies-decompiled`](https://github.com/MisaDev4/bo1-zombies-decompiled),
le moteur décompilé de Black Ops 1 Zombies sur PC. La branche `switch` le porte en homebrew Switch 1 (`.nro`,
AArch64, libnx).

## Objectif v1
Zombies en solo hors-ligne sur les maps de base, sans mods ni réseau.

## État
Travail en cours. Le plan par phases, l'avancement et les décisions sont dans `docs/PORTING.md`. L'architecture du
portage est dans `docs/ARCHITECTURE.md`, et le contrat du renderer (GL maintenant, deko3d possible plus tard) dans
`docs/RENDERER.md`.

## Données du jeu
Aucun fichier du jeu n'est distribué. Il faut posséder Call of Duty: Black Ops sur Steam. Un convertisseur lancé
sur ton PC (`tools/ffconv`, phase 2) transforme les fastfiles de ton install en format 64-bit pour le port. Tu
copies ensuite le résultat sur la carte SD, dans `sdmc:/switch/bo1zm/`.

## Lancement sur Switch
Le jeu doit être lancé en mode application (title takeover : maintenir R en lançant un jeu). Le mode applet,
c'est-à-dire l'album, ne donne qu'environ 400 Mo de RAM.

## Build PC d'origine
Les instructions upstream (Windows, MSVC x86, D3D9) sont dans `README.md`, `setup.ps1` et `Play.bat`.
