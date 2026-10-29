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
- une partie solo, ou un serveur dédié à rejoindre. La ligne **Serveur** ouvre son propre écran. On y active ou non
  la connexion, on y saisit l'adresse IP et le port du serveur avec le clavier de la console, et son nom de joueur.
  L'écran montre aussi l'adresse réseau de la console (`docs/SERVER.md`, « Joining from a Switch ») ;
- la map, parmi les 10 maps zombies ;
- l'aim assist : activé ou non, avec ralentissement, verrouillage et visée auto ;
- le compteur de zombies à côté du tableau des scores (mod zinfo : zombies en vie et restants dans la manche),
  activé par défaut ;
- le rendu (deko3d ou OpenGL), la limite de FPS (30 ou 60), les ombres, le filtre de texture et le compteur
  FPS/batterie ;
- les manettes : la liste des manettes détectées, et l'écran de la Switch pour en connecter.

Les touches : croix pour choisir, gauche/droite pour modifier, A pour valider, **+** pour lancer la partie
directement sur la map (ou, avec un serveur, ouvrir le menu du jeu, qui rejoint alors le serveur tout seul), **−** pour
quitter. Dans l'écran du serveur, B revient au menu. Les choix sont gardés dans `sdmc:/switch/bo1zm/menu.cfg`. Au lancement, le
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
(chemins de `Server.sh` : variables `BO1_EXE`, `BO1_INSTALL`, `BO1_ZONES`, `BO1_SERVER` ; `Server.bat` et `Play.bat` lisent
aussi `BO1_INSTALL` et `BO1_ZONES` quand les fichiers du jeu ou les zones sont dans un autre worktree). Liste des maps :
`docs/SERVER.md`, section « Maps ». Les clients se connectent
avec `Play.bat net <adresse>:<port> [pseudo]` (ou `Launch.bat net 172.23.3.156:3074 Player2`), avec
`connect <adresse>:<port>`, ou tout seuls avec `+set cl_autoConnect <adresse>:<port>` dans un `launch.cfg` posé à côté
du programme, et `tools/server_status.py <adresse>` affiche la map et les joueurs.

Commandes admin, tapées dans la console du serveur (ou en rcon avec `tools/server_rcon.py <adresse> <mot de passe>
<commande>`, quand `rcon_password` est défini) ; `<joueur>` est un numéro, un pseudo ou `all` :
- `zm_god <joueur> [0|1]` (invincible), `zm_demigod`, `zm_notarget`, `zm_noclip` ;
- `zm_points <joueur> <montant>` (négatif pour retirer), `zm_setpoints <joueur> <montant>` ;
- `zm_perk <joueur> <perk ...|all>` : `jugg`, `revive`, `speed`, `doubletap`, `staminup`, `phd`, `deadshot`, `mulekick` ;
- `zm_revive <joueur>` (relève un joueur à terre ; un joueur mort est spectateur, c'est `zm_respawn`), `zm_down <joueur>`
  (met à terre sans tuer, pour tester le revive), `zm_kill <joueur>` (tue : directement spectateur),
  `zm_respawn <joueur>` (fait revenir un spectateur tout de suite) ;
- `zm_round <n>`, `zm_reset` (relance la map), `zm_map <map>`, `zm_mods "<mods>"` (change `fs_mods` et relance) ;
- `zm_admin` liste les joueurs et leur état ;
- réglages `zm_startpoints <n>` et `zm_startperks "<perks>"` : points et perks de départ de chaque joueur ;
- `zm_jip_mode immediate|nextround` : un joueur qui rejoint une partie en cours joue tout de suite (par défaut, près
  des autres joueurs, avec les points de départ), ou reste spectateur jusqu'à la fin du round.
- `set sv_maxclients <n>` (dans `server.cfg`, avant la map ; 4 par défaut, 32 au plus) : plus de 4 joueurs. Le jeu n'a
  que 4 personnages : à partir du 5e, chaque nouveau joueur prend le personnage le moins pris (modèle, bras et voix).
  Les points de départ et la difficulté sont ceux d'une partie à 4.
- `set zm_debug_revive 1` : quand un joueur est à terre, le log dit pour chaque autre joueur quelle condition du revive
  passe ou bloque (distance, trigger, orientation, visée, touche utiliser).

Dans la console curses : F4 god, F5 revive, F6 respawn, F7 points, F8 kick, F10 reset (la commande s'écrit sur la
ligne, Entrée l'exécute), Tab affiche le log du joueur sélectionné.

Logs des clients : chaque client (PC et Switch) envoie son log console au serveur qu'il rejoint, en UDP sur le port de
jeu + 1 (3075 pour 3074, à ouvrir aussi dans le pare-feu). Le serveur écrit chaque partie d'un client dans
`<fs_h>/logs/<pseudo>_<date>-<heure>_<session>.log` (`build\server\logs\` avec `Server.bat`), depuis le lancement du
client, chargements compris ; les paquets perdus y sont marqués (`[netlog: lost N packets]`). `bo1_netlog 0` coupe
l'envoi côté client. Le côté script est le mod `mods/admin`, que le serveur ajoute toujours à `fs_mods`. Détails
(en anglais) : `docs/SERVER.md`.

Console d'administration : `tools/zmadmin` (Python et curses, compilée avec Nuitka en `zmadmin` / `zmadmin.exe` dans
les paquets). `zmadmin <adresse> -p <rcon_password>` pilote un serveur : vues Serveurs, Joueurs (une touche par
commande `zm_*`), Partie (round, courant, relance, map, mods), Réglages, Console rcon et Log. Dans la vue
Réglages, rien ne part avant le bouton **Sauvegarder** (ou Ctrl+S) : il envoie les commandes, relit chaque valeur et
marque ce qui est appliqué ou refusé ; chaque réglage est expliqué sous le formulaire, et `?` explique les touches
de chaque vue. Tout se fait au clavier ou à la souris (clic, double-clic, molette). Posée dans un dossier de `make_dist.sh`, elle lance aussi le serveur de ce dossier avec un mot de
passe rcon qu'elle garde, et suit son log en direct. Le serveur n'accepte qu'une commande rcon toutes les 0,5 s et
ne renvoie pas son log en rcon : à distance, les réponses `admin:` du mod ne s'affichent pas. Détails :
`tools/zmadmin/README.md`.


## Paquets prêts à copier
`tools/make_dist.sh` (bash, depuis WSL) part d'un arbre de sources où `setup.ps1` a copié le jeu. Il produit dans
`build/dist` un dossier prêt à copier et une archive par plateforme :
- `BO1Zombies_windows/` (`.zip`) : le client et le serveur 64 bits, et `zmadmin.exe`. `Play.bat` lance le jeu, `Play.bat net
  <adresse[:port]> [nom]` rejoint un serveur, `Server.bat [map] [port]` lance un serveur. Les DLL d'exécution 64 bits
  sont incluses ;
- `BO1Zombies_linux/` (`.tar.gz`) : le client et le serveur Linux, `zmadmin`, avec `play.sh` (`./play.sh net ...`),
  `server.sh [map] [port]` et `shaders.pack` ;
- `BO1Zombies_switch/` (`.zip`) : une arborescence de carte SD, `switch/bo1zm/` avec le `.nro`, `shaders.pack` et
  `shaders.dksh`.

Chaque paquet contient aussi les zones converties, les `main/*.iwd` et le `localization.txt` de l'install (plus
`main/video` sur PC), un `LISEZMOI.txt` et un `VERSION.txt` (commit, date, heure du lien du NRO).

    tools/make_dist.sh                       # les trois
    tools/make_dist.sh switch linux          # une partie
    tools/make_dist.sh --no-build windows    # empaqueter l'existant, échouer si quelque chose manque ou est périmé

Sans `--no-build`, le script compile ce qu'il faut, en incrémental :
- le preset `win64-msvc` ;
- les programmes Linux et Switch dans `--work`, un dossier ext4 (`~/.cache/bo1zm-dist`) ;
- les zones converties, si elles manquent ou sont plus vieilles que les sources du convertisseur (`ffconv --verify`,
  puis `check_shader_pack.py`). Les anciennes zones restent en place tant que les nouvelles n'ont pas passé ces
  vérifications ;
- `shaders.dksh`, quand son `shaders.pack` a changé. Le script échoue si uam rejette un shader.

Autres options : `--out`, `--game`, `--zones`, `--dxsdk`, `--archive auto|zip|tar|none`, `--jobs` ; `--help` les
décrit. Les paquets contiennent vos propres fichiers de jeu : gardez-les pour vos appareils, ne les publiez pas.
