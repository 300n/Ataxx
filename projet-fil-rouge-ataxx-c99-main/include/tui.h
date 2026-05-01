#ifndef TUI_H
#define TUI_H

#include <stdint.h>
#include <stdbool.h>

/* ── Colour constants (indices into 8-colour SGR palette) ─────────── */

#define TUI_BLACK 0
#define TUI_RED 1
#define TUI_GREEN 2
#define TUI_YELLOW 3
#define TUI_BLUE 4
#define TUI_MAGENTA 5
#define TUI_CYAN 6
#define TUI_WHITE 7

#define TUI_BRIGHT_BLACK 8
#define TUI_BRIGHT_RED 9
#define TUI_BRIGHT_GREEN 10
#define TUI_BRIGHT_YELLOW 11
#define TUI_BRIGHT_BLUE 12
#define TUI_BRIGHT_MAGENTA 13
#define TUI_BRIGHT_CYAN 14
#define TUI_BRIGHT_WHITE 15

#define TUI_DEFAULT_FG TUI_WHITE
#define TUI_DEFAULT_BG TUI_BLACK

/* ── Attribute flags (combinable with |) ──────────────────────────── */

#define TUI_ATTR_NONE 0
#define TUI_ATTR_BOLD (1 << 0)
#define TUI_ATTR_DIM (1 << 1)
#define TUI_ATTR_UNDERLINE (1 << 2)
#define TUI_ATTR_REVERSE (1 << 3)

/* ── Key constants ────────────────────────────────────────────────── */

#define TUI_KEY_NONE 0
#define TUI_KEY_UP (-1)
#define TUI_KEY_DOWN (-2)
#define TUI_KEY_LEFT (-3)
#define TUI_KEY_RIGHT (-4)
#define TUI_KEY_ENTER (-5)
#define TUI_KEY_ESCAPE (-6)
#define TUI_KEY_SPACE (-7)
#define TUI_KEY_BACKSPACE (-8)
#define TUI_KEY_TAB (-9)

/* ── Cell & screen ────────────────────────────────────────────────── */

typedef struct TuiCell
{
  char ch;
  uint8_t fg;
  uint8_t bg;
  uint8_t attr;
} TuiCell;

typedef struct TuiScreen
{
  int rows;
  int cols;
  TuiCell *cells;   /* current frame  (rows * cols) */
  TuiCell *shadow;  /* previous frame (rows * cols) */
  bool first_flush; /* true until first tui_screen_flush */
} TuiScreen;

/* ── Terminal lifecycle ───────────────────────────────────────────── */

bool tui_init(void);
void tui_shutdown(void);
void tui_get_size(int *rows, int *cols);

/* ── Screen buffer ────────────────────────────────────────────────── */

TuiScreen *tui_screen_create(int rows, int cols);
void tui_screen_destroy(TuiScreen *scr);
void tui_screen_clear(TuiScreen *scr, TuiCell fill);
void tui_screen_put(TuiScreen *scr, int row, int col,
                    char ch, uint8_t fg, uint8_t bg, uint8_t attr);
void tui_screen_print(TuiScreen *scr, int row, int col,
                      const char *text, uint8_t fg, uint8_t bg,
                      uint8_t attr);
void tui_screen_box(TuiScreen *scr, int row, int col,
                    int h, int w, uint8_t fg, uint8_t bg);
void tui_screen_flush(TuiScreen *scr);

/* ── Input ────────────────────────────────────────────────────────── */

int tui_poll_key(void); /* non-blocking, returns TUI_KEY_NONE if nothing */
int tui_wait_key(void); /* blocking */

/* ── Utility ──────────────────────────────────────────────────────── */

void tui_sleep_ms(int ms);

#endif /* TUI_H */
