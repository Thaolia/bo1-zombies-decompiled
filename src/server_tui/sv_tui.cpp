#include "sv_tui.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#undef MOUSE_MOVED // wincon.h's; PDCurses' curses.h defines its own
#else
#include <unistd.h>
#endif
#include <curses.h>

namespace
{
constexpr size_t kMaxLogLines = 5000;
constexpr size_t kMaxInput = 255;
constexpr size_t kMaxHistory = 100;

std::mutex s_logMutex; // ServerTui_Print runs on any thread, the drawing on the main one only
std::deque<std::string> s_log;
std::string s_partialLine;
bool s_logChanged;

bool s_active;
int s_scroll; // log lines above the newest one shown at the bottom
std::string s_input;
std::vector<std::string> s_history;
size_t s_historyPos;
int s_selected = -1; // selected player slot, -1 for none
std::string s_command;
std::chrono::steady_clock::time_point s_lastDraw;
bool s_dirty = true;
bool s_clientView; // Tab: the selected player's log instead of the server's
int s_clientScroll;
std::vector<std::string> s_clientLines;
std::string s_clientTitle;

// one log line per '\n'; the ^0..^@ colour codes are dropped (Conbuf_CleanText's rule)
void AppendLog(const char *text)
{
    for (const char *p = text; *p; ++p)
    {
        if (p[0] == '^' && p[1] && p[1] != '^' && p[1] >= '0' && p[1] <= '@')
        {
            ++p;
            continue;
        }
        if (*p == '\n')
        {
            s_log.push_back(s_partialLine);
            s_partialLine.clear();
            if (s_log.size() > kMaxLogLines)
                s_log.pop_front();
        }
        else if (*p != '\r')
        {
            s_partialLine += *p == '\t' ? ' ' : *p;
        }
    }
    s_logChanged = true;
}

const char *StateName(const ServerTuiPlayer &p)
{
    if (p.state == 5 && p.game[0])
        return p.game;
    switch (p.state)
    {
    case 3:
        return "connected";
    case 4:
        return "loading";
    case 5:
        return "active";
    default:
        return "leaving";
    }
}

const ServerTuiPlayer *FindPlayer(const ServerTuiStatus &status, int num)
{
    for (int i = 0; i < status.playerCount; ++i)
    {
        if (status.players[i].num == num)
            return &status.players[i];
    }
    return nullptr;
}

// F2 / F3: the previous / next connected slot, wrapping around
void SelectPlayer(const ServerTuiStatus &status, int step)
{
    if (!status.playerCount)
    {
        s_selected = -1;
        return;
    }
    int index = -1;
    for (int i = 0; i < status.playerCount; ++i)
    {
        if (status.players[i].num == s_selected)
            index = i;
    }
    index = index < 0 ? (step > 0 ? 0 : status.playerCount - 1) : (index + step + status.playerCount) % status.playerCount;
    s_selected = status.players[index].num;
}

// action keys only fill the command line; Enter runs it, so no key acts on a player by itself
void PrefillForSelected(const ServerTuiStatus &status, const char *command)
{
    // nothing selected yet: the first player, so a lone player needs no F2/F3 first
    if (!FindPlayer(status, s_selected) && status.playerCount > 0)
        s_selected = status.players[0].num;
    if (!FindPlayer(status, s_selected))
        return;
    char line[64];
    std::snprintf(line, sizeof(line), "%s %d", command, s_selected);
    s_input = line;
}

void HandleKey(int ch, const ServerTuiStatus &status, int logRows)
{
    switch (ch)
    {
    case '\n':
    case '\r':
    case KEY_ENTER:
        if (!s_input.empty())
        {
            if (s_history.empty() || s_history.back() != s_input)
                s_history.push_back(s_input);
            if (s_history.size() > kMaxHistory)
                s_history.erase(s_history.begin());
            s_historyPos = s_history.size();
            // as the Win32 console returns it: Sys_GetEvent copies strlen - 1 characters, dropping the newline
            s_command = s_input + "\n";
            s_input.clear();
            s_scroll = 0;
        }
        break;
    case KEY_BACKSPACE:
    case 127:
    case 8:
        if (!s_input.empty())
            s_input.pop_back();
        break;
    case 27: // Esc
        s_input.clear();
        break;
    case KEY_UP:
        if (s_historyPos > 0)
            s_input = s_history[--s_historyPos];
        break;
    case KEY_DOWN:
        if (s_historyPos + 1 < s_history.size())
            s_input = s_history[++s_historyPos];
        else
        {
            s_historyPos = s_history.size();
            s_input.clear();
        }
        break;
    case KEY_PPAGE:
        (s_clientView ? s_clientScroll : s_scroll) += logRows > 1 ? logRows - 1 : 1;
        break;
    case KEY_NPAGE:
    {
        int &scroll = s_clientView ? s_clientScroll : s_scroll;
        scroll -= logRows > 1 ? logRows - 1 : 1;
        if (scroll < 0)
            scroll = 0;
        break;
    }
    case KEY_END:
        (s_clientView ? s_clientScroll : s_scroll) = 0;
        break;
    case '\t':
        s_clientView = !s_clientView;
        s_clientScroll = 0;
        if (s_clientView && !FindPlayer(status, s_selected) && status.playerCount > 0)
            s_selected = status.players[0].num;
        break;
    case KEY_F(2):
        SelectPlayer(status, -1);
        break;
    case KEY_F(3):
        SelectPlayer(status, 1);
        break;
    case KEY_F(4):
        PrefillForSelected(status, "zm_god");
        break;
    case KEY_F(5):
        PrefillForSelected(status, "zm_revive");
        break;
    case KEY_F(6):
        PrefillForSelected(status, "zm_respawn");
        break;
    case KEY_F(7):
        PrefillForSelected(status, "zm_points");
        if (!s_input.empty())
            s_input += " ";
        break;
    case KEY_F(8):
        PrefillForSelected(status, "clientkick");
        break;
    case KEY_F(10):
        s_input = "zm_reset";
        break;
#if defined(KEY_RESIZE)
    case KEY_RESIZE:
#if defined(PDCURSES)
        resize_term(0, 0);
#endif
        break;
#endif
    default:
        if (ch >= 32 && ch < 127 && s_input.size() < kMaxInput)
            s_input += (char)ch;
        break;
    }
    s_dirty = true;
}

void DrawLine(int row, const char *text, int cols)
{
    move(row, 0);
    clrtoeol();
    mvaddnstr(row, 0, text, cols);
}

void Draw(const ServerTuiStatus &status)
{
    const int rows = LINES;
    const int cols = COLS;
    char line[512];
    erase();

    const int up = status.uptimeSeconds;
    std::snprintf(line, sizeof(line), " BO1 Zombies server | %s | %s | %d/%d players | port %d | up %d:%02d:%02d",
                  status.hostname, status.mapname[0] ? status.mapname : "-", status.playerCount, status.maxClients,
                  status.port, up / 3600, up / 60 % 60, up % 60);
    attron(A_REVERSE);
    mvhline(0, 0, ' ', cols);
    mvaddnstr(0, 0, line, cols);
    attroff(A_REVERSE);

    // the slots, at most a third of the screen so the log keeps room
    int slots = status.maxClients;
    if (slots > rows / 3)
        slots = rows / 3 > 1 ? rows / 3 : 1;
    std::snprintf(line, sizeof(line), "  %-3s %-24s %-22s %5s  %-10s %8s %s", "#", "name", "address", "ping", "state",
                  "points", "admin");
    attron(A_BOLD);
    DrawLine(1, line, cols);
    attroff(A_BOLD);
    for (int slot = 0; slot < slots; ++slot)
    {
        const ServerTuiPlayer *p = FindPlayer(status, slot);
        if (p)
            std::snprintf(line, sizeof(line), "%c %-3d %-24.24s %-22.22s %5d  %-10s %8d %s",
                          slot == s_selected ? '>' : ' ', slot, p->name, p->address, p->ping, StateName(*p), p->score,
                          p->admin);
        else
            std::snprintf(line, sizeof(line), "%c %-3d -", slot == s_selected ? '>' : ' ', slot);
        if (slot == s_selected)
            attron(A_REVERSE);
        DrawLine(2 + slot, line, cols);
        if (slot == s_selected)
            attroff(A_REVERSE);
    }

    const int sepRow = 2 + slots;
    const int logTop = sepRow + 1;
    const int logRows = rows - 2 - logTop;
    const ServerTuiPlayer *viewed = s_clientView ? FindPlayer(status, s_selected) : nullptr;
    if (s_clientView)
    {
        // read before s_logMutex: the server thread prints while it holds the client logs' lock
        s_clientLines.clear();
        if (!viewed)
            s_clientTitle = "client log: select a player (F2/F3)";
        else if (!ServerTui_ClientLog(*viewed, s_clientLines, s_clientTitle))
            s_clientTitle = std::string("client log: nothing received from ") + viewed->name + " yet";
        const int total = (int)s_clientLines.size();
        if (s_clientScroll > total - logRows)
            s_clientScroll = total - logRows > 0 ? total - logRows : 0;
        std::snprintf(line, sizeof(line), "-- %s%s (Tab: server log) ", s_clientTitle.c_str(),
                      s_clientScroll ? ", scrolled, End returns" : "");
        attron(A_BOLD);
        mvhline(sepRow, 0, '-', cols);
        mvaddnstr(sepRow, 0, line, cols);
        attroff(A_BOLD);
        const int first = total - s_clientScroll - logRows;
        for (int r = 0; r < logRows; ++r)
        {
            const int index = first + r;
            if (index >= 0 && index < total)
                mvaddnstr(logTop + r, 0, s_clientLines[index].c_str(), cols);
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        const int total = (int)s_log.size();
        if (s_scroll > total - logRows)
            s_scroll = total - logRows > 0 ? total - logRows : 0;
        std::snprintf(line, sizeof(line), "-- console%s(Tab: player's log) ", s_scroll ? " (scrolled, End returns) " : " ");
        attron(A_BOLD);
        mvhline(sepRow, 0, '-', cols);
        mvaddnstr(sepRow, 0, line, cols);
        attroff(A_BOLD);
        const int first = total - s_scroll - logRows;
        for (int r = 0; r < logRows; ++r)
        {
            const int index = first + r;
            if (index >= 0 && index < total)
                mvaddnstr(logTop + r, 0, s_log[index].c_str(), cols);
        }
        s_logChanged = false;
    }

    attron(A_REVERSE);
    mvhline(rows - 2, 0, ' ', cols);
    mvaddnstr(rows - 2, 0,
              " F2/F3 player  F4 god  F5 revive  F6 respawn  F7 points  F8 kick  F10 reset  Tab player's log  PgUp/PgDn log"
              "  Up/Down history  Esc clear  'quit' stops the server",
              cols);
    attroff(A_REVERSE);

    // the end of a command line longer than the screen stays visible
    const int room = cols - 3;
    const char *shown = s_input.c_str();
    if (room > 0 && (int)s_input.size() > room)
        shown += s_input.size() - room;
    DrawLine(rows - 1, "> ", cols);
    mvaddnstr(rows - 1, 2, shown, cols - 2);
    move(rows - 1, 2 + (int)std::strlen(shown) < cols ? 2 + (int)std::strlen(shown) : cols - 1);
    refresh();
}
} // namespace

bool ServerTui_Start()
{
    if (s_active)
        return true;
#if defined(_WIN32)
    // a server started from Explorer or a GUI shell has no console: the console gets its own window
    if (!GetConsoleWindow() && !AllocConsole())
        return false;
#else
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return false;
#endif
    if (!initscr())
        return false;
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    s_active = true;
    s_dirty = true;
    std::atexit(ServerTui_Stop);
    return true;
}

void ServerTui_Stop()
{
    if (!s_active)
        return;
    s_active = false;
    endwin();
}

bool ServerTui_Active()
{
    return s_active;
}

void ServerTui_Print(const char *text)
{
    std::lock_guard<std::mutex> lock(s_logMutex);
    AppendLog(text);
}

const char *ServerTui_Frame(const ServerTuiStatus &status)
{
    if (!s_active)
        return nullptr;
    s_command.clear();
    const int slots = status.maxClients < LINES / 3 ? status.maxClients : (LINES / 3 > 1 ? LINES / 3 : 1);
    const int logRows = LINES - 2 - (3 + slots);
    for (int ch = getch(); ch != ERR && s_command.empty(); ch = getch())
        HandleKey(ch, status, logRows);
    if (s_selected >= 0 && !FindPlayer(status, s_selected))
        s_selected = -1;

    const auto now = std::chrono::steady_clock::now();
    bool logChanged;
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        logChanged = s_logChanged;
    }
    // the client log view has no change signal: redraw it at the 10 Hz cap
    if (s_dirty || logChanged || s_clientView || now - s_lastDraw >= std::chrono::seconds(1))
    {
        if (now - s_lastDraw >= std::chrono::milliseconds(100) || s_dirty)
        {
            Draw(status);
            s_lastDraw = now;
            s_dirty = false;
        }
    }
    return s_command.empty() ? nullptr : s_command.c_str();
}
