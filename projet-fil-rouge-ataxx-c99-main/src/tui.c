#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  Platform-specific includes                                        */
/* ================================================================== */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#endif

/* ================================================================== */
/*  Platform state                                                    */
/* ================================================================== */

#ifdef _WIN32
static HANDLE g_hStdin = INVALID_HANDLE_VALUE;
static HANDLE g_hStdout = INVALID_HANDLE_VALUE;
static DWORD g_old_stdin_mode;
static DWORD g_old_stdout_mode;
static UINT g_old_cp;
#else
static struct termios g_old_termios;
static int g_old_flags;
#endif

static bool g_tui_active = false;

/* ================================================================== */
/*  Internal helpers                                                  */
/* ================================================================== */

static void emit(const char *s)
{
  fputs(s, stdout);
}

static void emit_goto(int row, int col)
{
  printf("\033[%d;%dH", row + 1, col + 1);
}

static void emit_sgr(uint8_t fg, uint8_t bg, uint8_t attr)
{
  /* reset first, then apply attributes and colours */
  printf("\033[0");

  if (attr & TUI_ATTR_BOLD)
    printf(";1");
  if (attr & TUI_ATTR_DIM)
    printf(";2");
  if (attr & TUI_ATTR_UNDERLINE)
    printf(";4");
  if (attr & TUI_ATTR_REVERSE)
    printf(";7");

  /* foreground: 30-37 for 0-7, 90-97 for 8-15 */
  if (fg < 8)
    printf(";%d", 30 + fg);
  else
    printf(";%d", 90 + (fg - 8));

  /* background: 40-47 for 0-7, 100-107 for 8-15 */
  if (bg < 8)
    printf(";%d", 40 + bg);
  else
    printf(";%d", 100 + (bg - 8));

  printf("m");
}

/* ================================================================== */
/*  Terminal lifecycle                                                */
/* ================================================================== */

bool tui_init(void)
{
  if (g_tui_active)
    return true;

#ifdef _WIN32
  g_hStdin = GetStdHandle(STD_INPUT_HANDLE);
  g_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
  if (g_hStdin == INVALID_HANDLE_VALUE || g_hStdout == INVALID_HANDLE_VALUE)
  {
    return false;
  }

  /* save original modes */
  GetConsoleMode(g_hStdin, &g_old_stdin_mode);
  GetConsoleMode(g_hStdout, &g_old_stdout_mode);

  /* enable VT100 processing on stdout */
  SetConsoleMode(g_hStdout,
                 g_old_stdout_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);

  /* raw-ish input: no line buffering, no echo */
  SetConsoleMode(g_hStdin,
                 ENABLE_VIRTUAL_TERMINAL_INPUT);

  /* switch to UTF-8 codepage for box-drawing chars */
  g_old_cp = GetConsoleOutputCP();
  SetConsoleOutputCP(65001);
#else
  struct termios raw;

  if (tcgetattr(STDIN_FILENO, &g_old_termios) < 0)
  {
    return false;
  }

  raw = g_old_termios;
  raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG);
  raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
  {
    return false;
  }

  /* save and set non-blocking on stdin */
  g_old_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, g_old_flags | O_NONBLOCK);
#endif

  /* hide cursor, clear screen */
  emit("\033[?25l"); /* hide cursor    */
  emit("\033[2J");   /* clear screen   */
  emit("\033[H");    /* cursor to home */
  fflush(stdout);

  g_tui_active = true;
  return true;
}

void tui_shutdown(void)
{
  if (!g_tui_active)
    return;

  /* reset attributes, show cursor, clear screen */
  emit("\033[0m");
  emit("\033[2J");
  emit("\033[H");
  emit("\033[?25h"); /* show cursor */
  fflush(stdout);

#ifdef _WIN32
  SetConsoleMode(g_hStdin, g_old_stdin_mode);
  SetConsoleMode(g_hStdout, g_old_stdout_mode);
  SetConsoleOutputCP(g_old_cp);
#else
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_old_termios);
  fcntl(STDIN_FILENO, F_SETFL, g_old_flags);
#endif

  g_tui_active = false;
}

void tui_get_size(int *rows, int *cols)
{
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(g_hStdout, &csbi))
  {
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  }
  else
  {
    *rows = 24;
    *cols = 80;
  }
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
  {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
  }
  else
  {
    *rows = 24;
    *cols = 80;
  }
#endif
}

/* ================================================================== */
/*  Screen buffer                                                     */
/* ================================================================== */

TuiScreen *tui_screen_create(int rows, int cols)
{
  TuiScreen *scr;
  int total;

  if (rows <= 0 || cols <= 0)
    return NULL;

  scr = (TuiScreen *)calloc(1, sizeof(TuiScreen));
  if (!scr)
    return NULL;

  total = rows * cols;
  scr->rows = rows;
  scr->cols = cols;
  scr->cells = (TuiCell *)calloc((size_t)total, sizeof(TuiCell));
  scr->shadow = (TuiCell *)calloc((size_t)total, sizeof(TuiCell));
  scr->first_flush = true;

  if (!scr->cells || !scr->shadow)
  {
    free(scr->cells);
    free(scr->shadow);
    free(scr);
    return NULL;
  }

  return scr;
}

void tui_screen_destroy(TuiScreen *scr)
{
  if (!scr)
    return;
  free(scr->cells);
  free(scr->shadow);
  free(scr);
}

void tui_screen_clear(TuiScreen *scr, TuiCell fill)
{
  int total = scr->rows * scr->cols;
  int i;
  for (i = 0; i < total; ++i)
  {
    scr->cells[i] = fill;
  }
}

void tui_screen_put(TuiScreen *scr, int row, int col,
                    char ch, uint8_t fg, uint8_t bg, uint8_t attr)
{
  int idx;
  if (row < 0 || row >= scr->rows || col < 0 || col >= scr->cols)
    return;
  idx = row * scr->cols + col;
  scr->cells[idx].ch = ch;
  scr->cells[idx].fg = fg;
  scr->cells[idx].bg = bg;
  scr->cells[idx].attr = attr;
}

void tui_screen_print(TuiScreen *scr, int row, int col,
                      const char *text, uint8_t fg, uint8_t bg, uint8_t attr)
{
  while (*text && col < scr->cols)
  {
    tui_screen_put(scr, row, col, *text, fg, bg, attr);
    ++text;
    ++col;
  }
}

void tui_screen_box(TuiScreen *scr, int row, int col,
                    int h, int w, uint8_t fg, uint8_t bg)
{
  int r, c;

  if (h < 2 || w < 2)
    return;

  /* corners */
  tui_screen_put(scr, row, col, '+', fg, bg, TUI_ATTR_NONE);
  tui_screen_put(scr, row, col + w - 1, '+', fg, bg, TUI_ATTR_NONE);
  tui_screen_put(scr, row + h - 1, col, '+', fg, bg, TUI_ATTR_NONE);
  tui_screen_put(scr, row + h - 1, col + w - 1, '+', fg, bg, TUI_ATTR_NONE);

  /* horizontal edges */
  for (c = col + 1; c < col + w - 1; ++c)
  {
    tui_screen_put(scr, row, c, '-', fg, bg, TUI_ATTR_NONE);
    tui_screen_put(scr, row + h - 1, c, '-', fg, bg, TUI_ATTR_NONE);
  }

  /* vertical edges */
  for (r = row + 1; r < row + h - 1; ++r)
  {
    tui_screen_put(scr, r, col, '|', fg, bg, TUI_ATTR_NONE);
    tui_screen_put(scr, r, col + w - 1, '|', fg, bg, TUI_ATTR_NONE);
  }

  /* fill interior with spaces */
  for (r = row + 1; r < row + h - 1; ++r)
  {
    for (c = col + 1; c < col + w - 1; ++c)
    {
      tui_screen_put(scr, r, c, ' ', fg, bg, TUI_ATTR_NONE);
    }
  }
}

void tui_screen_flush(TuiScreen *scr)
{
  int r, c, idx;
  uint8_t cur_fg = 255, cur_bg = 255, cur_attr = 255;
  int cur_row = -1, cur_col = -1;

  for (r = 0; r < scr->rows; ++r)
  {
    for (c = 0; c < scr->cols; ++c)
    {
      idx = r * scr->cols + c;

      /* skip unchanged cells (unless first flush) */
      if (!scr->first_flush &&
          scr->cells[idx].ch == scr->shadow[idx].ch &&
          scr->cells[idx].fg == scr->shadow[idx].fg &&
          scr->cells[idx].bg == scr->shadow[idx].bg &&
          scr->cells[idx].attr == scr->shadow[idx].attr)
      {
        continue;
      }

      /* move cursor if not already positioned */
      if (r != cur_row || c != cur_col)
      {
        emit_goto(r, c);
      }

      /* change colours if needed */
      if (scr->cells[idx].fg != cur_fg ||
          scr->cells[idx].bg != cur_bg ||
          scr->cells[idx].attr != cur_attr)
      {
        emit_sgr(scr->cells[idx].fg,
                 scr->cells[idx].bg,
                 scr->cells[idx].attr);
        cur_fg = scr->cells[idx].fg;
        cur_bg = scr->cells[idx].bg;
        cur_attr = scr->cells[idx].attr;
      }

      /* emit character */
      {
        char buf[2] = {scr->cells[idx].ch ? scr->cells[idx].ch : ' ', '\0'};
        emit(buf);
      }

      cur_row = r;
      cur_col = c + 1;
    }
  }

  fflush(stdout);

  /* swap: copy cells into shadow */
  memcpy(scr->shadow, scr->cells,
         (size_t)(scr->rows * scr->cols) * sizeof(TuiCell));
  scr->first_flush = false;
}

/* ================================================================== */
/*  Input                                                             */
/* ================================================================== */

#ifdef _WIN32

int tui_poll_key(void)
{
  int ch;
  if (!_kbhit())
    return TUI_KEY_NONE;

  ch = _getch();

  if (ch == 0 || ch == 0xE0)
  {
    /* extended key: read the scan code */
    if (!_kbhit())
      return TUI_KEY_NONE;
    ch = _getch();
    switch (ch)
    {
    case 72:
      return TUI_KEY_UP;
    case 80:
      return TUI_KEY_DOWN;
    case 75:
      return TUI_KEY_LEFT;
    case 77:
      return TUI_KEY_RIGHT;
    default:
      return TUI_KEY_NONE;
    }
  }

  /* VT100 escape sequences (sent when ENABLE_VIRTUAL_TERMINAL_INPUT is on) */
  if (ch == 27)
  {
    if (!_kbhit())
      return TUI_KEY_ESCAPE; /* bare ESC with no follow-up */
    ch = _getch();
    if (ch == '[')
    {
      if (!_kbhit())
        return TUI_KEY_ESCAPE;
      ch = _getch();
      switch (ch)
      {
      case 'A':
        return TUI_KEY_UP;
      case 'B':
        return TUI_KEY_DOWN;
      case 'C':
        return TUI_KEY_RIGHT;
      case 'D':
        return TUI_KEY_LEFT;
      default:
        return TUI_KEY_NONE;
      }
    }
    return TUI_KEY_ESCAPE;
  }

  switch (ch)
  {
  case 13:
    return TUI_KEY_ENTER;
  case 32:
    return TUI_KEY_SPACE;
  case 8:
    return TUI_KEY_BACKSPACE;
  case 9:
    return TUI_KEY_TAB;
  }

  /* printable ASCII */
  if (ch >= 32 && ch < 127)
    return ch;

  return TUI_KEY_NONE;
}

#else /* POSIX */

int tui_poll_key(void)
{
  unsigned char buf[8];
  ssize_t n;

  n = read(STDIN_FILENO, buf, sizeof(buf));
  if (n <= 0)
    return TUI_KEY_NONE;

  /* ESC sequence */
  if (n >= 3 && buf[0] == '\033' && buf[1] == '[')
  {
    switch (buf[2])
    {
    case 'A':
      return TUI_KEY_UP;
    case 'B':
      return TUI_KEY_DOWN;
    case 'C':
      return TUI_KEY_RIGHT;
    case 'D':
      return TUI_KEY_LEFT;
    }
    return TUI_KEY_ESCAPE;
  }

  if (n == 1 && buf[0] == '\033')
    return TUI_KEY_ESCAPE;

  if (n >= 1)
  {
    switch (buf[0])
    {
    case '\r':
    case '\n':
      return TUI_KEY_ENTER;
    case ' ':
      return TUI_KEY_SPACE;
    case 127:
    case '\b':
      return TUI_KEY_BACKSPACE;
    case '\t':
      return TUI_KEY_TAB;
    }

    if (buf[0] >= 32 && buf[0] < 127)
      return (int)buf[0];
  }

  return TUI_KEY_NONE;
}

#endif

int tui_wait_key(void)
{
  int key;
  for (;;)
  {
    key = tui_poll_key();
    if (key != TUI_KEY_NONE)
      return key;
    tui_sleep_ms(10);
  }
}

/* ================================================================== */
/*  Utility                                                           */
/* ================================================================== */

void tui_sleep_ms(int ms)
{
  if (ms <= 0)
    return;
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
    {
      /* retry on interrupt */
    }
  }
#endif
}
