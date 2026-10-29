"""Every text the views show to explain a setting, a command or a key (French: the players' language).

Sources: docs/SERVER.md ("Admin commands", "Settings and their order", "More than 4 players") and mods/admin/README.md.
"""

MAPS = [
    ("zombie_theater", "Kino der Toten"),
    ("zombie_pentagon", "Five"),
    ("zombie_cosmodrome", "Ascension"),
    ("zombie_coast", "Call of the Dead"),
    ("zombie_temple", "Shangri-La"),
    ("zombie_moon", "Moon"),
    ("zombie_cod5_prototype", "Nacht der Untoten"),
    ("zombie_cod5_asylum", "Verrückt"),
    ("zombie_cod5_sumpf", "Shi No Numa"),
    ("zombie_cod5_factory", "Der Riese"),
    ("zombietron", "Dead Ops Arcade"),
]
MAP_NAMES = dict(MAPS)

PERKS = [
    ("jugg", "Juggernog : plus de vie"),
    ("revive", "Quick Revive : relève plus vite"),
    ("speed", "Speed Cola : recharge plus vite"),
    ("doubletap", "Double Tap : tire plus vite"),
    ("staminup", "Stamin-Up : court plus vite et plus longtemps"),
    ("phd", "PhD Flopper : pas de dégâts d'explosion ni de chute"),
    ("deadshot", "Deadshot Daiquiri : visée à la tête"),
    ("mulekick", "Mule Kick : une troisième arme"),
]


_PARAGRAPH_STARTS = ("Valeurs", "Effet", "Exemple", "Défaut", "immediate :", "nextround :")


def paragraphs(fragments):
    """SETTINGS_HELP's line fragments joined into paragraphs, to be wrapped at the screen's width."""
    out = []
    for f in fragments:
        if out and not f.startswith(_PARAGRAPH_STARTS):
            out[-1] += " " + f
        else:
            out.append(f)
    return out


def map_label(code):
    name = MAP_NAMES.get(code)
    return f"{name} ({code})" if name else code


SETTINGS_HELP = {
    "sv_hostname": (
        "Nom du serveur",
        "Le nom que getstatus renvoie, et que les clients voient en se connectant.",
        "Valeurs : du texte, sans guillemets ni point-virgule.",
        "Effet : tout de suite.",
        'Exemple : Zombies chez moi',
    ),
    "g_password": (
        "Mot de passe des joueurs",
        "Mot de passe qu'un joueur doit donner avant de se connecter (« set password <mdp> » dans la console du",
        "client). Vide : tout le monde peut entrer.",
        "Effet : pour les prochaines connexions. Les joueurs déjà là restent.",
        "Exemple : laisser vide sur un réseau local.",
    ),
    "rcon_password": (
        "Mot de passe rcon (administration)",
        "Le mot de passe que ZmAdmin et tools/server_rcon.py envoient avec chaque commande. ZmAdmin passe tout seul",
        "au nouveau. Vide couperait rcon, et plus rien ne pourrait commander le serveur : refusé ici.",
        "Valeurs : lettres, chiffres, - _ . (pas d'espace).",
        "Effet : tout de suite.",
    ),
    "zm_startpoints": (
        "Points de départ",
        "Les points de chaque joueur à son premier spawn sur une map. 0 garde ceux du jeu : 500 (1500 après le",
        "round 6 pour un joueur qui rejoint en cours de partie).",
        "Valeurs : un nombre entier, 0 ou plus. Défaut : 0.",
        "Effet : au premier spawn suivant. Les joueurs déjà en jeu ne changent pas : zm_reset l'applique à tous.",
    ),
    "zm_startperks": (
        "Perks de départ",
        "Perks donnés gratuitement à chaque joueur à son premier spawn. Seuls les perks dont la map a la machine",
        "sont donnés (pas de Mule Kick sur Kino, par exemple).",
        "Valeurs : cocher les perks avec Espace. Aucun : le jeu normal.",
        "Effet : au premier spawn suivant, comme les points de départ.",
    ),
    "zm_jip_mode": (
        "Arrivée en cours de partie",
        "immediate : il joue tout de suite, près des autres joueurs, avec les points de départ.",
        "nextround : il reste spectateur jusqu'à la fin du round, comme après une mort.",
        "Défaut : immediate.",
        "Effet : pour le prochain joueur qui arrive.",
    ),
    "zm_debug_revive": (
        "Diagnostic du revive",
        "Oui : quand un joueur est à terre, le log du serveur dit, pour chaque autre joueur, quelle condition du",
        "revive passe ou bloque (distance, trigger, orientation, visée, touche utiliser). Lisible dans la vue Log",
        "en local. Non : rien de plus dans le log. Défaut : non.",
        "Effet : tout de suite.",
    ),
    "g_inactivity": (
        "Expulsion des joueurs inactifs",
        "Secondes sans aucune action avant qu'un joueur soit expulsé. 0 : jamais (le défaut du serveur zombies ;",
        "les 120 s des configs multijoueur ne servent pas ici).",
        "Valeurs : un nombre entier de secondes, 0 ou plus.",
        "Effet : tout de suite.",
    ),
    "sv_maxclients": (
        "Nombre de places",
        "Le jeu est fait pour 4. Au-delà, le mod admin donne à chaque joueur en plus le personnage le moins pris,",
        "et les points de départ et la difficulté d'une partie à 4.",
        "Valeurs : 1 à 32. Défaut : 4.",
        "Effet : au prochain chargement de map. Sauvegarder propose alors zm_reset (la partie recommence).",
    ),
    "fs_mods": (
        "Mods chargés",
        "Les mods du dossier mods/ chargés avec la map, séparés par des espaces. Le serveur ajoute toujours",
        "mods/admin (les commandes zm_*), inutile de le cocher.",
        "Valeurs : cocher les mods avec Espace, ou les taper si le serveur est distant.",
        "Effet : appliqué par zm_mods, qui recharge la map tout de suite : la partie en cours est perdue.",
    ),
}

PLAYER_ACTIONS = [
    ("g", "god", "zm_god", "Invincible : aucun dégât. Appuyer encore l'enlève. Gardé d'une map à l'autre."),
    ("d", "demigod", "zm_demigod", "Prend des dégâts mais ne meurt pas et ne tombe pas à terre."),
    ("n", "notarget", "zm_notarget", "Les zombies l'ignorent."),
    ("c", "noclip", "zm_noclip", "Vole à travers les murs."),
    ("r", "revive", "zm_revive", "Relève un joueur à terre. Sans effet sur un mort : c'est respawn."),
    ("R", "respawn", "zm_respawn", "Fait revenir un spectateur (mort) tout de suite, sans attendre la fin du round."),
    ("p", "points +", "zm_points", "Donne des points (un nombre négatif en retire, jusqu'à 0)."),
    ("s", "points =", "zm_setpoints", "Fixe les points du joueur."),
    ("k", "perks", "zm_perk", "Donne des perks gratuitement (seulement ceux dont la map a la machine)."),
    ("D", "à terre", "zm_down", "Met à terre sans tuer (god est enlevé avant) : pour tester le revive."),
    ("X", "tuer", "zm_kill", "Tue : pas de last stand, spectateur jusqu'à la fin du round (ou respawn)."),
    ("K", "kick", "clientkick", "Expulse le joueur du serveur."),
]
DESTRUCTIVE = {"zm_down", "zm_kill", "clientkick"}

GAME_ACTIONS = [
    ("round", "Aller au round…",
     "zm_round <n> : passe au round n. Entre deux rounds, ça arrive au début du round suivant."),
    ("power", "Allumer le courant",
     "zm_power : allume le courant comme un joueur qui utilise l'interrupteur (sur Kino, use_elec_switch)."),
    ("reset", "Relancer la partie",
     "zm_reset : recharge la map en cours. Nouvelle partie : tous les joueurs repartent du round 1."),
    ("map", "Changer de map…",
     "zm_map <map> : charge une autre map. La partie en cours est perdue ; les joueurs suivent le serveur."),
    ("mods", "Changer les mods…",
     "zm_mods \"<mods>\" : change fs_mods et recharge la map tout de suite (mods/admin reste toujours chargé)."),
    ("admin", "Lister les joueurs (zm_admin)",
     "zm_admin : affiche dans la Console l'état de chaque joueur et les réglages de départ."),
]

VIEW_HELP = {
    "servers": [
        "SERVEURS : les serveurs enregistrés, et le serveur local quand ZmAdmin est dans un dossier BO1Zombies.",
        "",
        "Entrée      se connecter au serveur sélectionné (ou lancer le serveur local s'il est arrêté)",
        "a           ajouter un serveur (nom, adresse, port, mot de passe rcon)",
        "e           modifier le serveur sélectionné",
        "Suppr / x   retirer le serveur sélectionné de la liste",
        "l           lancer le serveur local (map, port, mot de passe rcon)",
        "S           arrêter le serveur local (commande quit, puis arrêt forcé après 15 s)",
        "",
        "Les serveurs sont enregistrés dans le fichier servers.json du dossier de configuration de ZmAdmin.",
        "Un serveur a besoin d'un rcon_password : sans lui, il refuse toute commande à distance.",
    ],
    "players": [
        "JOUEURS : chaque place du serveur, rafraîchie toutes les secondes (getstatus) ; l'état de jeu, la vie et les "
        "drapeaux viennent de zm_admin, demandé toutes les quelques secondes.",
        "",
        "Haut/Bas    choisir le joueur",
        "a           la prochaine action vise tous les joueurs (all)",
    ] + [f"{key:<11} {label:<9} {text}" for key, label, _, text in PLAYER_ACTIONS] + [
        "",
        "Une action part tout de suite (après confirmation pour à terre, tuer et kick). Le mod admin agit à la frame "
        "suivante : la colonne État change à la prochaine lecture de zm_admin.",
        "État : connected/loading (en connexion), playing, down (à terre), dead (mort, spectateur).",
        "Drapeaux : G god, D demigod, N notarget, C noclip, F gelé (pendant l'intro).",
    ],
    "game": [
        "PARTIE : la map, les mods et les réglages de départ, et les actions sur toute la partie.",
        "",
        "Haut/Bas    choisir une action ; l'aide de l'action s'affiche en bas",
        "Entrée      exécuter l'action (confirmation pour celles qui rechargent la map)",
        "",
    ] + [f"{label:<30} {text}" for _, label, text in GAME_ACTIONS],
    "settings": [
        "RÉGLAGES : les réglages du serveur qu'on peut changer en direct.",
        "",
        "Haut/Bas    choisir un réglage ; son explication s'affiche en bas",
        "Entrée      modifier (texte, nombre, choix, cases à cocher)",
        "Espace      basculer un oui/non ou un choix",
        "Ctrl+S      Sauvegarder (comme le bouton)",
        "",
        "Rien ne part au serveur avant Sauvegarder. Une ligne modifiée est marquée *.",
        "Sauvegarder montre d'abord la liste des commandes. Elles partent ensemble, en une seule commande rcon (le "
        "serveur n'en accepte qu'une toutes les 0,5 s), puis ZmAdmin relit chaque réglage : ✓ appliqué, ✗ refusé (la "
        "raison s'affiche sous le formulaire).",
        "Annuler revient aux valeurs du serveur. Relire relit tout.",
        "En local, « Écrire aussi dans server.cfg » garde les valeurs pour le prochain lancement du serveur.",
    ],
    "console": [
        "CONSOLE : n'importe quelle commande du serveur, envoyée en rcon, et sa réponse.",
        "",
        "Entrée      envoyer la commande",
        "Haut/Bas    historique (gardé entre les sessions)",
        "PgPréc/Suiv faire défiler (ou la molette) ; PgSuiv jusqu'en bas revient aux dernières lignes",
        "Échap       effacer la ligne",
        "",
        "Exemples : status, zm_admin, zm_god all 1, set g_password secret, quit.",
        "Les réponses « admin: » du mod arrivent après la commande, dans le log du serveur : en local elles "
        "s'affichent ici en vert ; à distance rcon ne les renvoie pas (le moteur ne transmet que la réponse "
        "immédiate). Les erreurs sont en rouge.",
    ],
    "log": [
        "LOG : le fichier console_mp.log du serveur local, en direct.",
        "",
        "/           filtrer (texte à chercher ; vide pour tout montrer)",
        "Espace      pause / reprise du défilement",
        "PgPréc/Suiv faire défiler (ou la molette) ; Fin revenir aux dernières lignes",
        "",
        "Seulement quand ZmAdmin tourne sur la machine du serveur, dans son dossier BO1Zombies : le moteur n'envoie "
        "pas son log en rcon.",
    ],
}

GLOBAL_HELP = [
    "F1 Serveurs  F2 Joueurs  F3 Partie  F4 Réglages  F5 Console  F6 Log   (ou Tab / Maj+Tab)",
    "?  ou F9    cette aide                q  ou F10   quitter",
    "Souris      clic : choisir un onglet, une ligne, un bouton ou une action de la légende ; double-clic : ouvrir "
    "ou exécuter la ligne ; molette : défiler. Pour sélectionner du texte dans le terminal, tenir Maj (ou lancer "
    "zmadmin --no-mouse).",
]
