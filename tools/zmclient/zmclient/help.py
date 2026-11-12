"""The texts the views show to explain a key or a setting (French: the players' language)."""

MODE_LABELS = {
    "solo": "Solo (hors ligne)",
    "join": "Rejoindre un serveur",
    "host": "Serveur local + le rejoindre",
}
MODE_HELP = {
    "solo": "Une partie seul sur ce PC, réglée dans la vue Solo (F3) : map, mods, horde, difficulté.",
    "join": "Rejoindre un serveur dédié (LAN ou internet), choisi dans la vue Serveurs (F2). Le serveur décide de "
            "la map, des mods et du god mode.",
    "host": "Démarre le serveur dédié de ce dossier (comme ZmAdmin, qui peut ensuite l'administrer), puis le jeu, "
            "qui le rejoint dès qu'il a chargé sa map. D'autres joueurs du réseau peuvent venir.",
}
WINDOW_LABELS = {"borderless": "Sans bordure (plein écran fenêtré)", "window": "Fenêtre", "fullscreen": "Plein écran"}
TRISTATE_LABELS = {"": "réglage du jeu", "1": "oui", "0": "non"}
DIFFICULTY_LABELS = {0: "Normal", 1: "Nightmare", 2: "Hell"}

ROW_HELP = {
    "mode": "Comment jouer. Entrée : choisir.",
    "map": "La map de la partie solo. Les maps « Mapkit » sont construites par le mod mapkit sur une map de base.",
    "server": "Le serveur à rejoindre : adresse, port et mot de passe des joueurs (g_password, vide s'il n'en a "
              "pas). La vue Serveurs (F2) liste les favoris et ceux du réseau local.",
    "name": "Votre nom sur les serveurs (1 à 15 lettres, chiffres, « _ », « - », « . »). Vide : celui de "
            "config.cfg.",
    "god": "Invincible (bo1_testclient_god : le jeu remet le god mode à chaque image). Solo seulement : un serveur "
           "décide pour ses joueurs.",
    "zinfo": "En tenant Tab, les zombies en vie et ceux qui restent dans la manche s'affichent à gauche (mod zinfo).",
    "noperks": "Pas de machines à atouts (mod noperks). Le Pack-a-Punch et la boîte restent.",
    "quickrestart": "Après la fin de partie, R relance la même map avec les mêmes réglages.",
    "start_points": "Les points de chaque joueur à son arrivée sur la map, au lieu des 500 du jeu (mod admin, "
                    "zm_startpoints). Solo seulement : un serveur décide pour ses joueurs.",
    "start_power": "Le courant est mis à l'arrivée du premier joueur (mod admin, zm_startpower) : les machines, le "
                   "Pack-a-Punch et les portes électriques marchent tout de suite. Sans effet sur une map sans courant.",
    "start_perk": "Un atout donné gratuitement à l'arrivée sur la map (mod admin, zm_startperks). Seulement s'il a "
                  "sa machine sur cette map : Kino et Five n'ont que Juggernog, Quick Revive, Speed Cola et Double "
                  "Tap.",
    "horde": "Mod horde : beaucoup de zombies à la fois ; la manche suivante commence quand le dernier zombie de la "
             "manche est apparu.",
    "horde_max": "Zombies en même temps (24 à 1024). Au-delà de 300 c'est lourd : le jeu active ses options de "
                 "performance (comme le lanceur .hta).",
    "horde_round": "La manche de départ (1 : normal).",
    "horde_napalm": "Zombies napalm (de Shangri-La). Kino et Five seulement (fichiers de spawn du mod horde).",
    "horde_sonic": "Zombies hurleurs (shriekers, de Shangri-La). Kino et Five seulement.",
    "horde_spawn_all": "Les zombies sortent de toutes les fenêtres des zones ouvertes, pas seulement près de vous.",
    "horde_unjam": "Un zombie coincé dans la foule traverse les autres pendant 1,5 s.",
    "horde_clear_round": "La manche suivante n'arrive que quand tous les zombies sont morts (sinon : dès que le "
                         "dernier est apparu, les vivants restent).",
    "difficulty": "Nightmare : George, un astronaute, des zombies napalm et hurleurs, des chiens... dès la manche 1. "
                  "Hell : plusieurs de chaque à la fois. Kino et Five seulement ; charge trois maps de plus (démarrage "
                  "plus lent).",
    "bo2_weapons": "Les armes de Black Ops II (HAMR, AN-94, DSR-50, Ray Gun Mark II...) et leur version Pack-a-Punch, "
                   "dans la boîte (mod bo2weapons). Leur mod.ff se construit depuis votre propre Black Ops II "
                   "(docs/BO2_WEAPONS.md) et se pose dans mods/bo2weapons/. Les armes au mur restent celles de la "
                   "map. Un joueur qui rejoint un serveur avec ces armes doit avoir le même mod.ff (vue Jouer, mode "
                   "Rejoindre : « Armes de Black Ops II »).",
    "join_bo2_weapons": "À cocher pour rejoindre un serveur qui a les armes de Black Ops II (mod bo2weapons) : le jeu "
                        "charge alors le même mods/bo2weapons/mod.ff. Sans lui, le jeu s'arrête en entrant sur ce "
                        "serveur (« Weapon index mismatch »).",
    "window": "Sans bordure : une fenêtre de la taille de l'écran (comme Play.bat). Fenêtre : la taille ci-dessous. "
              "Plein écran : à la résolution de config.cfg (vue Config du jeu, F5).",
    "window_size": "La taille de la fenêtre en mode Fenêtre, LARGEURxHAUTEUR.",
    "fps_counter": "Le petit compteur d'images par seconde du portage, en haut à gauche.",
    "scene_models": "Les modèles que le jeu dessine par image (r_mod_sceneModels ; 1024 dans le jeu d'origine). Une map "
                    "importée par Mapkit en a bien plus (Nuketown : 2 096 props) : à 1024 les plus lointains manquent "
                    "(l'horloge de la tour vue de la rue). 4096 les dessine tous et agrandit d'autant la réserve "
                    "d'éclairage des modèles. Utile surtout pour les maps importées.",
    "aim_assist": "Aide à la visée à la manette (bo1_aimassist). Les trois réglages suivants n'agissent que si elle "
                  "est active. « réglage du jeu » : rien n'est forcé.",
    "aim_slowdown": "La visée ralentit sur un zombie.",
    "aim_lockon": "La visée suit un zombie qui bouge.",
    "aim_autoaim": "La visée saute vers un zombie en visant (ADS).",
    "host_map": "La map que le serveur local charge.",
    "host_port": "Le port UDP du serveur local (3074 par défaut ; le port + 1 reçoit les logs des clients). Ouvrez-"
                 "les dans le pare-feu pour des joueurs d'autres machines.",
    "host_password": "Le mot de passe rcon du serveur local, partagé avec ZmAdmin (son fichier servers.json) : "
                     "ZmAdmin administre le serveur que ZmClient a lancé.",
}

BO2_WEAPON_HELP = {
    0: "les armes de la map seulement",
    1: "les armes de Black Ops II s'ajoutent à celles de la map (bo2weapons_box 0)",
    2: "la boîte ne donne plus que les armes de Black Ops II, plus les armes spéciales de la map : Ray Gun, "
       "Thundergun, Wunderwaffe, singe, Gersh... (bo2weapons_box 1)",
    3: "la boîte ne donne plus que les armes de Black Ops II (bo2weapons_box 2)",
}

VIEW_HELP = {
    "play": [
        "Jouer : choisir comment jouer, puis lancer.",
        "Haut/Bas    choisir une ligne ; Entrée, Espace ou double-clic : la modifier",
        "L           lancer le jeu (et le serveur local en mode Serveur local)",
        "S           arrêter le jeu lancé par ZmClient",
        "K           arrêter le serveur local",
        "La ligne de commande exacte est affichée en bas. Les réglages sont gardés dans profile.json (dossier de "
        "configuration de ZmClient).",
    ],
    "servers": [
        "Serveurs : vos favoris et les serveurs du réseau local, interrogés toutes les 3 secondes (getstatus).",
        "Entrée      rejoindre le serveur choisi (double-clic aussi)",
        "a           ajouter un favori        e  modifier        x  retirer",
        "f           ajouter le serveur du réseau choisi aux favoris",
        "r           interroger maintenant",
        "Un serveur du réseau est trouvé par broadcast sur les ports 3074 à 3083 ; un serveur sur internet doit être "
        "ajouté à la main (son adresse et son port). Le mot de passe d'un favori est celui des joueurs (g_password), "
        "pas le rcon.",
    ],
    "solo": [
        "Solo : la partie hors ligne. Les options grisées ne s'appliquent pas à la map choisie (la raison est "
        "affichée en bas).",
        "Haut/Bas    choisir ; Entrée, Espace ou double-clic : modifier",
        "Les drapeaux de mods sont ceux de tools/mod-launcher.hta (horde_flags.js, nightmare_flags.js), à "
        "l'identique.",
    ],
    "launch": [
        "Lancement : ce que ZmClient ajoute à la ligne de commande dans tous les modes.",
        "Haut/Bas    choisir ; Entrée, Espace ou double-clic : modifier",
        "La ligne de commande gagne sur config.cfg pour ces réglages.",
    ],
    "config": [
        "Config du jeu : players/config.cfg du dossier, le fichier que le jeu écrit en quittant. ZmClient lance le "
        "jeu avec bo1_mod_sharedconfig 1 : un seul fichier pour tous les mods.",
        "Haut/Bas    choisir ; Entrée ou double-clic : modifier (marqué *)",
        "Ctrl+S      sauvegarder (rien n'est écrit avant) ;  u  annuler les modifications",
        "n           modifier un autre dvar du fichier (son nom, puis sa valeur)",
        "b           touches : changer ou retirer une commande liée (bind)",
        "r           relire le fichier",
        "Le jeu réécrit config.cfg en quittant : ZmClient refuse de sauvegarder pendant qu'il tourne.",
    ],
    "extra": [
        "Dvars libres : des commandes ajoutées à la ligne de commande, avant la map ou la connexion.",
        "a           ajouter une ligne (ex. « set r_fog 0 », « set bo1_frameperf 1 »)",
        "e           modifier          x  retirer          Espace  activer / désactiver",
        "Chaque ligne devient une commande « +ligne ». Pas de « + » dans une ligne : le jeu coupe sa ligne de "
        "commande à chaque « + », et n'en lit que 63.",
    ],
    "setup": [
        "Installation : les fichiers du jeu, depuis votre installation de Call of Duty: Black Ops (Steam).",
        "Copie localization.txt, main/*.iwd et main/video/, puis convertit les zones zombies avec le ffconv du "
        "paquet (environ 6 minutes). Votre installation n'est jamais modifiée.",
        "c           choisir l'installation : trouvée dans les bibliothèques Steam, en parcourant les dossiers "
        "(flèches, Entrée pour ouvrir, Retour arrière pour remonter, Espace pour choisir ; une installation est "
        "marquée « Black Ops »), ou en tapant le chemin",
        "v           avec ou sans les vidéos      z  avec ou sans la conversion des zones",
        "i           installer          Échap  annuler une installation en cours",
        "Un fichier déjà copié (même taille, même date) n'est pas recopié. Si ffconv échoue, les zones précédentes "
        "restent. Journal : main/zmclient_setup.log.",
    ],
    "log": [
        "Journal : le console_mp.log du jeu lancé, en direct.",
        "/           filtrer       Espace  pause       PgPréc/PgSuiv, molette : défiler       Fin  dernières lignes",
    ],
}

GLOBAL_HELP = [
    "F1 Jouer  F2 Serveurs  F3 Solo  F4 Lancement  F5 Config du jeu  F6 Dvars libres  F7 Installation  F8 Journal",
    "Tab / Maj+Tab   vue suivante / précédente",
    "?  ou F9    cette aide                q  ou F10   quitter",
    "Souris      clic : choisir un onglet, une ligne ou un bouton ; double-clic : modifier ou exécuter la ligne ; "
    "molette : défiler. Pour sélectionner du texte dans le terminal, tenir Maj (ou lancer zmclient --no-mouse).",
]
