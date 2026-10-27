// The start menu of the Switch build. Map, aim assist, god mode, graphics and controllers are chosen with the pad on
// libnx's text console before the engine starts, so a game opens straight on its map without the front end. The
// choices persist in menu.cfg; Start writes launch.cfg and gfx.cfg from them (launcher_config.cpp), which a skip_menu
// boot (emulator, scripted runs) then reuses as they are. The console font is ASCII only: no accents in the text.

// libnx before anything that could pull the Win32 shim (switch_main.cpp explains why).
#include <switch.h>

#include "launcher_config.h"
#include "switch_launcher.h"

#include <sys/iosupport.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

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

enum Item
{
    ITEM_MAP,
    ITEM_AIMASSIST,
    ITEM_AIM_SLOWDOWN,
    ITEM_AIM_LOCKON,
    ITEM_AIM_AUTOAIM,
    ITEM_GOD,
    ITEM_BACKEND,
    ITEM_MAXFPS,
    ITEM_SHADOWS,
    ITEM_TEXFILTER,
    ITEM_FPSCOUNTER,
    ITEM_CONTROLLERS,
    ITEM_START,
    ITEM_COUNT
};

struct ItemInfo
{
    int row;
    const char *label;
    const char *help;
};

// Console rows (1-based, 45 of them at 1280x720).
const ItemInfo kItems[ITEM_COUNT] = {
    {6, "Map", "La map ou la partie commence directement, sans le menu du jeu."},
    {8, "Aim assist", "Aide a la visee a la manette sur les zombies."},
    {9, "  Ralentissement", "La visee ralentit quand le reticule passe sur un zombie."},
    {10, "  Verrouillage", "La visee suit le zombie vise."},
    {11, "  Visee auto", "En visant (ZL), le reticule saute sur le zombie le plus proche."},
    {13, "God mode", "Les zombies ne peuvent pas tuer le joueur."},
    {16, "Rendu", "deko3d: le plus rapide. OpenGL: l'ancien chemin, plus lent."},
    {17, "Limite de FPS", "30: plus stable et plus econome en batterie. 60: plus fluide."},
    {18, "Ombres", "Ombres dynamiques. Les couper fait gagner des FPS."},
    {19, "Filtre de texture", "Nettete des textures au loin. 16x: le plus net. Bilineaire: le plus leger."},
    {20, "Compteur FPS", "Compteur de FPS et de batterie en haut a droite."},
    {22, "Manettes", "A: ecran de la Switch pour connecter ou changer de manette."},
    {33, "DEMARRER", "Lance la partie avec ces reglages (aussi: bouton +)."},
};
const int kGraphicsHeaderRow = 15;
const int kControllerFirstRow = 23;
const int kControllerMaxRows = 9; // handheld + players 1-8
const int kHelpRow = 35;
const int kStatusRow = 36;
const int kFooterRow = 38;

bool Selectable(const LauncherSettings &s, int item)
{
    if (item == ITEM_AIM_SLOWDOWN || item == ITEM_AIM_LOCKON || item == ITEM_AIM_AUTOAIM)
        return s.aimAssist;
    return true;
}

const char *YesNo(bool v)
{
    return v ? "Oui" : "Non";
}

std::string ValueText(const LauncherSettings &s, int item)
{
    switch (item)
    {
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
    case ITEM_BACKEND:
        return s.backend == LAUNCHER_BACKEND_DEKO3D ? "deko3d" : "OpenGL";
    case ITEM_MAXFPS:
        return std::to_string(s.maxFps);
    case ITEM_SHADOWS:
        return YesNo(s.shadows);
    case ITEM_TEXFILTER:
        return Launcher_TexFilterLabel(s.texFilter);
    case ITEM_FPSCOUNTER:
        return YesNo(s.fpsCounter);
    default:
        return "";
    }
}

// Left/right (dir -1/+1) and A (+1) on a value line.
void Change(LauncherSettings *s, int item, int dir)
{
    switch (item)
    {
    case ITEM_MAP:
        s->map = (s->map + dir + kLauncherMapCount) % kLauncherMapCount;
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
    case ITEM_BACKEND:
        s->backend = s->backend == LAUNCHER_BACKEND_DEKO3D ? LAUNCHER_BACKEND_GL : LAUNCHER_BACKEND_DEKO3D;
        break;
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
    default:
        break;
    }
}

int Step(const LauncherSettings &s, int item, int dir)
{
    do
        item = (item + dir + ITEM_COUNT) % ITEM_COUNT;
    while (!Selectable(s, item));
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

void Draw(const LauncherSettings &s, int selected, const char *buildStamp, int battery, const std::string &controllers)
{
    std::printf("\x1b[2J");
    At(2, 3);
    std::printf("%sBLACK OPS ZOMBIES - LANCEUR%s", kColorTitle, kColorNormal);
    if (battery >= 0)
    {
        At(2, 64);
        std::printf("Batterie %3d%%", battery);
    }
    At(3, 3);
    std::printf("%sbuild %s%s", kColorDisabled, buildStamp, kColorNormal);
    At(4, 3);
    std::printf("----------------------------------------------------------------------------");
    At(kGraphicsHeaderRow, 3);
    std::printf("%sGraphismes%s", kColorTitle, kColorNormal);

    for (int item = 0; item < ITEM_COUNT; ++item)
    {
        const ItemInfo &info = kItems[item];
        const bool isSelected = item == selected;
        At(info.row, 3);
        if (item == ITEM_START)
        {
            std::printf("%s%s[ DEMARRER ]%s", isSelected ? "> " : "  ", isSelected ? "\x1b[30;43m" : kColorSelected,
                        kColorNormal);
            continue;
        }
        const char *color = isSelected ? kColorSelected : (Selectable(s, item) ? kColorNormal : kColorDisabled);
        std::printf("%s%s %-20s", color, isSelected ? ">" : " ", info.label);
        if (item == ITEM_CONTROLLERS)
            std::printf("[A] ecran des manettes%s", kColorNormal);
        else
            std::printf("<  %s  >%s", ValueText(s, item).c_str(), kColorNormal);
    }

    if (controllers.empty())
    {
        At(kControllerFirstRow, 7);
        std::printf("%sAucune manette detectee%s", kColorWarn, kColorNormal);
    }
    else
    {
        int row = kControllerFirstRow;
        size_t pos = 0;
        while (pos < controllers.size() && row < kControllerFirstRow + kControllerMaxRows)
        {
            const size_t end = controllers.find('\n', pos);
            At(row++, 7);
            std::printf("%s", controllers.substr(pos, end - pos).c_str());
            pos = end + 1;
        }
    }

    At(kHelpRow, 3);
    std::printf("%s", kItems[selected].help);
    At(kFooterRow, 3);
    std::printf("----------------------------------------------------------------------------");
    At(kFooterRow + 1, 3);
    std::printf("Haut/Bas: choisir  Gauche/Droite: modifier  A: valider  +: jouer  -: quitter");
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
} // namespace

LauncherResult Launcher_Run(const char *buildStamp, char *line, size_t lineSize, char *backend, size_t backendSize)
{
    LauncherSettings s = Launcher_ParseSettings(ReadText(kMenuConfigPath, 4096));
    Log("menu.cfg read: map %s, backend %s", kLauncherMaps[s.map].bsp, Launcher_BackendName(s.backend));
    // The first battery query logs through stdout (switch_main.cpp's BootLog), which the console would print over the
    // menu: make it before consoleInit.
    int battery = Switch_BatteryPercent();

    const devoptab_t *const stdoutDevice = devoptab_list[STD_OUT];
    const devoptab_t *const stderrDevice = devoptab_list[STD_ERR];
    consoleInit(nullptr);
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeAny(&pad);

    int selected = ITEM_START; // the last settings are one press away
    std::string controllers = ControllerList();
    LogControllers(controllers);
    bool dirty = true;
    u64 lastPoll = armGetSystemTick();
    Repeat vertical, horizontal;
    LauncherResult result = LAUNCHER_QUIT;
    bool start = false;

    // appletMainLoop must never be called again once it said false (switch-homebrew.md): leaving the loop on it is
    // final, and main then returns without the engine.
    while (appletMainLoop())
    {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        const u64 held = padGetButtons(&pad);
        const u64 now = armGetSystemTick();

        if (vertical.Fire(down & (HidNpadButton_AnyUp | HidNpadButton_AnyDown),
                          held & (HidNpadButton_AnyUp | HidNpadButton_AnyDown), now))
        {
            selected = Step(s, selected, (held & HidNpadButton_AnyUp) ? -1 : 1);
            dirty = true;
        }
        if (horizontal.Fire(down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight),
                            held & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight), now) &&
            selected != ITEM_CONTROLLERS && selected != ITEM_START)
        {
            Change(&s, selected, (held & HidNpadButton_AnyLeft) ? -1 : 1);
            dirty = true;
        }
        if (down & HidNpadButton_A)
        {
            if (selected == ITEM_START)
                start = true;
            else if (selected == ITEM_CONTROLLERS)
            {
                ShowControllerApplet();
                controllers = ControllerList();
                LogControllers(controllers);
            }
            else
                Change(&s, selected, 1);
            dirty = true;
        }
        if (down & HidNpadButton_Plus)
            start = true;
        if (start)
        {
            result = LAUNCHER_START;
            break;
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
            if (list != controllers)
            {
                controllers = list;
                LogControllers(controllers);
                dirty = true;
            }
            const int level = Switch_BatteryPercent();
            if (level != battery)
            {
                battery = level;
                dirty = true;
            }
        }
        if (dirty)
        {
            Draw(s, selected, buildStamp, battery, controllers);
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
        Log("start: %s (extra \"%s\")", kLauncherMaps[s.map].label, extra.c_str());
        At(kStatusRow, 3);
        std::printf("%sChargement de %s...%s", kColorSelected, kLauncherMaps[s.map].label, kColorNormal);
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
