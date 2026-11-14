// The start menu of the Switch build. Map, aim assist, god mode, the Tab zombie counter, graphics and controllers are
// chosen with the pad on libnx's text console before the engine starts, so a game opens straight on its map without the
// front end. A second view sets up joining a dedicated server instead (docs/SERVER.md): the game then opens on its own
// menu and joins by itself. Another sets a solo game's start kit (points and perks, given by mods/admin). A third
// chooses which button does what in the game (gamepad_remap.h); this menu itself
// keeps reading the console's buttons as they are, so a layout that went wrong can always be fixed here. The choices
// persist in menu.cfg; Start writes launch.cfg and gfx.cfg from them (launcher_config.cpp), which a skip_menu boot
// (emulator, scripted runs) then reuses as they are. The console font is ASCII only: no accents in the text.

// libnx before anything that could pull the Win32 shim (switch_main.cpp explains why).
#include <switch.h>

#include "launcher_config.h"
#include "../lan_browser.h"
#include "switch_launcher.h"
#include "switch_net.h"

#include <sys/iosupport.h>
#include <sys/stat.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" void Switch_BootLogWrite(const char *text);
extern "C" int Switch_BatteryPercent();

namespace
{
const char *const kMenuConfigPath = "sdmc:/switch/bo1zm/menu.cfg";
const char *const kLaunchConfigPath = "sdmc:/switch/bo1zm/launch.cfg";
const char *const kGfxConfigPath = "sdmc:/switch/bo1zm/gfx.cfg";
const char *const kLaunchExtraPath = "sdmc:/switch/bo1zm/launch_extra.cfg";

// Through the boot log's one writer only: stdout belongs to the console while the menu is up, and a printf would land
// on the screen over the menu.
void Log(const char *fmt, ...)
{
    static const char kPrefix[] = "[launcher] ";
    char text[600];
    std::memcpy(text, kPrefix, sizeof(kPrefix) - 1);
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(text + sizeof(kPrefix) - 1, sizeof(text) - sizeof(kPrefix), fmt, ap);
    va_end(ap);
    size_t len = sizeof(kPrefix) - 1 + (n < 0 ? 0 : size_t(n));
    if (len > sizeof(text) - 2)
        len = sizeof(text) - 2;
    text[len] = '\n';
    text[len + 1] = '\0';
    Switch_BootLogWrite(text);
}

std::string ReadText(const char *path, size_t maxBytes)
{
    std::string text;
    if (FILE *f = std::fopen(path, "rb"))
    {
        text.resize(maxBytes);
        text.resize(std::fread(&text[0], 1, maxBytes, f));
        std::fclose(f);
    }
    return text;
}

// A map is offered when the files it needs beside the game's own are on the SD card (Launcher_MapFile: the player's
// own extraction of another game, which the Switch package holds only when make_dist.sh had one).
bool MapAvailable(int map)
{
    for (int i = 0; i < Launcher_MapFileCount(map); ++i)
    {
        struct stat st;
        if (stat(("sdmc:/switch/bo1zm/" + Launcher_MapFile(map, i)).c_str(), &st) != 0)
            return false;
    }
    return true;
}

bool WriteText(const char *path, const std::string &text)
{
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

// launch_extra.cfg as one line: its line breaks become spaces, runs of blanks one space.
std::string ReadExtraArgs()
{
    const std::string raw = ReadText(kLaunchExtraPath, 512);
    std::string out;
    for (char c : raw)
    {
        const bool blank = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        if (!blank)
            out += c;
        else if (!out.empty() && out.back() != ' ')
            out += ' ';
    }
    if (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

enum View
{
    VIEW_MAIN,
    VIEW_SERVER,
    VIEW_PAD,
    VIEW_KIT,
};

enum Item
{
    // Main view.
    ITEM_SERVER,
    ITEM_MAP,
    ITEM_AIMASSIST,
    ITEM_AIM_SLOWDOWN,
    ITEM_AIM_LOCKON,
    ITEM_AIM_AUTOAIM,
    ITEM_RUNINFO,
    ITEM_GOD,
    ITEM_TABINFO,
    ITEM_KIT,
    ITEM_BACKEND,
    ITEM_RESOLUTION,
    ITEM_MAXFPS,
    ITEM_SHADOWS,
    ITEM_TEXFILTER,
    ITEM_FPSCOUNTER,
    ITEM_PAD,
    ITEM_CONTROLLERS,
    ITEM_START,
    // Server view.
    ITEM_NET_ENABLE,
    ITEM_NET_HOST,
    ITEM_NET_PORT,
    ITEM_NET_NAME,
    ITEM_NET_BO2W,
    ITEM_NET_LANAUTO,
    ITEM_NET_SCAN,
    ITEM_NET_FOUND_FIRST,
    ITEM_NET_FOUND_LAST = ITEM_NET_FOUND_FIRST + 5,
    ITEM_NET_BACK,
    // Pad view: one line per game control (Bo1PadControl order), then reset and back.
    ITEM_PAD_FIRST,
    ITEM_PAD_LAST = ITEM_PAD_FIRST + BO1_PAD_COUNT - 1,
    ITEM_PAD_RESET,
    ITEM_PAD_BACK,
    // Start kit view: the points, one line per perk (kLauncherPerks order), back.
    ITEM_KIT_POINTS,
    ITEM_KIT_POWER,
    ITEM_KIT_PAP,
    ITEM_KIT_PERK_FIRST,
    ITEM_KIT_PERK_LAST = ITEM_KIT_PERK_FIRST + 7,
    ITEM_KIT_FAR,
    ITEM_KIT_BACK,
    ITEM_COUNT
};

bool PadControlItem(int item)
{
    return item >= ITEM_PAD_FIRST && item <= ITEM_PAD_LAST;
}

bool KitPerkItem(int item)
{
    return item >= ITEM_KIT_PERK_FIRST && item <= ITEM_KIT_PERK_LAST;
}

struct ItemInfo
{
    View view;
    int row;
    const char *label; // null on a pad control line: Launcher_PadActionLabel names it
    const char *help;
};

// Console rows (1-based, 45 of them at 1280x720).
const ItemInfo kItems[ITEM_COUNT] = {
    {VIEW_MAIN, 6, "Serveur", "A: rejoindre un serveur dedie (adresse, port, nom). Gauche/Droite: activer."},
    {VIEW_MAIN, 8, "Map", "La map ou la partie commence directement, sans le menu du jeu."},
    {VIEW_MAIN, 10, "Aim assist", "Aide a la visee a la manette sur les zombies."},
    {VIEW_MAIN, 11, "  Ralentissement", "La visee ralentit quand le reticule passe sur un zombie."},
    {VIEW_MAIN, 12, "  Verrouillage", "La visee suit le zombie vise."},
    {VIEW_MAIN, 13, "  Visee auto", "En visant (ZL), le reticule saute sur le zombie le plus proche."},
    {VIEW_MAIN, 14, "Infos speedrun", "Reglages et hash du programme et des zones: au chargement, ou aussi en jeu."},
    {VIEW_MAIN, 15, "God mode", "Les zombies ne peuvent pas tuer le joueur (partie solo)."},
    {VIEW_MAIN, 16, "Compteur zombies", "Zombies en vie et restants dans la manche, a cote du tableau des scores."},
    {VIEW_MAIN, 17, "Depart", "A: points et atouts donnes a l'arrivee sur la map (partie solo)."},
    {VIEW_MAIN, 19, "Rendu", "deko3d: le plus rapide. OpenGL: l'ancien chemin, plus lent."},
    {VIEW_MAIN, 20, "Resolution", "720p: celle de l'ecran. 540p, 480p: image moins nette, plus de FPS (en ligne)."},
    {VIEW_MAIN, 21, "Limite de FPS", "30: plus stable et plus econome en batterie. 60: plus fluide."},
    {VIEW_MAIN, 22, "Ombres", "Ombres dynamiques. Les couper fait gagner des FPS."},
    {VIEW_MAIN, 23, "Filtre de texture", "Nettete des textures au loin. 16x: le plus net. Bilineaire: le plus leger."},
    {VIEW_MAIN, 24, "Compteur FPS", "Compteur de FPS et de batterie en haut a droite."},
    {VIEW_MAIN, 25, "Touches manette", "A: choisir le bouton de chaque action du jeu (tirer, viser, sauter...)."},
    {VIEW_MAIN, 26, "Manettes", "A: ecran de la Switch pour connecter ou changer de manette."},
    {VIEW_MAIN, 35, "DEMARRER", "Lance la partie avec ces reglages (aussi: bouton +)."},
    {VIEW_SERVER, 7, "Connexion", "Activee: la partie rejoint ce serveur au lieu d'une partie solo."},
    {VIEW_SERVER, 9, "Adresse IP", "A: l'adresse IP du PC qui fait tourner le serveur (Server.bat, Server.sh)."},
    {VIEW_SERVER, 10, "Port", "A: le port UDP du serveur, 3074 par defaut."},
    {VIEW_SERVER, 11, "Nom du joueur", "A: votre nom sur le serveur (lettres, chiffres, _ - .), vide: celui du jeu."},
    {VIEW_SERVER, 14, "Armes BO2", "Oui: charge vos armes Black Ops II (mods/bo2weapons), pour un serveur qui les a."},
    {VIEW_SERVER, 12, "Recherche auto", "Oui: cherche les serveurs du reseau local a l'ouverture de cet ecran."},
    {VIEW_SERVER, 13, "Chercher maintenant", "A: cherche les serveurs du reseau local (une seconde)."},
#define NET_FOUND(row) {VIEW_SERVER, row, nullptr, "A: rejoindre ce serveur (adresse et port remplis, connexion activee)."}
    NET_FOUND(15), NET_FOUND(16), NET_FOUND(17), NET_FOUND(18), NET_FOUND(19), NET_FOUND(20),
#undef NET_FOUND
    {VIEW_SERVER, 22, "RETOUR", "Revient au menu (aussi: bouton B)."},
#define PAD_LINE(row) {VIEW_PAD, row, nullptr, "Gauche/Droite: choisir le bouton. L'action qui l'avait prend l'ancien."}
    PAD_LINE(6),  PAD_LINE(7),  PAD_LINE(8),  PAD_LINE(9),  PAD_LINE(10), PAD_LINE(11), PAD_LINE(12), PAD_LINE(13),
    PAD_LINE(14), PAD_LINE(15), PAD_LINE(16), PAD_LINE(17), PAD_LINE(18), PAD_LINE(19), PAD_LINE(20), PAD_LINE(21),
#undef PAD_LINE
    {VIEW_PAD, 23, "Reinitialiser", "A: remet les touches du jeu d'origine."},
    {VIEW_PAD, 25, "RETOUR", "Revient au menu (aussi: bouton B)."},
    {VIEW_KIT, 6, "Points de depart", "Gauche/Droite: les points a l'arrivee sur la map, au lieu des 500 du jeu."},
    {VIEW_KIT, 7, "Courant", "Oui: le courant de la map est mis des l'arrivee (portes, atouts, Pack-a-Punch)."},
    {VIEW_KIT, 8, "Pack-a-Punch", "Oui: un Pack-a-Punch toujours allume au point de depart des joueurs."},
#define KIT_PERK(row) {VIEW_KIT, row, nullptr, "Gauche/Droite ou A: atout gratuit a l'arrivee, si la map a sa machine."}
    KIT_PERK(9), KIT_PERK(10), KIT_PERK(11), KIT_PERK(12), KIT_PERK(13), KIT_PERK(14), KIT_PERK(15), KIT_PERK(16),
#undef KIT_PERK
    {VIEW_KIT, 17, "Zombies eloignes", "Gauche/Droite: un zombie plus loin que ca de tous et non vu est remplace."},
    {VIEW_KIT, 18, "RETOUR", "Revient au menu (aussi: bouton B)."},
};
static_assert(ITEM_KIT_PERK_LAST - ITEM_KIT_PERK_FIRST + 1 == 8, "one kit line per kLauncherPerks entry");
static_assert(sizeof(kItems) / sizeof(kItems[0]) == ITEM_COUNT, "one kItems line per Item");
const int kGraphicsHeaderRow = 18;
const int kControllerFirstRow = 27;
const int kControllerMaxRows = 8; // handheld + players 1-7: the rows up to DEMARRER
const int kPadInfoRow = 28;
const int kNetInfoRow = 24;
const int kKitInfoRow = 20;
const int kHelpRow = 37;
const int kStatusRow = 38;
const int kFooterRow = 40;

// The last LAN search's servers (lan_browser.h), and whether one ran since the server view opened.
std::vector<LanServer> s_found;
bool s_searched;

bool FoundItem(int item)
{
    return item >= ITEM_NET_FOUND_FIRST && item <= ITEM_NET_FOUND_LAST;
}

bool Selectable(const LauncherSettings &s, View view, int item)
{
    if (kItems[item].view != view)
        return false;
    if (item == ITEM_AIM_SLOWDOWN || item == ITEM_AIM_LOCKON || item == ITEM_AIM_AUTOAIM)
        return s.aimAssist;
    // A dedicated server picks the map and decides god mode, the zombie counter and its players' start.
    if (item == ITEM_MAP || item == ITEM_GOD || item == ITEM_TABINFO || item == ITEM_KIT)
        return !s.join;
    if (FoundItem(item))
        return size_t(item - ITEM_NET_FOUND_FIRST) < s_found.size();
    if (item == ITEM_NET_BO2W)
        return s.bo2WeaponsInstalled;
    return true;
}

// Lines whose value comes from the console's keyboard (A), not from left/right.
bool TypedItem(int item)
{
    return item == ITEM_NET_HOST || item == ITEM_NET_PORT || item == ITEM_NET_NAME;
}

// Lines A acts on without a value to change.
bool ActionItem(int item)
{
    return item == ITEM_CONTROLLERS || item == ITEM_START || item == ITEM_NET_BACK || item == ITEM_PAD ||
           item == ITEM_PAD_RESET || item == ITEM_PAD_BACK || item == ITEM_KIT || item == ITEM_KIT_BACK ||
           item == ITEM_NET_SCAN || FoundItem(item);
}

const char *ItemLabel(int item)
{
    if (FoundItem(item))
        return "";
    if (KitPerkItem(item))
        return kLauncherPerks[item - ITEM_KIT_PERK_FIRST].label;
    return PadControlItem(item) ? Launcher_PadActionLabel(item - ITEM_PAD_FIRST) : kItems[item].label;
}

const char *YesNo(bool v)
{
    return v ? "Oui" : "Non";
}

std::string ValueText(const LauncherSettings &s, int item)
{
    switch (item)
    {
    case ITEM_SERVER:
        if (!s.join)
            return "Desactive (partie solo)";
        return s.serverHost.empty() ? "Active, adresse a saisir" : "Active: " + Launcher_ServerAddress(s);
    case ITEM_MAP:
        return kLauncherMaps[s.map].label;
    case ITEM_AIMASSIST:
        return s.aimAssist ? "Active" : "Desactive";
    case ITEM_AIM_SLOWDOWN:
        return YesNo(s.aimSlowdown);
    case ITEM_AIM_LOCKON:
        return YesNo(s.aimLockon);
    case ITEM_AIM_AUTOAIM:
        return YesNo(s.aimAutoaim);
    case ITEM_GOD:
        return YesNo(s.god);
    case ITEM_TABINFO:
        return YesNo(s.tabInfo);
    case ITEM_KIT:
    {
        int perks = 0;
        for (int i = 0; i < kLauncherPerkCount; ++i)
            perks += (s.startPerks >> i) & 1;
        if (!s.startPoints && !perks && !s.startPower && !s.startPap && !s.farRespawn)
            return "Celui du jeu";
        std::string text = s.startPoints ? std::to_string(s.startPoints) + " points" : "500 points";
        if (perks)
            text += ", " + std::to_string(perks) + (perks > 1 ? " atouts" : " atout");
        if (s.startPower)
            text += ", courant";
        if (s.startPap)
            text += ", Pack-a-Punch";
        if (s.farRespawn > 0)
            text += ", rapproches";
        return text;
    }
    case ITEM_KIT_POINTS:
        return s.startPoints ? std::to_string(s.startPoints) : "Ceux du jeu (500)";
    case ITEM_BACKEND:
        return s.backend == LAUNCHER_BACKEND_DEKO3D ? "deko3d" : "OpenGL";
    case ITEM_KIT_POWER:
        return YesNo(s.startPower);
    case ITEM_KIT_PAP:
        return YesNo(s.startPap);
    case ITEM_KIT_FAR:
        return s.farRespawn > 0 ? "Au-dela de " + std::to_string(s.farRespawn) : std::string("Non");
    case ITEM_RESOLUTION:
        return s.height == 480 ? "480p" : s.height == 540 ? "540p" : "720p";
    case ITEM_MAXFPS:
        return std::to_string(s.maxFps);
    case ITEM_SHADOWS:
        return YesNo(s.shadows);
    case ITEM_TEXFILTER:
        return Launcher_TexFilterLabel(s.texFilter);
    case ITEM_FPSCOUNTER:
        return YesNo(s.fpsCounter);
    case ITEM_RUNINFO:
        return s.runInfo == 0 ? "Non" : s.runInfo == 1 ? "Au chargement" : "Chargement + en jeu";
    case ITEM_NET_ENABLE:
        return s.join ? "Activee" : "Desactivee";
    case ITEM_NET_LANAUTO:
        return YesNo(s.lanAuto);
    case ITEM_NET_BO2W:
        return s.bo2WeaponsInstalled ? YesNo(s.joinBo2Weapons) : "absentes de la carte SD";
    case ITEM_NET_SCAN:
        if (!s_searched)
            return "pas encore lancee";
        return s_found.empty() ? "aucun serveur trouve"
                               : std::to_string(s_found.size()) + (s_found.size() > 1 ? " serveurs trouves" : " serveur trouve");
    case ITEM_NET_HOST:
        return s.serverHost.empty() ? "(a saisir)" : s.serverHost;
    case ITEM_NET_PORT:
        return std::to_string(s.serverPort);
    case ITEM_NET_NAME:
        return s.playerName.empty() ? "(celui du jeu)" : s.playerName;
    case ITEM_PAD:
        return Bo1_PadRemapIsIdentity(s.pad) ? "Celles du jeu" : "Personnalisees";
    case ITEM_PAD_RESET:
        return Bo1_PadRemapIsIdentity(s.pad) ? "deja les touches du jeu" : "touches du jeu d'origine";
    default:
        if (PadControlItem(item))
            return Launcher_PadButtonLabel(s.pad.source[item - ITEM_PAD_FIRST]);
        if (KitPerkItem(item))
            return YesNo((s.startPerks >> (item - ITEM_KIT_PERK_FIRST)) & 1);
        return "";
    }
}

// Left/right (dir -1/+1) and A (+1) on a value line.
void Change(LauncherSettings *s, int item, int dir)
{
    switch (item)
    {
    case ITEM_SERVER:
    case ITEM_NET_ENABLE:
        s->join = !s->join;
        break;
    case ITEM_NET_LANAUTO:
        s->lanAuto = !s->lanAuto;
        break;
    case ITEM_NET_BO2W:
        s->joinBo2Weapons = !s->joinBo2Weapons;
        break;
    case ITEM_MAP:
        do
            s->map = (s->map + dir + kLauncherMapCount) % kLauncherMapCount;
        while (!MapAvailable(s->map));
        break;
    case ITEM_AIMASSIST:
        s->aimAssist = !s->aimAssist;
        break;
    case ITEM_AIM_SLOWDOWN:
        s->aimSlowdown = !s->aimSlowdown;
        break;
    case ITEM_AIM_LOCKON:
        s->aimLockon = !s->aimLockon;
        break;
    case ITEM_AIM_AUTOAIM:
        s->aimAutoaim = !s->aimAutoaim;
        break;
    case ITEM_GOD:
        s->god = !s->god;
        break;
    case ITEM_TABINFO:
        s->tabInfo = !s->tabInfo;
        break;
    case ITEM_BACKEND:
        s->backend = s->backend == LAUNCHER_BACKEND_DEKO3D ? LAUNCHER_BACKEND_GL : LAUNCHER_BACKEND_DEKO3D;
        break;
    case ITEM_KIT_POWER:
        s->startPower = !s->startPower;
        break;
    case ITEM_KIT_PAP:
        s->startPap = !s->startPap;
        break;
    case ITEM_RESOLUTION:
    {
        static const int kHeights[] = {720, 540, 480};
        int at = 0;
        while (at < 2 && kHeights[at] != s->height)
            ++at;
        s->height = kHeights[(at + dir + 3) % 3];
        break;
    }
    case ITEM_MAXFPS:
        s->maxFps = s->maxFps == 60 ? 30 : 60;
        break;
    case ITEM_SHADOWS:
        s->shadows = !s->shadows;
        break;
    case ITEM_TEXFILTER:
        s->texFilter = LauncherTexFilter((s->texFilter + dir + LAUNCHER_TEXFILTER_COUNT) % LAUNCHER_TEXFILTER_COUNT);
        break;
    case ITEM_FPSCOUNTER:
        s->fpsCounter = !s->fpsCounter;
        break;
    case ITEM_RUNINFO:
        s->runInfo = (s->runInfo + dir + 3) % 3;
        break;
    case ITEM_KIT_FAR:
    {
        int at = 0;
        while (at < kLauncherFarRespawnCount && kLauncherFarRespawns[at] != s->farRespawn)
            ++at;
        at = (at % kLauncherFarRespawnCount + dir + kLauncherFarRespawnCount) % kLauncherFarRespawnCount;
        s->farRespawn = kLauncherFarRespawns[at];
        break;
    }
    case ITEM_KIT_POINTS:
    {
        int at = 0;
        while (at < kLauncherStartPointCount && kLauncherStartPoints[at] != s->startPoints)
            ++at;
        at = (at % kLauncherStartPointCount + dir + kLauncherStartPointCount) % kLauncherStartPointCount;
        s->startPoints = kLauncherStartPoints[at];
        break;
    }
    default:
        if (KitPerkItem(item))
            s->startPerks ^= 1u << (item - ITEM_KIT_PERK_FIRST);
        if (PadControlItem(item))
        {
            const int control = item - ITEM_PAD_FIRST;
            const int physical = (s->pad.source[control] + dir + BO1_PAD_COUNT) % BO1_PAD_COUNT;
            Bo1_PadRemapAssign(&s->pad, control, physical);
        }
        break;
    }
}

int Step(const LauncherSettings &s, View view, int item, int dir)
{
    do
        item = (item + dir + ITEM_COUNT) % ITEM_COUNT;
    while (!Selectable(s, view, item));
    return item;
}

const char *StyleName(u32 style)
{
    if (style & HidNpadStyleTag_NpadHandheld)
        return "Joy-Con attaches a la console";
    if (style & HidNpadStyleTag_NpadFullKey)
        return "Manette Pro";
    if (style & HidNpadStyleTag_NpadJoyDual)
        return "Paire de Joy-Con";
    if (style & HidNpadStyleTag_NpadJoyLeft)
        return "Joy-Con gauche seul";
    if (style & HidNpadStyleTag_NpadJoyRight)
        return "Joy-Con droit seul";
    if (style & HidNpadStyleTag_NpadGc)
        return "Manette GameCube";
    return "Autre manette";
}

// One line per connected controller, "" when none: the menu redraws when it changes.
std::string ControllerList()
{
    static const HidNpadIdType kIds[] = {HidNpadIdType_Handheld, HidNpadIdType_No1, HidNpadIdType_No2,
                                         HidNpadIdType_No3,      HidNpadIdType_No4, HidNpadIdType_No5,
                                         HidNpadIdType_No6,      HidNpadIdType_No7, HidNpadIdType_No8};
    std::string list;
    for (HidNpadIdType id : kIds)
    {
        const u32 style = hidGetNpadStyleSet(id);
        if (!style)
            continue;
        char line[96];
        if (id == HidNpadIdType_Handheld)
            std::snprintf(line, sizeof(line), "Portable : %s\n", StyleName(style));
        else
            std::snprintf(line, sizeof(line), "Joueur %d : %s\n", int(id) + 1, StyleName(style));
        list += line;
    }
    return list;
}

void LogControllers(const std::string &list)
{
    std::string oneLine = list.empty() ? "none" : list;
    for (char &c : oneLine)
        if (c == '\n')
            c = ';';
    Log("controllers: %s", oneLine.c_str());
}

// The console's controller screen: solo game, so one player, handheld allowed (single mode). The ForSystem call,
// because the game's own ShowControllerSupport only shows its screen when the connected pads fall short of the
// request: with the Joy-Cons attached it returned at once (0x5d59, LibAppletBadExit) and the screen never appeared.
// flag false: the game's screen, not the HOME menu's.
void ShowControllerApplet()
{
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.enable_single_mode = 1;
    HidLaControllerSupportResultInfo info = {};
    const Result rc = hidLaShowControllerSupportForSystem(&info, &arg, false);
    Log("controller applet -> 0x%x, players %d, selected id %u", rc, int(info.player_count), info.selected_id);
}

// The console's keyboard applet, which takes the screen and gives it back as the controller screen does. True with the
// typed text in *text; false, *text unchanged, when the player cancelled it (B) or it failed.
bool AskText(int item, std::string *text)
{
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc))
    {
        Log("swkbdCreate -> 0x%x", rc);
        return false;
    }
    swkbdConfigMakePresetDefault(&kbd);
    if (item == ITEM_NET_HOST)
    {
        // Digits, with '.' on the key beside 0.
        swkbdConfigSetType(&kbd, SwkbdType_NumPad);
        swkbdConfigSetLeftOptionalSymbolKey(&kbd, ".");
        swkbdConfigSetHeaderText(&kbd, "Adresse IP du serveur");
        swkbdConfigSetGuideText(&kbd, "192.168.1.20");
        swkbdConfigSetStringLenMax(&kbd, 63);
    }
    else if (item == ITEM_NET_PORT)
    {
        swkbdConfigSetType(&kbd, SwkbdType_NumPad);
        swkbdConfigSetHeaderText(&kbd, "Port du serveur");
        swkbdConfigSetGuideText(&kbd, "3074");
        swkbdConfigSetStringLenMax(&kbd, 5);
    }
    else
    {
        swkbdConfigSetKeySetDisableBitmask(&kbd, SwkbdKeyDisableBitmask_Space);
        swkbdConfigSetHeaderText(&kbd, "Nom du joueur");
        swkbdConfigSetStringLenMax(&kbd, 15);
    }
    swkbdConfigSetInitialText(&kbd, text->c_str());
    char out[64] = "";
    rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);
    Log("keyboard (%s) -> 0x%x \"%s\"", kItems[item].label, rc, R_SUCCEEDED(rc) ? out : "");
    if (R_FAILED(rc))
        return false;
    *text = out;
    return true;
}

// A on a keyboard line: takes the typed value, or says why it was refused ("" when taken or cancelled).
std::string EditTyped(LauncherSettings *s, int item)
{
    std::string typed = item == ITEM_NET_HOST   ? s->serverHost
                        : item == ITEM_NET_PORT ? std::to_string(s->serverPort)
                                                : s->playerName;
    if (!AskText(item, &typed))
        return "";
    if (item == ITEM_NET_HOST)
    {
        if (!typed.empty() && !Launcher_ValidServerHost(typed))
            return "Adresse refusee: chiffres et points seulement, par exemple 192.168.1.20.";
        s->serverHost = typed;
    }
    else if (item == ITEM_NET_PORT)
    {
        if (!Launcher_ParsePort(typed, &s->serverPort))
            return "Port refuse: un nombre de 1 a 65535.";
    }
    else
    {
        if (!typed.empty() && !Launcher_ValidPlayerName(typed))
            return "Nom refuse: 1 a 15 lettres, chiffres, _ - . sans espace.";
        s->playerName = typed;
    }
    return "";
}

void At(int row, int col)
{
    std::printf("\x1b[%d;%dH", row, col);
}

// Bright black is the console's grey, for lines that do not apply.
const char *const kColorNormal = "\x1b[0m";
const char *const kColorSelected = "\x1b[33;1m";
const char *const kColorDisabled = "\x1b[30;1m";
const char *const kColorTitle = "\x1b[36;1m";
const char *const kColorWarn = "\x1b[31;1m";

struct Screen
{
    const char *buildStamp;
    int battery;
    std::string controllers;
    std::string network; // the console's address, or "aucun"
    bool networkUp;
    std::string status; // why the last Start or keyboard entry was refused, until the next change
};

void DrawItem(const LauncherSettings &s, View view, int item, int selected)
{
    const ItemInfo &info = kItems[item];
    const bool isSelected = item == selected;
    At(info.row, 3);
    if (item == ITEM_START || item == ITEM_NET_BACK || item == ITEM_PAD_BACK || item == ITEM_KIT_BACK)
    {
        std::printf("%s%s[ %s ]%s", isSelected ? "> " : "  ", isSelected ? "\x1b[30;43m" : kColorSelected, info.label,
                    kColorNormal);
        return;
    }
    const char *color = isSelected ? kColorSelected : (Selectable(s, view, item) ? kColorNormal : kColorDisabled);
    if (FoundItem(item))
    {
        const size_t index = size_t(item - ITEM_NET_FOUND_FIRST);
        if (index < s_found.size())
        {
            const LanServer &f = s_found[index];
            std::string where = f.address + ":" + std::to_string(f.port);
            std::string count = std::to_string(f.players) + (f.maxPlayers > 0 ? "/" + std::to_string(f.maxPlayers) : "");
            std::printf("%s%s %-21s %-22.22s %-16.16s %s%s", color, isSelected ? ">" : " ", where.c_str(),
                        f.hostname.c_str(), f.map.c_str(), count.c_str(), kColorNormal);
        }
        return;
    }
    std::printf("%s%s %-24s", color, isSelected ? ">" : " ", ItemLabel(item));
    if (item == ITEM_CONTROLLERS)
        std::printf("[A] ecran des manettes%s", kColorNormal);
    else if (item == ITEM_SERVER || item == ITEM_PAD || item == ITEM_PAD_RESET || item == ITEM_KIT || TypedItem(item) ||
             item == ITEM_NET_SCAN)
        std::printf("[A] %s%s", ValueText(s, item).c_str(), kColorNormal);
    else if (PadControlItem(item))
    {
        // The game's own button beside a changed one, so a custom layout reads at a glance.
        const int control = item - ITEM_PAD_FIRST;
        std::printf("<  %-7s >", ValueText(s, item).c_str());
        if (s.pad.source[control] != control)
            std::printf("   %s(jeu: %s)", kColorDisabled, Launcher_PadButtonLabel(control));
        std::printf("%s", kColorNormal);
    }
    else
        std::printf("<  %s  >%s", ValueText(s, item).c_str(), kColorNormal);
}

void Draw(const LauncherSettings &s, View view, int selected, const Screen &screen)
{
    std::printf("\x1b[2J");
    At(2, 3);
    const char *title = view == VIEW_MAIN     ? "LANCEUR"
                        : view == VIEW_SERVER ? "CONNEXION AU SERVEUR"
                        : view == VIEW_KIT    ? "DEPART (POINTS ET ATOUTS)"
                                              : "TOUCHES DE LA MANETTE";
    std::printf("%sBLACK OPS ZOMBIES - %s%s", kColorTitle, title, kColorNormal);
    if (screen.battery >= 0)
    {
        At(2, 64);
        std::printf("Batterie %3d%%", screen.battery);
    }
    At(3, 3);
    std::printf("%sbuild %s%s", kColorDisabled, screen.buildStamp, kColorNormal);
    At(4, 3);
    std::printf("----------------------------------------------------------------------------");

    for (int item = 0; item < ITEM_COUNT; ++item)
        if (kItems[item].view == view)
            DrawItem(s, view, item, selected);

    if (view == VIEW_MAIN)
    {
        At(kGraphicsHeaderRow, 3);
        std::printf("%sGraphismes%s", kColorTitle, kColorNormal);
        if (screen.controllers.empty())
        {
            At(kControllerFirstRow, 7);
            std::printf("%sAucune manette detectee%s", kColorWarn, kColorNormal);
        }
        else
        {
            int row = kControllerFirstRow;
            size_t pos = 0;
            while (pos < screen.controllers.size() && row < kControllerFirstRow + kControllerMaxRows)
            {
                const size_t end = screen.controllers.find('\n', pos);
                At(row++, 7);
                std::printf("%s", screen.controllers.substr(pos, end - pos).c_str());
                pos = end + 1;
            }
        }
    }
    else if (view == VIEW_PAD)
    {
        At(kPadInfoRow, 3);
        std::printf("%sA gauche l'action du jeu, a droite le bouton de la Switch qui la fait.%s", kColorTitle,
                    kColorNormal);
        At(kPadInfoRow + 1, 5);
        std::printf("Les menus du jeu suivent aussi: valider = Sauter, retour = S'accroupir.");
        At(kPadInfoRow + 2, 5);
        std::printf("Ce lanceur garde toujours les boutons de la console: une erreur se corrige ici.");
        At(kPadInfoRow + 3, 5);
        std::printf("Les sticks (deplacement, camera) ne changent pas.");
    }
    else if (view == VIEW_KIT)
    {
        At(kKitInfoRow, 3);
        std::printf("%sDonnes a chaque partie solo, a l'arrivee sur la map (mod admin).%s", kColorTitle, kColorNormal);
        At(kKitInfoRow + 1, 5);
        std::printf("Un atout n'est donne que si la map a sa machine:");
        At(kKitInfoRow + 2, 5);
        std::printf("Kino et Five n'ont que Juggernog, Quick Revive, Speed Cola et Double Tap.");
        At(kKitInfoRow + 3, 5);
        std::printf("Sur un serveur, c'est le serveur qui decide.");
    }
    else
    {
        At(kNetInfoRow, 3);
        std::printf("%sReseau de la console%s", kColorTitle, kColorNormal);
        At(kNetInfoRow + 1, 5);
        if (screen.networkUp)
            std::printf("Connectee, adresse IP %s", screen.network.c_str());
        else
            std::printf("%sAucun reseau: activez le Wi-Fi (mode avion coupe).%s", kColorWarn, kColorNormal);
        At(kNetInfoRow + 3, 3);
        std::printf("%sAu demarrage%s", kColorTitle, kColorNormal);
        At(kNetInfoRow + 4, 5);
        if (!s.join)
            std::printf("Partie solo sur %s (connexion desactivee).", kLauncherMaps[s.map].label);
        else if (s.serverHost.empty())
            std::printf("%sAdresse du serveur a saisir.%s", kColorWarn, kColorNormal);
        else
        {
            std::printf("Le menu du jeu s'ouvre puis rejoint %s.", Launcher_ServerAddress(s).c_str());
            At(kNetInfoRow + 5, 5);
            std::printf("Il reessaie tout seul si le serveur n'est pas encore lance ou redemarre.");
        }
        At(kNetInfoRow + 7, 3);
        std::printf("%sLe serveur choisit la map, le god mode et le compteur de zombies.%s", kColorDisabled, kColorNormal);
    }

    At(kHelpRow, 3);
    std::printf("%s", kItems[selected].help);
    if (!screen.status.empty())
    {
        At(kStatusRow, 3);
        std::printf("%s%s%s", kColorWarn, screen.status.c_str(), kColorNormal);
    }
    At(kFooterRow, 3);
    std::printf("----------------------------------------------------------------------------");
    At(kFooterRow + 1, 3);
    if (view == VIEW_MAIN)
        std::printf("Haut/Bas: choisir  Gauche/Droite: modifier  A: valider  +: jouer  -: quitter");
    else if (view == VIEW_KIT)
        std::printf("Haut/Bas: choisir  Gauche/Droite: modifier  A: valider  B: retour  +: jouer  -: quitter");
    else if (view == VIEW_PAD)
        std::printf("Haut/Bas: choisir  Gauche/Droite: bouton  A: valider  B: retour  +: jouer  -: quitter");
    else
        std::printf("Haut/Bas: choisir  A: modifier  B: retour  +: jouer  -: quitter");
    At(kFooterRow + 2, 3);
    std::printf("%sReglages: sdmc:/switch/bo1zm/menu.cfg   (fichier skip_menu: sans menu)%s", kColorDisabled,
                kColorNormal);
    consoleUpdate(nullptr);
}

// Held directions repeat after 350 ms, every 120 ms, so the map list scrolls without one press per entry.
struct Repeat
{
    u64 heldSince = 0;
    u64 lastFire = 0;

    bool Fire(bool down, bool held, u64 now)
    {
        if (down)
        {
            heldSince = lastFire = now;
            return true;
        }
        if (!held)
            return false;
        if (armTicksToNs(now - heldSince) >= 350000000ull && armTicksToNs(now - lastFire) >= 120000000ull)
        {
            lastFire = now;
            return true;
        }
        return false;
    }
};

// Why Start cannot go yet, or "".
std::string StartRefusal(const LauncherSettings &s, bool networkUp)
{
    if (s.join && s.serverHost.empty())
        return "Entrez l'adresse du serveur (Serveur, A) ou desactivez la connexion.";
    if (s.join && !networkUp)
        return "Pas de reseau: activez le Wi-Fi (mode avion coupe) ou jouez en solo.";
    return "";
}
} // namespace

LauncherResult Launcher_Run(const char *buildStamp, char *line, size_t lineSize, char *backend, size_t backendSize)
{
    LauncherSettings s = Launcher_ParseSettings(ReadText(kMenuConfigPath, 4096));
    if (!MapAvailable(s.map))
    {
        Log("menu.cfg map %s: its files are not on the SD card, Kino instead", Launcher_MapKey(s.map).c_str());
        s.map = 0;
    }
    struct stat bo2weapons;
    s.bo2WeaponsInstalled = stat("sdmc:/switch/bo1zm/mods/bo2weapons/mod.ff", &bo2weapons) == 0;
    Log("menu.cfg read: map %s, backend %s, server %s (%s), BO2 weapons %s", Launcher_MapKey(s.map).c_str(),
        Launcher_BackendName(s.backend),
        s.serverHost.empty() ? "-" : Launcher_ServerAddress(s).c_str(), s.join ? "join" : "solo",
        !s.bo2WeaponsInstalled ? "not on the SD card" : s.joinBo2Weapons ? "loaded to join" : "off");
    Screen screen;
    screen.buildStamp = buildStamp;
    // The first battery query logs through stdout (switch_main.cpp's BootLog), which the console would print over the
    // menu: make it before consoleInit.
    screen.battery = Switch_BatteryPercent();

    const devoptab_t *const stdoutDevice = devoptab_list[STD_OUT];
    const devoptab_t *const stderrDevice = devoptab_list[STD_ERR];
    consoleInit(nullptr);
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeAny(&pad);

    View view = VIEW_MAIN;
    int selected = ITEM_START; // the last settings are one press away
    screen.controllers = ControllerList();
    LogControllers(screen.controllers);
    char address[32];
    screen.networkUp = SwitchNet_Address(address, sizeof(address));
    screen.network = address;
    bool dirty = true;
    // The server view's LAN search: about a second, so the screen first says it is searching. Ryujinx 1.1.1375 stops
    // on the first UDP reply (recvfrom, switch-homebrew.md): judge it on the console.
    auto searchLan = [&]() {
        screen.status = "Recherche des serveurs du reseau local...";
        Draw(s, view, selected, screen);
        unsigned int ip = 0, mask = 0;
        s_searched = true;
        if (!SwitchNet_AddressAndMask(&ip, &mask))
        {
            s_found.clear();
            screen.status = "Pas de reseau: activez le Wi-Fi (mode avion coupe).";
            return;
        }
        s_found = LanBrowser_Scan(LanBrowser_Broadcasts(ip, mask), kLanFirstPort, kLanPortCount, 1000);
        if (s_found.size() > size_t(ITEM_NET_FOUND_LAST - ITEM_NET_FOUND_FIRST + 1))
            s_found.resize(size_t(ITEM_NET_FOUND_LAST - ITEM_NET_FOUND_FIRST + 1));
        screen.status.clear();
        Log("LAN search: %d servers", int(s_found.size()));
        for (const LanServer &f : s_found)
            Log("  %s:%d \"%s\" %s %d/%d players, %d ms", f.address.c_str(), f.port, f.hostname.c_str(), f.map.c_str(),
                f.players, f.maxPlayers, f.pingMs);
        if (FoundItem(selected) && !Selectable(s, view, selected))
            selected = ITEM_NET_SCAN;
    };
    u64 lastPoll = armGetSystemTick();
    Repeat vertical, horizontal;
    LauncherResult result = LAUNCHER_QUIT;

    // appletMainLoop must never be called again once it said false (switch-homebrew.md): leaving the loop on it is
    // final, and main then returns without the engine.
    while (appletMainLoop())
    {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        const u64 held = padGetButtons(&pad);
        const u64 now = armGetSystemTick();
        bool start = false;

        if (vertical.Fire(down & (HidNpadButton_AnyUp | HidNpadButton_AnyDown),
                          held & (HidNpadButton_AnyUp | HidNpadButton_AnyDown), now))
        {
            selected = Step(s, view, selected, (held & HidNpadButton_AnyUp) ? -1 : 1);
            dirty = true;
        }
        if (horizontal.Fire(down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight),
                            held & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight), now) &&
            !ActionItem(selected) && !TypedItem(selected))
        {
            Change(&s, selected, (held & HidNpadButton_AnyLeft) ? -1 : 1);
            screen.status.clear();
            dirty = true;
        }
        if (down & HidNpadButton_A)
        {
            screen.status.clear();
            if (selected == ITEM_START)
                start = true;
            else if (selected == ITEM_CONTROLLERS)
            {
                ShowControllerApplet();
                screen.controllers = ControllerList();
                LogControllers(screen.controllers);
            }
            else if (selected == ITEM_SERVER)
            {
                view = VIEW_SERVER;
                selected = s.join ? ITEM_NET_HOST : ITEM_NET_ENABLE;
                if (s.lanAuto)
                    searchLan();
            }
            else if (selected == ITEM_NET_SCAN)
                searchLan();
            else if (FoundItem(selected))
            {
                const LanServer &f = s_found[size_t(selected - ITEM_NET_FOUND_FIRST)];
                s.serverHost = f.address;
                s.serverPort = f.port;
                s.join = true;
                screen.status = "Serveur choisi: " + Launcher_ServerAddress(s) + " (+ pour rejoindre).";
                Log("LAN search: %s chosen", Launcher_ServerAddress(s).c_str());
            }
            else if (selected == ITEM_NET_BACK)
            {
                view = VIEW_MAIN;
                selected = ITEM_SERVER;
            }
            else if (selected == ITEM_PAD)
            {
                view = VIEW_PAD;
                selected = ITEM_PAD_FIRST + int(BO1_PAD_RTRIGGER); // Tirer, the line players change first
            }
            else if (selected == ITEM_PAD_RESET)
            {
                Bo1_PadRemapIdentity(&s.pad);
                Log("pad layout reset to the game's");
            }
            else if (selected == ITEM_PAD_BACK)
            {
                view = VIEW_MAIN;
                selected = ITEM_PAD;
            }
            else if (selected == ITEM_KIT)
            {
                view = VIEW_KIT;
                selected = ITEM_KIT_POINTS;
            }
            else if (selected == ITEM_KIT_BACK)
            {
                view = VIEW_MAIN;
                selected = ITEM_KIT;
            }
            else if (TypedItem(selected))
                screen.status = EditTyped(&s, selected);
            else
                Change(&s, selected, 1);
            dirty = true;
        }
        if ((down & HidNpadButton_B) && view != VIEW_MAIN)
        {
            screen.status.clear();
            selected = view == VIEW_SERVER ? ITEM_SERVER : view == VIEW_KIT ? ITEM_KIT : ITEM_PAD;
            view = VIEW_MAIN;
            dirty = true;
        }
        if (down & HidNpadButton_Plus)
            start = true;
        if (start)
        {
            screen.status = StartRefusal(s, screen.networkUp);
            if (screen.status.empty())
            {
                result = LAUNCHER_START;
                break;
            }
            Log("start refused: %s", screen.status.c_str());
            dirty = true;
        }
        if (down & HidNpadButton_Minus)
        {
            Log("quit from the menu");
            break;
        }

        if (armTicksToNs(now - lastPoll) >= 500000000ull)
        {
            lastPoll = now;
            const std::string list = ControllerList();
            if (list != screen.controllers)
            {
                screen.controllers = list;
                LogControllers(screen.controllers);
                dirty = true;
            }
            const int level = Switch_BatteryPercent();
            if (level != screen.battery)
            {
                screen.battery = level;
                dirty = true;
            }
            const bool up = SwitchNet_Address(address, sizeof(address));
            if (up != screen.networkUp || screen.network != address)
            {
                screen.networkUp = up;
                screen.network = address;
                Log("network: %s", address);
                dirty = true;
            }
        }
        if (dirty)
        {
            Draw(s, view, selected, screen);
            dirty = false;
        }
        else
            consoleUpdate(nullptr); // also paces the loop at the display's rate
    }

    if (!WriteText(kMenuConfigPath, Launcher_FormatSettings(s)))
        Log("could not write %s", kMenuConfigPath);
    if (result == LAUNCHER_START)
    {
        const std::string extra = ReadExtraArgs();
        std::string args = Launcher_BuildLaunchLine(s, extra);
        if (args.size() >= lineSize)
        {
            Log("launch_extra.cfg makes the line too long (%u bytes), started without it", unsigned(args.size()));
            args = Launcher_BuildLaunchLine(s, "");
        }
        std::snprintf(line, lineSize, "%s", args.c_str());
        std::snprintf(backend, backendSize, "%s", Launcher_BackendName(s.backend));
        if (!WriteText(kLaunchConfigPath, args + "\n"))
            Log("could not write %s", kLaunchConfigPath);
        if (!WriteText(kGfxConfigPath, std::string(backend) + "\n"))
            Log("could not write %s", kGfxConfigPath);
        const std::string what =
            s.join ? "le serveur " + Launcher_ServerAddress(s) : std::string(kLauncherMaps[s.map].label);
        Log("start: %s (extra \"%s\")", what.c_str(), extra.c_str());
        At(kStatusRow, 3);
        std::printf("\x1b[2K%s%s %s...%s", kColorSelected, s.join ? "Connexion a" : "Chargement de", what.c_str(),
                    kColorNormal);
        consoleUpdate(nullptr);
    }
    // Frees the console's framebuffer on the default window, which the renderer takes next. consoleExit leaves stdout
    // and stderr on the console, whose next character starts a frame on the closed framebuffer (framebufferBegin read
    // address 0 on Ryujinx at the first BootLog after Start): give them back the devices they had before consoleInit.
    consoleExit(nullptr);
    devoptab_list[STD_OUT] = stdoutDevice;
    devoptab_list[STD_ERR] = stderrDevice;
    return result;
}
