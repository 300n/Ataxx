#include "tui.h"

#include <stdio.h>

int main(void)
{
  TuiScreen *scr;
  int rows, cols;
  int key;

  if (!tui_init())
  {
    fprintf(stderr, "tui_init failed\n");
    return 1;
  }

  tui_get_size(&rows, &cols);
  scr = tui_screen_create(rows, cols);
  if (!scr)
  {
    tui_shutdown();
    fprintf(stderr, "tui_screen_create failed\n");
    return 1;
  }

  /* clear screen */
  {
    TuiCell blank = {' ', TUI_WHITE, TUI_BLACK, TUI_ATTR_NONE};
    tui_screen_clear(scr, blank);
  }

  /* draw a box */
  tui_screen_box(scr, 2, 4, 8, 30, TUI_CYAN, TUI_BLACK);

  /* print coloured text inside the box */
  tui_screen_print(scr, 3, 6, "TUI Smoke Test",
                   TUI_BRIGHT_YELLOW, TUI_BLACK, TUI_ATTR_BOLD);
  tui_screen_print(scr, 5, 6, "Green text",
                   TUI_GREEN, TUI_BLACK, TUI_ATTR_NONE);
  tui_screen_print(scr, 6, 6, "Red bold text",
                   TUI_RED, TUI_BLACK, TUI_ATTR_BOLD);
  tui_screen_print(scr, 8, 6, "Press any key to quit...",
                   TUI_WHITE, TUI_BLACK, TUI_ATTR_DIM);

  tui_screen_flush(scr);

  /* wait for a key press */
  key = tui_wait_key();
  (void)key;

  tui_screen_destroy(scr);
  tui_shutdown();

  printf("TUI smoke test passed.\n");
  return 0;
}
