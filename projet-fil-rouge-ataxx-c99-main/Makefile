# ── Toolchain ────────────────────────────────────────────────────────
CC      := gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS ?=

# ── Platform detection ───────────────────────────────────────────────
# Check MSYSTEM first — MSYS2 also sets OS=Windows_NT, so we must
# detect it before the generic Windows branch.
ifneq ($(MSYSTEM),)
  # Running inside an MSYS2 shell (UCRT64, MINGW64, MINGW32, …)
  EXE     := .exe
  SHLIB   := .dll
  SHFLAGS := -shared
  DL_LIBS :=
  RM_F    := rm -f
  MKDIR_P := mkdir -p
  SEP     := /
else ifeq ($(OS),Windows_NT)
  # Native Windows (PowerShell / cmd)
  EXE     := .exe
  SHLIB   := .dll
  SHFLAGS := -shared
  DL_LIBS :=
  RM_F    := del /Q /F
  MKDIR_P := if not exist plugins mkdir
  SEP     := \\
else
  # Linux / macOS
  EXE     :=
  SHLIB   := .so
  SHFLAGS := -shared -fPIC
  DL_LIBS := -ldl
  RM_F    := rm -f
  MKDIR_P := mkdir -p
  SEP     := /
endif

# ── Plugins directory ─────────────────────────────────────────────────
PLUGINS_DIR := plugins
LEGACY_AGENT_OBJ := src$(SEP)agent_random.o src$(SEP)agent_random_plugin.o

# ── Source sets ──────────────────────────────────────────────────────
CORE_SRC   := src/game.c src/avl.c
AGENT_SRC  := src/agents/agent_random.c
CLI_SRC    := src/main.c $(CORE_SRC) $(AGENT_SRC) src/agent_loader.c
HARNESS_SRC:= src/main_harness.c $(CORE_SRC) $(AGENT_SRC) src/tui.c src/agent_loader.c
TOURNEY_SRC:= src/main_tournament.c $(CORE_SRC) $(AGENT_SRC) src/agent_loader.c

CLI_OBJ     := $(CLI_SRC:.c=.o)
HARNESS_OBJ := $(HARNESS_SRC:.c=.o)
TOURNEY_OBJ := $(TOURNEY_SRC:.c=.o)
TEST_AVL_OBJ:= tests/test_avl.o src/avl.o
TEST_TUI_OBJ:= tests/test_tui.o src/tui.o

# ── Default target ───────────────────────────────────────────────────
all: ataxx_cli$(EXE) ataxx_harness$(EXE) ataxx_tournament$(EXE) test_avl$(EXE)

# ── Original CLI binary (unchanged behaviour) ───────────────────────
ataxx_cli$(EXE): $(CLI_OBJ)
	$(CC) $(CFLAGS) -o $@ $(CLI_OBJ) $(LDFLAGS) $(DL_LIBS)

# ── Interactive TUI harness ──────────────────────────────────────────
ataxx_harness$(EXE): $(HARNESS_OBJ)
	$(CC) $(CFLAGS) -o $@ $(HARNESS_OBJ) $(LDFLAGS) $(DL_LIBS)

# ── Tournament engine ────────────────────────────────────────────────
ataxx_tournament$(EXE): $(TOURNEY_OBJ)
	$(CC) $(CFLAGS) -o $@ $(TOURNEY_OBJ) $(LDFLAGS) $(DL_LIBS)

# ── Tests ────────────────────────────────────────────────────────────
test_avl$(EXE): $(TEST_AVL_OBJ)
	$(CC) $(CFLAGS) -o $@ $(TEST_AVL_OBJ) $(LDFLAGS)

test_tui$(EXE): $(TEST_TUI_OBJ)
	$(CC) $(CFLAGS) -o $@ $(TEST_TUI_OBJ) $(LDFLAGS)

# ── Random agent plugin ─────────────────────────────────────────────
agent_random_plugin: $(PLUGINS_DIR)$(SEP)agent_random$(SHLIB)

$(PLUGINS_DIR)$(SEP)agent_random$(SHLIB): src/agents/agent_random.c src/game.c | $(PLUGINS_DIR)
	$(CC) $(CFLAGS) $(SHFLAGS) -o $@ src/agents/agent_random.c src/game.c $(LDFLAGS)

# ── Student plugin  (usage: make student_plugin SRC=myagent.c NAME=myname)
SRC  ?= my_agent.c
NAME ?= custom
SRC_PATH := $(if $(wildcard $(SRC)),$(SRC),$(if $(wildcard src/$(SRC)),src/$(SRC),$(if $(wildcard src/agents/$(SRC)),src/agents/$(SRC),$(SRC))))
student_plugin: $(SRC_PATH) $(CORE_SRC) | $(PLUGINS_DIR)
	$(CC) $(CFLAGS) $(SHFLAGS) -o $(PLUGINS_DIR)$(SEP)agent_$(NAME)$(SHLIB) $(SRC_PATH) $(CORE_SRC) $(LDFLAGS)

# ── Create plugins directory ─────────────────────────────────────────
$(PLUGINS_DIR):
	$(MKDIR_P) $(PLUGINS_DIR)

# ── Pattern rule ─────────────────────────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Clean ────────────────────────────────────────────────────────────
clean:
	-$(RM_F) $(CLI_OBJ) $(HARNESS_OBJ) $(TOURNEY_OBJ) $(TEST_AVL_OBJ) $(TEST_TUI_OBJ)
	-$(RM_F) $(LEGACY_AGENT_OBJ)
	-$(RM_F) ataxx_cli$(EXE) ataxx_harness$(EXE) ataxx_tournament$(EXE) test_avl$(EXE) test_tui$(EXE)
	-$(RM_F) $(PLUGINS_DIR)$(SEP)*$(SHLIB)

.PHONY: all clean agent_random_plugin student_plugin tournament

# ── Convenience target: build and run tournament ─────────────────────
tournament: ataxx_tournament$(EXE) agent_random_plugin
	.$(SEP)ataxx_tournament$(EXE)