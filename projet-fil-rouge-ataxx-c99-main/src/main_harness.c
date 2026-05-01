#include "agent.h"
#include "agent_loader.h"
#include "game.h"
#include "tui.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

/* ================================================================== */
/*  Constants                                                         */
/* ================================================================== */

#define MAX_PLUGINS 16
#define MAX_DISCOVERED 32
#define PLUGINS_DIR "plugins"
#define SPEED_MIN_MS 50
#define SPEED_MAX_MS 2000
#define SPEED_STEP_MS 50
#define DEFAULT_SPEED_MS 500
#define DEFAULT_MOVE_LIMIT 100

/* ── Built-in agent identifiers ───────────────────────────────────── */

#define AGENT_RANDOM 0
#define AGENT_PLUGIN_BASE 1 /* indices 1..1+MAX_PLUGINS-1 are plugins */

/* ================================================================== */
/*  Harness state                                                     */
/* ================================================================== */

typedef struct
{
  /* agents */
  AgentPlugin plugins[MAX_PLUGINS];
  int plugin_count;

  /* menu settings */
  int p1_agent; /* agent index for player 1 */
  int p2_agent; /* agent index for player 2 */
  int move_limit;
  int board_size;
  int menu_cursor; /* 0=P1, 1=P2, 2=limit, 3=size, 4=start */

  /* game playback */
  int speed_ms;
  bool step_mode;
  bool paused;

  /* screen */
  TuiScreen *scr;
} Harness;

/* ================================================================== */
/*  Forward declarations                                              */
/* ================================================================== */

static void harness_init(Harness *h);
static void harness_cleanup(Harness *h);
static const char *agent_name(const Harness *h, int idx);
static void format_agent_menu_name(const Harness *h, int idx,
                                   char *buf, size_t buf_size);
static Move agent_get_move(Harness *h, int agent_idx,
                           const GameState *state, AgentContext *ctx);
static bool screen_menu(Harness *h);
static void screen_game(Harness *h);
static bool screen_results(Harness *h, const GameState *state);
static void draw_board(Harness *h, const GameState *state,
                       int origin_row, int origin_col);
static void draw_centered(Harness *h, int row, const char *text,
                          uint8_t fg, uint8_t bg, uint8_t attr);
static bool select_agent_for_player(Harness *h, Player player,
                                    int *target_agent);

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void)
{
  Harness h;
  bool running;

  if (!tui_init())
  {
    fprintf(stderr, "Failed to initialise TUI.\n");
    return 1;
  }

  harness_init(&h);

  running = true;
  while (running)
  {
    if (!screen_menu(&h))
    {
      running = false;
      break;
    }
    screen_game(&h);
    /* screen_results returns true if user wants to replay / new game */
  }

  harness_cleanup(&h);
  tui_shutdown();
  return 0;
}

/* ================================================================== */
/*  Harness lifecycle                                                 */
/* ================================================================== */

static void harness_init(Harness *h)
{
  int rows, cols;
  memset(h, 0, sizeof(*h));
  h->p1_agent = AGENT_RANDOM;
  h->p2_agent = AGENT_RANDOM;
  h->move_limit = DEFAULT_MOVE_LIMIT;
  h->board_size = ATAXX_BOARD_SIZE;
  h->speed_ms = DEFAULT_SPEED_MS;
  h->step_mode = false;
  h->paused = false;
  h->menu_cursor = 0;

  tui_get_size(&rows, &cols);
  h->scr = tui_screen_create(rows, cols);
}

static void harness_cleanup(Harness *h)
{
  int i;
  for (i = 0; i < h->plugin_count; ++i)
  {
    agent_plugin_unload(&h->plugins[i]);
  }
  tui_screen_destroy(h->scr);
}

static const char *agent_name(const Harness *h, int idx)
{
  if (idx == AGENT_RANDOM)
    return "Random";
  if (idx >= AGENT_PLUGIN_BASE &&
      idx < AGENT_PLUGIN_BASE + h->plugin_count)
  {
    return h->plugins[idx - AGENT_PLUGIN_BASE].name;
  }
  return "???";
}

static void format_agent_menu_name(const Harness *h, int idx,
                                   char *buf, size_t buf_size)
{
  if (idx == AGENT_RANDOM)
  {
    snprintf(buf, buf_size, "Random [built-in]");
    return;
  }
  snprintf(buf, buf_size, "%s [plugin]", agent_name(h, idx));
}

static Move agent_get_move(Harness *h, int agent_idx,
                           const GameState *state, AgentContext *ctx)
{
  if (agent_idx == AGENT_RANDOM)
  {
    return agent_random_choose_move(state);
  }
  if (agent_idx >= AGENT_PLUGIN_BASE &&
      agent_idx < AGENT_PLUGIN_BASE + h->plugin_count)
  {
    AgentPlugin *p = &h->plugins[agent_idx - AGENT_PLUGIN_BASE];
    return p->choose_move(state, ctx);
  }
  /* fallback: pass */
  {
    Move pass = {0, 0, 0, 0, true};
    return pass;
  }
}

/* ================================================================== */
/*  Drawing helpers                                                   */
/* ================================================================== */

static void clear_screen(Harness *h)
{
  TuiCell blank = {' ', TUI_WHITE, TUI_BLACK, TUI_ATTR_NONE};
  tui_screen_clear(h->scr, blank);
}

static void draw_centered(Harness *h, int row, const char *text,
                          uint8_t fg, uint8_t bg, uint8_t attr)
{
  int len = (int)strlen(text);
  int col = (h->scr->cols - len) / 2;
  if (col < 0)
    col = 0;
  tui_screen_print(h->scr, row, col, text, fg, bg, attr);
}

static void draw_board(Harness *h, const GameState *state,
                       int origin_row, int origin_col)
{
  int r, c;
  int box_h = state->board_size + 2;
  int box_w = state->board_size * 2 + 1;

  tui_screen_box(h->scr, origin_row, origin_col,
                 box_h, box_w, TUI_CYAN, TUI_BLACK);

  for (r = 0; r < state->board_size; ++r)
  {
    for (c = 0; c < state->board_size; ++c)
    {
      char ch = '.';
      uint8_t fg = TUI_WHITE;
      if (state->board[r][c] == PLAYER_ONE)
      {
        ch = 'X';
        fg = TUI_BRIGHT_GREEN;
      }
      else if (state->board[r][c] == PLAYER_TWO)
      {
        ch = 'O';
        fg = TUI_BRIGHT_RED;
      }
      tui_screen_put(h->scr,
                     origin_row + 1 + r,
                     origin_col + 1 + c * 2,
                     ch, fg, TUI_BLACK, TUI_ATTR_BOLD);
    }
  }
}

/* ================================================================== */
/*  Menu screen                                                       */
/* ================================================================== */

#define MENU_ITEMS 5 /* P1, P2, Limit, Size, Start */
#define MENU_ITEM_WIDTH 44

static void draw_menu_item(Harness *h, int row, int col,
                           const char *text, uint8_t fg, bool selected)
{
  char line[MENU_ITEM_WIDTH + 1];

  snprintf(line, sizeof(line), "%-*.*s",
           MENU_ITEM_WIDTH, MENU_ITEM_WIDTH, text);
  tui_screen_print(h->scr, row, col, line,
                   fg,
                   TUI_BLACK,
                   selected ? (TUI_ATTR_BOLD | TUI_ATTR_REVERSE)
                            : TUI_ATTR_NONE);
}

static void draw_menu(Harness *h)
{
  char buf[128];
  char agent_buf[64];
  int base_row;
  int left_col;
  int box_w = 50;
  int box_h = 12;
  int r;

  clear_screen(h);

  base_row = (h->scr->rows - box_h) / 2;
  left_col = (h->scr->cols - box_w) / 2;
  if (base_row < 0)
    base_row = 0;
  if (left_col < 0)
    left_col = 0;

  tui_screen_box(h->scr, base_row, left_col, box_h, box_w,
                 TUI_CYAN, TUI_BLACK);

  draw_centered(h, base_row + 1, "ATAXX - Tournament Arena",
                TUI_BRIGHT_YELLOW, TUI_BLACK, TUI_ATTR_BOLD);

  r = base_row + 3;

  /* Player 1 */
  format_agent_menu_name(h, h->p1_agent, agent_buf, sizeof(agent_buf));
  snprintf(buf, sizeof(buf), "  Player 1 agent: %s", agent_buf);
  draw_menu_item(h, r, left_col + 2, buf,
                 h->menu_cursor == 0 ? TUI_BRIGHT_WHITE : TUI_WHITE,
                 h->menu_cursor == 0);
  ++r;

  /* Player 2 */
  format_agent_menu_name(h, h->p2_agent, agent_buf, sizeof(agent_buf));
  snprintf(buf, sizeof(buf), "  Player 2 agent: %s", agent_buf);
  draw_menu_item(h, r, left_col + 2, buf,
                 h->menu_cursor == 1 ? TUI_BRIGHT_WHITE : TUI_WHITE,
                 h->menu_cursor == 1);
  ++r;

  /* Move limit */
  snprintf(buf, sizeof(buf), "  Move limit:< %-5d       >", h->move_limit);
  draw_menu_item(h, r, left_col + 2, buf,
                 h->menu_cursor == 2 ? TUI_BRIGHT_WHITE : TUI_WHITE,
                 h->menu_cursor == 2);
  ++r;

  /* Board size */
  snprintf(buf, sizeof(buf), "  Board size:< %-5d       >", h->board_size);
  draw_menu_item(h, r, left_col + 2, buf,
                 h->menu_cursor == 3 ? TUI_BRIGHT_WHITE : TUI_WHITE,
                 h->menu_cursor == 3);
  ++r;

  /* Start */
  draw_menu_item(h, r, left_col + 2,
                 "  [ENTER] Start game",
                 h->menu_cursor == 4 ? TUI_BRIGHT_GREEN : TUI_GREEN,
                 h->menu_cursor == 4);
  ++r;
  ++r;

  if (h->menu_cursor <= 1)
  {
    tui_screen_print(h->scr, r, left_col + 2,
                     "  [ENTER] Open plugin selection",
                     TUI_BRIGHT_CYAN, TUI_BLACK, TUI_ATTR_NONE);
  }
  else if (h->menu_cursor >= 2 && h->menu_cursor <= 3)
  {
    tui_screen_print(h->scr, r, left_col + 2,
                     "  Use [LEFT]/[RIGHT] arrows to adjust",
                     TUI_BRIGHT_CYAN, TUI_BLACK, TUI_ATTR_NONE);
  }
  ++r;

  tui_screen_print(h->scr, r, left_col + 2,
                   "  [ESC] Quit",
                   TUI_BRIGHT_CYAN, TUI_BLACK, TUI_ATTR_NONE);

  if (h->plugin_count > 0)
  {
    snprintf(buf, sizeof(buf), "  Loaded plugins: %d", h->plugin_count);
    tui_screen_print(h->scr, base_row + box_h, left_col + 2, buf,
                     TUI_BRIGHT_CYAN, TUI_BLACK, TUI_ATTR_NONE);
  }

  tui_screen_flush(h->scr);
}

static bool screen_menu(Harness *h)
{
  for (;;)
  {
    int key;
    draw_menu(h);
    key = tui_wait_key();

    switch (key)
    {
    case TUI_KEY_ESCAPE:
      return false;

    case TUI_KEY_UP:
      h->menu_cursor = (h->menu_cursor - 1 + MENU_ITEMS) % MENU_ITEMS;
      break;

    case TUI_KEY_DOWN:
      h->menu_cursor = (h->menu_cursor + 1) % MENU_ITEMS;
      break;

    case TUI_KEY_LEFT:
      if (h->menu_cursor == 2 && h->move_limit > 10)
        h->move_limit -= 10;
      else if (h->menu_cursor == 3 && h->board_size > 3)
        h->board_size -= 2;
      break;

    case TUI_KEY_RIGHT:
      if (h->menu_cursor == 2 && h->move_limit < 500)
        h->move_limit += 10;
      else if (h->menu_cursor == 3 && h->board_size < ATAXX_MAX_BOARD_SIZE)
        h->board_size += 2;
      break;

    case TUI_KEY_ENTER:
      if (h->menu_cursor == 0)
      {
        select_agent_for_player(h, PLAYER_ONE, &h->p1_agent);
      }
      else if (h->menu_cursor == 1)
      {
        select_agent_for_player(h, PLAYER_TWO, &h->p2_agent);
      }
      else if (h->menu_cursor == 4)
      {
        return true; /* start game */
      }
      break;

    default:
      break;
    }
  }
}

/* ================================================================== */
/*  Plugin directory scanning                                         */
/* ================================================================== */

typedef struct
{
  char paths[MAX_DISCOVERED][280];
  char names[MAX_DISCOVERED][64];
  int count;
} PluginList;

static void extract_display_name(const char *filename, char *buf, size_t bsz)
{
  const char *dot = strrchr(filename, '.');
  size_t len = dot ? (size_t)(dot - filename) : strlen(filename);
  if (len >= bsz)
    len = bsz - 1;
  memcpy(buf, filename, len);
  buf[len] = '\0';
}

static void discover_plugins(PluginList *list)
{
  list->count = 0;

#ifdef _WIN32
  {
    WIN32_FIND_DATAA fd;
    HANDLE hFind;
    char pattern[280];
    snprintf(pattern, sizeof(pattern), "%s\\*.dll", PLUGINS_DIR);
    hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
      return;
    do
    {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        continue;
      if (list->count >= MAX_DISCOVERED)
        break;
      snprintf(list->paths[list->count], sizeof(list->paths[0]),
               "%s\\%s", PLUGINS_DIR, fd.cFileName);
      extract_display_name(fd.cFileName, list->names[list->count], 64);
      ++list->count;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
  }
#else
  {
    DIR *dir = opendir(PLUGINS_DIR);
    struct dirent *ent;
    if (!dir)
      return;
    while ((ent = readdir(dir)) != NULL && list->count < MAX_DISCOVERED)
    {
      size_t nlen = strlen(ent->d_name);
      if (nlen <= 3 || strcmp(ent->d_name + nlen - 3, ".so") != 0)
        continue;
      snprintf(list->paths[list->count], sizeof(list->paths[0]),
               "%s/%s", PLUGINS_DIR, ent->d_name);
      extract_display_name(ent->d_name, list->names[list->count], 64);
      ++list->count;
    }
    closedir(dir);
  }
#endif
}

static int loaded_plugin_agent_index(const Harness *h, const char *name)
{
  int i;
  for (i = 0; i < h->plugin_count; ++i)
  {
    if (strcmp(h->plugins[i].name, name) == 0)
      return AGENT_PLUGIN_BASE + i;
  }
  return -1;
}

static int picker_cursor_from_agent(const Harness *h, const PluginList *list,
                                    int agent_idx)
{
  int i;

  if (agent_idx < AGENT_PLUGIN_BASE ||
      agent_idx >= AGENT_PLUGIN_BASE + h->plugin_count)
    return -1;

  for (i = 0; i < list->count; ++i)
  {
    if (strcmp(h->plugins[agent_idx - AGENT_PLUGIN_BASE].name,
               list->names[i]) == 0)
      return i;
  }

  return -1;
}

/* ================================================================== */
/*  Strategy selection                                                 */
/* ================================================================== */

static bool select_agent_for_player(Harness *h, Player player,
                                    int *target_agent)
{
  PluginList list;
  int cursor;
  int scroll = 0;
  int item_count;
  char buf[128];
  char title[64];
  char status[160] = "";
  uint8_t status_fg = TUI_BRIGHT_BLACK;
  int current_cursor;

  discover_plugins(&list);
  item_count = list.count;
  if (item_count == 0)
  {
    clear_screen(h);
    tui_screen_box(h->scr, (h->scr->rows - 7) / 2, (h->scr->cols - 50) / 2,
                   7, 50, TUI_CYAN, TUI_BLACK);
    draw_centered(h, (h->scr->rows - 7) / 2 + 1,
                  "No plugins found in plugins/",
                  TUI_BRIGHT_YELLOW, TUI_BLACK, TUI_ATTR_BOLD);
    draw_centered(h, (h->scr->rows - 7) / 2 + 3,
                  "Build or copy a .dll/.so into plugins/ first.",
                  TUI_WHITE, TUI_BLACK, TUI_ATTR_NONE);
    draw_centered(h, (h->scr->rows - 7) / 2 + 5,
                  "Press any key to return",
                  TUI_BRIGHT_CYAN, TUI_BLACK, TUI_ATTR_NONE);
    tui_screen_flush(h->scr);
    tui_wait_key();
    return false;
  }
  current_cursor = picker_cursor_from_agent(h, &list, *target_agent);
  cursor = current_cursor >= 0 ? current_cursor : 0;

  snprintf(title, sizeof(title), "Select Plugin for Player %d (%c)",
           player == PLAYER_ONE ? 1 : 2,
           player == PLAYER_ONE ? 'X' : 'O');

  for (;;)
  {
    int key;
    int visible_items;
    int box_h;
    int box_w = 52;
    int base_row;
    int left_col;
    int items_row;
    int i;

    visible_items = item_count;
    if (visible_items > h->scr->rows - 8)
      visible_items = h->scr->rows - 8;

    if (cursor < scroll)
      scroll = cursor;
    if (cursor >= scroll + visible_items)
      scroll = cursor - visible_items + 1;

    box_h = visible_items + 6;
    base_row = (h->scr->rows - box_h) / 2;
    left_col = (h->scr->cols - box_w) / 2;
    if (base_row < 0)
      base_row = 0;
    if (left_col < 0)
      left_col = 0;

    clear_screen(h);
    tui_screen_box(h->scr, base_row, left_col, box_h, box_w,
                   TUI_CYAN, TUI_BLACK);

    draw_centered(h, base_row + 1, title,
                  TUI_BRIGHT_YELLOW, TUI_BLACK, TUI_ATTR_BOLD);

    items_row = base_row + 3;
    for (i = 0; i < visible_items; ++i)
    {
      int item_idx = scroll + i;
      bool selected = (item_idx == cursor);
      bool current = (current_cursor >= 0 && item_idx == current_cursor);
      uint8_t fg = selected ? TUI_BRIGHT_WHITE : TUI_WHITE;
      uint8_t attr = selected ? (uint8_t)(TUI_ATTR_BOLD | TUI_ATTR_REVERSE)
                              : TUI_ATTR_NONE;

      int loaded_idx = loaded_plugin_agent_index(h, list.names[item_idx]);
      const char *suffix = "";

      if (current)
        suffix = " [current]";
      else if (loaded_idx >= 0)
        suffix = " [loaded]";

      snprintf(buf, sizeof(buf), "  %-36s%s",
               list.names[item_idx], suffix);

      tui_screen_print(h->scr, items_row + i, left_col + 2, buf,
                       fg, TUI_BLACK, attr);
    }

    if (item_count > visible_items)
    {
      snprintf(buf, sizeof(buf), "  Items %d-%d / %d",
               scroll + 1, scroll + visible_items, item_count);
      tui_screen_print(h->scr, base_row + box_h - 3, left_col + 2, buf,
                       TUI_BRIGHT_CYAN, TUI_BLACK, TUI_ATTR_NONE);
    }
    else if (status[0] != '\0')
    {
      tui_screen_print(h->scr, base_row + box_h - 3, left_col + 2, status,
                       status_fg, TUI_BLACK, TUI_ATTR_NONE);
    }

    tui_screen_print(h->scr, base_row + box_h - 2, left_col + 2,
                     "  [ENTER] Load plugin  [ESC] Back",
                     TUI_BRIGHT_CYAN, TUI_BLACK, TUI_ATTR_NONE);

    tui_screen_flush(h->scr);

    key = tui_wait_key();

    if (key == TUI_KEY_ESCAPE)
      return false;

    if (key == TUI_KEY_UP)
      cursor = (cursor - 1 + item_count) % item_count;
    else if (key == TUI_KEY_DOWN)
      cursor = (cursor + 1) % item_count;
    else if (key == TUI_KEY_ENTER)
    {
      {
        int loaded_idx = loaded_plugin_agent_index(h, list.names[cursor]);

        if (loaded_idx >= 0)
        {
          *target_agent = loaded_idx;
          return true;
        }
        if (h->plugin_count >= MAX_PLUGINS)
        {
          snprintf(status, sizeof(status), "Maximum plugins loaded.");
          status_fg = TUI_BRIGHT_RED;
          continue;
        }

        if (agent_plugin_load(&h->plugins[h->plugin_count],
                              list.paths[cursor]))
        {
          *target_agent = AGENT_PLUGIN_BASE + h->plugin_count;
          ++h->plugin_count;
          return true;
        }

        snprintf(status, sizeof(status), "Load failed: %.140s",
                 agent_plugin_last_error());
        status_fg = TUI_BRIGHT_RED;
      }
    }
  }
}

/* ================================================================== */
/*  Game screen                                                       */
/* ================================================================== */

static void draw_game(Harness *h, const GameState *state,
                      const char *last_move_desc)
{
  char buf[128];
  int board_row, board_col;
  int info_col;
  int r;

  clear_screen(h);

  /* title bar */
  snprintf(buf, sizeof(buf),
           " Tour %d  -  Joueur %s (%c)",
           state->turn_count + 1,
           state->current_player == PLAYER_ONE ? agent_name(h, h->p1_agent)
                                               : agent_name(h, h->p2_agent),
           state->current_player == PLAYER_ONE ? 'X' : 'O');
  tui_screen_print(h->scr, 0, 0, buf,
                   TUI_BRIGHT_YELLOW, TUI_BLACK, TUI_ATTR_BOLD);

  /* controls line */
  tui_screen_print(h->scr, 0, h->scr->cols - 38,
                   "[SPC]Step [P]ause [+][-]Speed [Q]uit",
                   TUI_BRIGHT_BLACK, TUI_BLACK, TUI_ATTR_NONE);

  /* board */
  board_row = 2;
  board_col = 2;
  draw_board(h, state, board_row, board_col);

  /* info panel */
  info_col = board_col + state->board_size * 2 + 4;
  r = board_row;

  tui_screen_print(h->scr, r, info_col, "Scores",
                   TUI_BRIGHT_WHITE, TUI_BLACK, TUI_ATTR_BOLD);
  ++r;

  snprintf(buf, sizeof(buf), "  X (%s): %d",
           agent_name(h, h->p1_agent),
           game_score(state, PLAYER_ONE));
  tui_screen_print(h->scr, r, info_col, buf,
                   TUI_BRIGHT_GREEN, TUI_BLACK, TUI_ATTR_NONE);
  ++r;

  snprintf(buf, sizeof(buf), "  O (%s): %d",
           agent_name(h, h->p2_agent),
           game_score(state, PLAYER_TWO));
  tui_screen_print(h->scr, r, info_col, buf,
                   TUI_BRIGHT_RED, TUI_BLACK, TUI_ATTR_NONE);
  r += 2;

  snprintf(buf, sizeof(buf), "Speed: %dms", h->speed_ms);
  tui_screen_print(h->scr, r, info_col, buf,
                   TUI_WHITE, TUI_BLACK, TUI_ATTR_NONE);
  ++r;

  snprintf(buf, sizeof(buf), "Mode:  %s",
           h->step_mode ? "Step" : (h->paused ? "Paused" : "Auto"));
  tui_screen_print(h->scr, r, info_col, buf,
                   TUI_WHITE, TUI_BLACK, TUI_ATTR_NONE);
  r += 2;

  if (last_move_desc[0])
  {
    tui_screen_print(h->scr, r, info_col, last_move_desc,
                     TUI_BRIGHT_CYAN, TUI_BLACK, TUI_ATTR_NONE);
  }

  tui_screen_flush(h->scr);
}

static void format_move(char *buf, size_t buf_size, const Move *m)
{
  if (m->is_pass)
  {
    snprintf(buf, buf_size, "Last: PASS");
  }
  else
  {
    int dist = m->from_row - m->to_row;
    if (dist < 0)
      dist = -dist;
    {
      int dc = m->from_col - m->to_col;
      if (dc < 0)
        dc = -dc;
      if (dc > dist)
        dist = dc;
    }
    snprintf(buf, buf_size, "Last: (%d,%d)->(%d,%d) [%s]",
             m->from_row, m->from_col,
             m->to_row, m->to_col,
             dist <= 1 ? "clone" : "jump");
  }
}

static void screen_game(Harness *h)
{
  GameState state;
  AgentContext ctx = {0};
  char last_move_desc[128] = "";
  bool quit = false;

  game_init(&state, h->board_size);
  h->paused = false;
  h->step_mode = false;

  while (!game_is_terminal(&state) &&
         state.turn_count < h->move_limit && !quit)
  {

    Move move;
    int agent_idx;
    bool advance = false;

    draw_game(h, &state, last_move_desc);

    /* ── Wait or step logic ───────────────────────────────── */
    if (h->step_mode)
    {
      /* wait for SPACE to advance */
      for (;;)
      {
        int key = tui_wait_key();
        if (key == TUI_KEY_SPACE)
        {
          advance = true;
          break;
        }
        if (key == 'q' || key == 'Q')
        {
          quit = true;
          break;
        }
        if (key == 'p' || key == 'P')
        {
          h->step_mode = false;
          break;
        }
        if (key == '+' || key == '=')
        {
          if (h->speed_ms > SPEED_MIN_MS)
            h->speed_ms -= SPEED_STEP_MS;
          draw_game(h, &state, last_move_desc);
        }
        if (key == '-')
        {
          if (h->speed_ms < SPEED_MAX_MS)
            h->speed_ms += SPEED_STEP_MS;
          draw_game(h, &state, last_move_desc);
        }
      }
      if (quit)
        break;
      if (!advance && !h->step_mode)
      {
        /* switched to auto mode, fall through */
      }
      else if (!advance)
      {
        continue;
      }
    }

    if (!h->step_mode)
    {
      /* auto mode: delay, poll for input during delay */
      int elapsed = 0;
      while (elapsed < h->speed_ms)
      {
        int key = tui_poll_key();
        if (key == 'q' || key == 'Q')
        {
          quit = true;
          break;
        }
        if (key == 'p' || key == 'P')
        {
          h->paused = !h->paused;
          draw_game(h, &state, last_move_desc);
        }
        if (key == TUI_KEY_SPACE)
        {
          h->step_mode = true;
          break;
        }
        if (key == '+' || key == '=')
        {
          if (h->speed_ms > SPEED_MIN_MS)
            h->speed_ms -= SPEED_STEP_MS;
          draw_game(h, &state, last_move_desc);
        }
        if (key == '-')
        {
          if (h->speed_ms < SPEED_MAX_MS)
            h->speed_ms += SPEED_STEP_MS;
          draw_game(h, &state, last_move_desc);
        }
        if (h->paused)
        {
          tui_sleep_ms(50);
          continue;
        }
        tui_sleep_ms(50);
        elapsed += 50;
      }
      if (quit || h->step_mode)
        continue;
    }

    /* ── Get move from the current player's agent ──────── */
    agent_idx = (state.current_player == PLAYER_ONE) ? h->p1_agent
                                                     : h->p2_agent;
    move = agent_get_move(h, agent_idx, &state, &ctx);
    format_move(last_move_desc, sizeof(last_move_desc), &move);

    if (!game_apply_move(&state, move))
    {
      snprintf(last_move_desc, sizeof(last_move_desc),
               "INVALID MOVE by %s!",
               agent_name(h, agent_idx));
      draw_game(h, &state, last_move_desc);
      tui_sleep_ms(2000);
      break;
    }
  }

  /* ── Results ──────────────────────────────────────────────── */
  if (!quit)
  {
    screen_results(h, &state);
  }
}

/* ================================================================== */
/*  Results screen                                                    */
/* ================================================================== */

static bool screen_results(Harness *h, const GameState *state)
{
  char buf[128];
  int s1, s2;
  int box_h = 12;
  int box_w = 40;
  int base_row, left_col, r;

  s1 = game_score(state, PLAYER_ONE);
  s2 = game_score(state, PLAYER_TWO);

  base_row = (h->scr->rows - box_h) / 2;
  left_col = (h->scr->cols - box_w) / 2;
  if (base_row < 0)
    base_row = 0;
  if (left_col < 0)
    left_col = 0;

  clear_screen(h);
  tui_screen_box(h->scr, base_row, left_col, box_h, box_w,
                 TUI_CYAN, TUI_BLACK);

  r = base_row + 1;
  draw_centered(h, r, "Game Over!",
                TUI_BRIGHT_YELLOW, TUI_BLACK, TUI_ATTR_BOLD);
  r += 2;

  snprintf(buf, sizeof(buf), "  X (%s): %d", agent_name(h, h->p1_agent), s1);
  tui_screen_print(h->scr, r, left_col + 2, buf,
                   TUI_BRIGHT_GREEN, TUI_BLACK, TUI_ATTR_NONE);
  ++r;

  snprintf(buf, sizeof(buf), "  O (%s): %d", agent_name(h, h->p2_agent), s2);
  tui_screen_print(h->scr, r, left_col + 2, buf,
                   TUI_BRIGHT_RED, TUI_BLACK, TUI_ATTR_NONE);
  r += 2;

  if (s1 > s2)
  {
    snprintf(buf, sizeof(buf), "  Winner: X (%s)!", agent_name(h, h->p1_agent));
    tui_screen_print(h->scr, r, left_col + 2, buf,
                     TUI_BRIGHT_GREEN, TUI_BLACK, TUI_ATTR_BOLD);
  }
  else if (s2 > s1)
  {
    snprintf(buf, sizeof(buf), "  Winner: O (%s)!", agent_name(h, h->p2_agent));
    tui_screen_print(h->scr, r, left_col + 2, buf,
                     TUI_BRIGHT_RED, TUI_BLACK, TUI_ATTR_BOLD);
  }
  else
  {
    tui_screen_print(h->scr, r, left_col + 2, "  Draw!",
                     TUI_BRIGHT_YELLOW, TUI_BLACK, TUI_ATTR_BOLD);
  }
  r += 2;

  tui_screen_print(h->scr, r, left_col + 2,
                   "  [M] Menu   [ESC] Quit",
                   TUI_WHITE, TUI_BLACK, TUI_ATTR_NONE);

  tui_screen_flush(h->scr);

  for (;;)
  {
    int key = tui_wait_key();
    if (key == 'm' || key == 'M' || key == TUI_KEY_ENTER)
      return true;
    if (key == TUI_KEY_ESCAPE || key == 'q' || key == 'Q')
      return false;
  }
}
