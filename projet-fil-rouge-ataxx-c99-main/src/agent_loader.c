#include "agent_loader.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ================================================================== */
/*  Platform-specific dynamic loading                                 */
/* ================================================================== */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static char g_agent_plugin_error[256];

static void set_last_error(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vsnprintf(g_agent_plugin_error, sizeof(g_agent_plugin_error), fmt, args);
  va_end(args);
}

const char *agent_plugin_last_error(void)
{
  return g_agent_plugin_error;
}

/* ── Extract a short display name from a file path ────────────────── */

static void extract_name(const char *path, char *buf, size_t buf_size)
{
  const char *slash;
  const char *base;
  const char *dot;
  size_t len;

  /* find last path separator */
  slash = strrchr(path, '/');
#ifdef _WIN32
  {
    const char *bs = strrchr(path, '\\');
    if (bs && (!slash || bs > slash))
      slash = bs;
  }
#endif

  base = slash ? slash + 1 : path;

  /* strip extension */
  dot = strrchr(base, '.');
  len = dot ? (size_t)(dot - base) : strlen(base);

  if (len >= buf_size)
    len = buf_size - 1;
  memcpy(buf, base, len);
  buf[len] = '\0';
}

/* ── Load ─────────────────────────────────────────────────────────── */

bool agent_plugin_load(AgentPlugin *plugin, const char *path)
{
  memset(plugin, 0, sizeof(*plugin));
  g_agent_plugin_error[0] = '\0';

#ifdef _WIN32
  {
    HMODULE hmod = LoadLibraryA(path);
    if (!hmod)
    {
      set_last_error("LoadLibrary failed for '%s' (error %lu)",
                     path, (unsigned long)GetLastError());
      return false;
    }

    plugin->handle = (void *)hmod;
    {
      FARPROC proc = GetProcAddress(hmod, "agent_choose_move");
      memcpy(&plugin->choose_move, &proc, sizeof(plugin->choose_move));
    }

    if (!plugin->choose_move)
    {
      set_last_error("symbol 'agent_choose_move' not found in '%s'",
                     path);
      FreeLibrary(hmod);
      plugin->handle = NULL;
      return false;
    }
  }
#else
  {
    void *dl = dlopen(path, RTLD_NOW);
    if (!dl)
    {
      set_last_error("dlopen failed for '%s': %s",
                     path, dlerror());
      return false;
    }

    plugin->handle = dl;

    /* cast via intermediate void* to avoid pedantic -Wpedantic warnings
       about function/data pointer conversion (which is well-defined on
       all our target platforms) */
    *(void **)(&plugin->choose_move) = dlsym(dl, "agent_choose_move");

    if (!plugin->choose_move)
    {
      set_last_error("symbol 'agent_choose_move' not found in '%s': %s",
                     path, dlerror());
      dlclose(dl);
      plugin->handle = NULL;
      return false;
    }
  }
#endif

  extract_name(path, plugin->name, sizeof(plugin->name));
  g_agent_plugin_error[0] = '\0';
  return true;
}

/* ── Unload ───────────────────────────────────────────────────────── */

void agent_plugin_unload(AgentPlugin *plugin)
{
  if (!plugin || !plugin->handle)
    return;

#ifdef _WIN32
  FreeLibrary((HMODULE)plugin->handle);
#else
  dlclose(plugin->handle);
#endif

  memset(plugin, 0, sizeof(*plugin));
}
