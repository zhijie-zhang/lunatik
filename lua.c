/*
** $Id: lua.c,v 1.109 2002/11/19 13:49:43 roberto Exp roberto $
** Lua stand-alone interpreter
** See Copyright Notice in lua.h
*/


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


/*
** generic extra include file
*/
#ifdef LUA_USERCONFIG
#include LUA_USERCONFIG
#endif


#ifdef _POSIX_SOURCE
#include <unistd.h>
#else
static int isatty (int x) { return x==0; }  /* assume stdin is a tty */
#endif



#ifndef PROMPT
#define PROMPT		"> "
#endif


#ifndef PROMPT2
#define PROMPT2		">> "
#endif


#ifndef lua_userinit
#define lua_userinit(L)		openstdlibs(L)
#endif


#ifndef LUA_EXTRALIBS
#define LUA_EXTRALIBS	/* empty */
#endif


static lua_State *L = NULL;

static const char *progname;



static const luaL_reg lualibs[] = {
  {"baselib", lua_baselibopen},
  {"tablib", lua_tablibopen},
  {"iolib", lua_iolibopen},
  {"strlib", lua_strlibopen},
  {"mathlib", lua_mathlibopen},
  {"dblib", lua_dblibopen},
  /* add your libraries here */
  LUA_EXTRALIBS
  {NULL, NULL}
};



static void lstop (lua_State *l, lua_Debug *ar) {
  (void)ar;  /* unused arg. */
  lua_sethook(l, NULL, 0, 0);
  luaL_error(l, "interrupted!");
}


static void laction (int i) {
  signal(i, SIG_DFL); /* if another SIGINT happens before lstop,
                              terminate process (default action) */
  lua_sethook(L, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}


static void print_usage (void) {
  fprintf(stderr,
  "usage: %s [options] [script [args]].\n"
  "Available options are:\n"
  "  -        execute stdin as a file\n"
  "  -e stat  execute string `stat'\n"
  "  -i       enter interactive mode after executing `script'\n"
  "  -l name  load and run library `name'\n"
  "  -v       show version information\n"
  "  --       stop handling options\n" ,
  progname);
}


static void l_message (const char *pname, const char *msg) {
  if (pname) fprintf(stderr, "%s: ", pname);
  fprintf(stderr, "%s\n", msg);
}


static int report (int status) {
  const char *msg;
  if (status) {
    msg = lua_tostring(L, -1);
    if (msg == NULL) msg = "(error with no message)";
    l_message(progname, msg);
    lua_pop(L, 1);
  }
  return status;
}


static int lcall (int narg, int clear) {
  int status;
  int base = lua_gettop(L) - narg;  /* function index */
  lua_getglobal(L, "_TRACEBACK");  /* get traceback function */
  lua_insert(L, base);  /* put it under chunk and args */
  signal(SIGINT, laction);
  status = lua_pcall(L, narg, (clear ? 0 : LUA_MULTRET), base);
  signal(SIGINT, SIG_DFL);
  lua_remove(L, base);  /* remove traceback function */
  return status;
}


static int l_panic (lua_State *l) {
  (void)l;
  l_message(progname, "unable to recover; exiting");
  return 0;
}


static void print_version (void) {
  l_message(NULL, LUA_VERSION "  " LUA_COPYRIGHT);
}


static void getargs (char *argv[], int n) {
  int i;
  lua_newtable(L);
  for (i=0; argv[i]; i++) {
    lua_pushnumber(L, i - n);
    lua_pushstring(L, argv[i]);
    lua_rawset(L, -3);
  }
  /* arg.n = maximum index in table `arg' */
  lua_pushliteral(L, "n");
  lua_pushnumber(L, i-n-1);
  lua_rawset(L, -3);
}


static int docall (int status) {
  if (status == 0) status = lcall(0, 1);
  return report(status);
}


static int file_input (const char *name) {
  return docall(luaL_loadfile(L, name));
}


static int dostring (const char *s, const char *name) {
  return docall(luaL_loadbuffer(L, s, strlen(s), name));
}


static int load_file (const char *name) {
  lua_getglobal(L, "require");
  if (!lua_isfunction(L, -1)) {  /* no `require' defined? */
    lua_pop(L, 1);
    return file_input(name);
  }
  else {
    lua_pushstring(L, name);
    return report(lcall(1, 1));
  }
}


/*
** this macro can be used by some `history' system to save lines
** read in manual input
*/
#ifndef lua_saveline
#define lua_saveline(L,line)	/* empty */
#endif


/*
** this macro defines a function to show the prompt and reads the
** next line for manual input
*/
#ifndef lua_readline
#define lua_readline(L,prompt)		readline(L,prompt)

/* maximum length of an input line */
#ifndef MAXINPUT
#define MAXINPUT	512
#endif


static int readline (lua_State *l, const char *prompt) {
  static char buffer[MAXINPUT];
  if (prompt) {
    fputs(prompt, stdout);
    fflush(stdout);
  }
  if (fgets(buffer, sizeof(buffer), stdin) == NULL)
    return 0;  /* read fails */
  else {
    lua_pushstring(l, buffer);
    return 1;
  }
}

#endif


static const char *get_prompt (int firstline) {
  const char *p = NULL;
  lua_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2");
  p = lua_tostring(L, -1);
  if (p == NULL) p = (firstline ? PROMPT : PROMPT2);
  lua_pop(L, 1);  /* remove global */
  return p;
}


static int incomplete (int status) {
  if (status == LUA_ERRSYNTAX &&
         strstr(lua_tostring(L, -1), "near `<eof>'") != NULL) {
    lua_pop(L, 1);
    return 1;
  }
  else
    return 0;
}


static int load_string (void) {
  int status;
  lua_settop(L, 0);
  if (lua_readline(L, get_prompt(1)) == 0)  /* no input? */
    return -1;
  if (lua_tostring(L, -1)[0] == '=') {  /* line starts with `=' ? */
    lua_pushfstring(L, "return %s", lua_tostring(L, -1)+1);/* `=' -> `return' */
    lua_remove(L, -2);  /* remove original line */
  }
  for (;;) {  /* repeat until gets a complete line */
    status = luaL_loadbuffer(L, lua_tostring(L, 1), lua_strlen(L, 1), "=stdin");
    if (!incomplete(status)) break;  /* cannot try to add lines? */
    if (lua_readline(L, get_prompt(0)) == 0)  /* no more input? */
      return -1;
    lua_concat(L, lua_gettop(L));  /* join lines */
  }
  lua_saveline(L, lua_tostring(L, 1));
  lua_remove(L, 1);  /* remove line */
  return status;
}


static void manual_input (void) {
  int status;
  const char *oldprogname = progname;
  progname = NULL;
  while ((status = load_string()) != -1) {
    if (status == 0) status = lcall(0, 0);
    report(status);
    if (status == 0 && lua_gettop(L) > 0) {  /* any result to print? */
      lua_getglobal(L, "print");
      lua_insert(L, 1);
      if (lua_pcall(L, lua_gettop(L)-1, 0, 0) != 0)
        l_message(progname, "error calling `print'");
    }
  }
  lua_settop(L, 0);  /* clear stack */
  fputs("\n", stdout);
  progname = oldprogname;
}


static int handle_argv (char *argv[], int *interactive) {
  if (argv[1] == NULL) {  /* no more arguments? */
    if (isatty(0)) {
      print_version();
      manual_input();
    }
    else
      file_input(NULL);  /* executes stdin as a file */
  }
  else {  /* other arguments; loop over them */
    int i;
    for (i = 1; argv[i] != NULL; i++) {
      if (argv[i][0] != '-') break;  /* not an option? */
      switch (argv[i][1]) {  /* option */
        case '-': {  /* `--' */
          i++;  /* skip this argument */
          goto endloop;  /* stop handling arguments */
        }
        case '\0': {
          file_input(NULL);  /* executes stdin as a file */
          break;
        }
        case 'i': {
          *interactive = 1;
          break;
        }
        case 'v': {
          print_version();
          break;
        }
        case 'e': {
          const char *chunk = argv[i] + 2;
          if (*chunk == '\0') chunk = argv[++i];
          if (chunk == NULL) {
            print_usage();
            return EXIT_FAILURE;
          }
          if (dostring(chunk, "=<command line>") != 0)
            return EXIT_FAILURE;
          break;
        }
        case 'l': {
          const char *filename = argv[i] + 2;
          if (*filename == '\0') filename = argv[++i];
          if (filename == NULL) {
            print_usage();
            return EXIT_FAILURE;
          }
          if (load_file(filename))
            return EXIT_FAILURE;  /* stop if file fails */
          break;
        }
        case 'c': {
          l_message(progname, "option `-c' is deprecated");
          break;
        }
        case 's': {
          l_message(progname, "option `-s' is deprecated");
          break;
        }
        default: {
          print_usage();
          return EXIT_FAILURE;
        }
      }
    } endloop:
    if (argv[i] != NULL) {
      const char *filename = argv[i];
      getargs(argv, i);  /* collect arguments */
      lua_setglobal(L, "arg");
      return file_input(filename);  /* stop scanning arguments */
    }
  }
  return 0;
}


static void openstdlibs (lua_State *l) {
  const luaL_reg *lib = lualibs;
  for (; lib->name; lib++) {
    lib->func(l);  /* open library */
    lua_settop(l, 0);  /* discard any results */
  }
}


static int handle_luainit (void) {
  const char *init = getenv("LUA_INIT");
  if (init == NULL) return 0;  /* status OK */
  else if (init[0] == '@')
    return file_input(init+1);
  else
    return dostring(init, "=LUA_INIT");
}


int main (int argc, char *argv[]) {
  int status;
  int interactive = 0;
  (void)argc;  /* to avoid warnings */
  progname = argv[0];
  L = lua_open();  /* create state */
  lua_atpanic(L, l_panic);
  lua_userinit(L);  /* open libraries */
  status = handle_luainit();
  if (status != 0) return status;
  status = handle_argv(argv, &interactive);
  if (status == 0 && interactive) manual_input();
  lua_close(L);
  return status;
}

