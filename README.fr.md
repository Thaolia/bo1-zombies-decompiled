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
sur ton PC (`tools/ffconv`) transforme les fastfiles de ton install en format 64-bit pour le port, et traduit leurs
shaders en GLSL :

    cd tools/ffconv && cmake --preset ffconv && cmake --build --preset ffconv      # depuis WSL ou Linux, MinGW + python3-clang
    build\ffconv\ffconv.exe --verify --game "C:\...\Call of Duty Black Ops" --out C:\bo1zm

Il lit les zones de toutes les maps zombies (environ 3 Go) et écrit `zone\` et `shaders.pack` dans `--out`, jamais
dans l'install. `--verify` relit chaque zone écrite et la compare à l'originale. Tu copies ensuite le résultat sur la
carte SD, dans `sdmc:/switch/bo1zm/`.

## Lancement sur Switch
Le jeu doit être lancé en mode application (title takeover : maintenir R en lançant un jeu). Le mode applet,
c'est-à-dire l'album, ne donne qu'environ 400 Mo de RAM.

Au lancement, un menu texte s'ouvre avant le jeu. On y choisit :
- la map, parmi les 10 maps zombies ;
- l'aim assist : activé ou non, avec ralentissement, verrouillage et visée auto ;
- le god mode ;
- le rendu (deko3d ou OpenGL), la limite de FPS (30 ou 60), les ombres, le filtre de texture et le compteur
  FPS/batterie ;
- les manettes : la liste des manettes détectées, et l'écran de la Switch pour en connecter.

Les touches : croix pour choisir, gauche/droite pour modifier, A pour valider, **+** pour lancer la partie
directement sur la map, **−** pour quitter. Les choix sont gardés dans `sdmc:/switch/bo1zm/menu.cfg`. Au lancement, le
menu écrit `launch.cfg` (la ligne de commande du moteur) et `gfx.cfg` (le rendu). Le contenu facultatif de
`launch_extra.cfg` est ajouté à la ligne de commande, ce qui sert pour des options de diagnostic. Avec un fichier
vide `skip_menu` dans le même dossier, le jeu démarre sans menu, sur `launch.cfg` et `gfx.cfg` tels quels. C'est
pratique sur un émulateur.

## Build PC d'origine
Les instructions upstream (Windows, MSVC x86, D3D9) sont dans `README.md`, `setup.ps1` et `Play.bat`.

## Build hors-ligne sur PC
La build hors-ligne du port, sans Steam, Bink, NVAPI, DemonWare ni chat vocal, se joue déjà sur Windows avec les
fichiers copiés par `setup.ps1` : `cmake --preset win32-msvc-offline`, `cmake --build --preset
win32-msvc-offline-release`, puis `Play.bat offline`.

## Build 64 bits (en cours)
Kino der Toten se charge et se joue une minute ; les parties plus longues ne sont pas encore testées. Cette build lit
des zones converties : `ffconv --verify --game <install> --out build\zones64`, puis `cmake --preset win64-msvc`,
`cmake --build --preset win64-msvc-release` et `Play.bat x64`.

## Build Linux (en cours)
Sur Linux x86-64, le moteur tourne en serveur dédié, en client headless qui ne dessine rien, ou en client fenêtré
qui rend en OpenGL 4.3 core (clavier et souris ; pas encore de son ni de manette). `cmake --preset linux64-clang
-DDXSDK_DIR=<SDK DirectX de juin 2010>` puis `cmake --build --preset linux64-clang` (clang, Ninja, les fichiers de
développement de SDL2 et de GL (`libsdl2-dev`, `libgl-dev`), et python3-clang pour la vérification du layout des
zones). Il lit les mêmes zones converties. Lance-le depuis un dossier qui contient le programme, le
`localization.txt` de ton install, un dossier `zone` de zones converties et le `shaders.pack` du convertisseur :
`./BO1Zombies +set fs_b <install> +set fs_h <ce dossier> +devmap zombie_theater` pour le client fenêtré, avec `+set
bo1_headless 1` pour le serveur dédié (et `+set bo1_headless_client 1` pour le client headless).
`BO1_GFX_BACKEND=null` force le backend qui ne dessine rien, et `BO1_GL_DEBUG=1` écrit les messages de debug
d'OpenGL dans le log. Les zones converties avant le 2026-10-24 ont besoin d'un nouveau `shaders.pack` (relancer
`ffconv`).

## Serveur dédié zombies (en cours, branche `server`)
C'est le même programme lancé avec `+set bo1_server 1`, sous Windows ou Linux : pas de fenêtre, écoute UDP sur toutes
les interfaces, réglages dans `server.cfg`. `Server.bat [map] [port]` et `./Server.sh [map] [port]` en lancent un
(chemins de `Server.sh` : variables `BO1_EXE`, `BO1_INSTALL`, `BO1_ZONES`, `BO1_SERVER`). Les clients se connectent
avec `Play.bat net <adresse>:<port> [pseudo]` (ou `Launch.bat net 172.23.3.156:3074 Player2`), avec
`connect <adresse>:<port>`, ou tout seuls avec `+set cl_autoConnect <adresse>:<port>` dans un `launch.cfg` posé à côté
du programme, et `tools/server_status.py <adresse>` affiche la map et les joueurs. Détails (en anglais) :
`docs/SERVER.md`.
