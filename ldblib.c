/*
** $Id: ldblib.c,v 1.50 2002/05/06 19:05:10 roberto Exp roberto $
** Interface from Lua to its debug API
** See Copyright Notice in lua.h
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "luadebug.h"
#include "lualib.h"



static void settabss (lua_State *L, const char *i, const char *v) {
  lua_pushstring(L, i);
  lua_pushstring(L, v);
  lua_rawset(L, -3);
}


static void settabsi (lua_State *L, const char *i, int v) {
  lua_pushstring(L, i);
  lua_pushnumber(L, v);
  lua_rawset(L, -3);
}


static int getinfo (lua_State *L) {
  lua_Debug ar;
  const char *options = luaL_opt_string(L, 2, "flnSu");
  if (lua_isnumber(L, 1)) {
    if (!lua_getstack(L, (int)(lua_tonumber(L, 1)), &ar)) {
      lua_pushnil(L);  /* level out of range */
      return 1;
    }
  }
  else if (lua_isfunction(L, 1)) {
    luaL_vstr(L, ">%s", options);
    options = lua_tostring(L, -1);
    lua_pushvalue(L, 1);
  }
  else
    return luaL_argerror(L, 1, "function or level expected");
  if (!lua_getinfo(L, options, &ar))
    return luaL_argerror(L, 2, "invalid option");
  lua_newtable(L);
  for (; *options; options++) {
    switch (*options) {
      case 'S':
        settabss(L, "source", ar.source);
        if (ar.source)
          settabss(L, "short_src", ar.short_src);
        settabsi(L, "linedefined", ar.linedefined);
        settabss(L, "what", ar.what);
        break;
      case 'l':
        settabsi(L, "currentline", ar.currentline);
        break;
      case 'u':
        settabsi(L, "nups", ar.nups);
        break;
      case 'n':
        settabss(L, "name", ar.name);
        settabss(L, "namewhat", ar.namewhat);
        break;
      case 'f':
        lua_pushliteral(L, "func");
        lua_pushvalue(L, -3);
        lua_rawset(L, -3);
        break;
    }
  }
  return 1;  /* return table */
}
    

static int getlocal (lua_State *L) {
  lua_Debug ar;
  const char *name;
  if (!lua_getstack(L, luaL_check_int(L, 1), &ar))  /* level out of range? */
    return luaL_argerror(L, 1, "level out of range");
  name = lua_getlocal(L, &ar, luaL_check_int(L, 2));
  if (name) {
    lua_pushstring(L, name);
    lua_pushvalue(L, -2);
    return 2;
  }
  else {
    lua_pushnil(L);
    return 1;
  }
}


static int setlocal (lua_State *L) {
  lua_Debug ar;
  if (!lua_getstack(L, luaL_check_int(L, 1), &ar))  /* level out of range? */
    return luaL_argerror(L, 1, "level out of range");
  luaL_check_any(L, 3);
  lua_pushstring(L, lua_setlocal(L, &ar, luaL_check_int(L, 2)));
  return 1;
}



static const char KEY_CALLHOOK = 'c';
static const char KEY_LINEHOOK = 'l';


static void hookf (lua_State *L, void *key) {
  lua_pushudataval(L, key);
  lua_rawget(L, LUA_REGISTRYINDEX);
  if (lua_isfunction(L, -1)) {
    lua_pushvalue(L, -2);  /* original argument (below function) */
    lua_rawcall(L, 1, 0);
  }
  else
    lua_pop(L, 1);  /* pop result from gettable */
}


static void callf (lua_State *L, lua_Debug *ar) {
  lua_pushstring(L, ar->event);
  hookf(L, (void *)&KEY_CALLHOOK);
}


static void linef (lua_State *L, lua_Debug *ar) {
  lua_pushnumber(L, ar->currentline);
  hookf(L, (void *)&KEY_LINEHOOK);
}


static void sethook (lua_State *L, void *key, lua_Hook hook,
                     lua_Hook (*sethookf)(lua_State * L, lua_Hook h)) {
  lua_settop(L, 1);
  if (lua_isnoneornil(L, 1))
    (*sethookf)(L, NULL);
  else if (lua_isfunction(L, 1))
    (*sethookf)(L, hook);
  else
    luaL_argerror(L, 1, "function expected");
  lua_pushudataval(L, key);
  lua_rawget(L, LUA_REGISTRYINDEX);   /* get old value */
  lua_pushudataval(L, key);
  lua_pushvalue(L, 1);
  lua_rawset(L, LUA_REGISTRYINDEX);  /* set new value */
}


static int setcallhook (lua_State *L) {
  sethook(L, (void *)&KEY_CALLHOOK, callf, lua_setcallhook);
  return 1;
}


static int setlinehook (lua_State *L) {
  sethook(L, (void *)&KEY_LINEHOOK, linef, lua_setlinehook);
  return 1;
}


static int debug (lua_State *L) {
  for (;;) {
    char buffer[250];
    fputs("lua_debug> ", stderr);
    if (fgets(buffer, sizeof(buffer), stdin) == 0 ||
        strcmp(buffer, "cont\n") == 0)
      return 0;
    lua_dostring(L, buffer);
    lua_settop(L, 0);  /* remove eventual returns */
  }
}


#define LEVELS1	12	/* size of the first part of the stack */
#define LEVELS2	10	/* size of the second part of the stack */

static int errorfb (lua_State *L) {
  int level = 1;  /* skip level 0 (it's this function) */
  int firstpart = 1;  /* still before eventual `...' */
  lua_Debug ar;
  luaL_check_string(L, 1);
  lua_settop(L, 1);
  lua_pushliteral(L, "\n");
  while (lua_getstack(L, level++, &ar)) {
    char buff[10];
    if (level == 2)
      lua_pushliteral(L, "stack traceback:\n");
    else if (level > LEVELS1 && firstpart) {
      /* no more than `LEVELS2' more levels? */
      if (!lua_getstack(L, level+LEVELS2, &ar))
        level--;  /* keep going */
      else {
        lua_pushliteral(L, "       ...\n");  /* too many levels */
        while (lua_getstack(L, level+LEVELS2, &ar))  /* find last levels */
          level++;
      }
      firstpart = 0;
      continue;
    }
    sprintf(buff, "%4d:  ", level-1);
    lua_pushstring(L, buff);
    lua_getinfo(L, "Snl", &ar);
    switch (*ar.namewhat) {
      case 'g':  case 'l':  /* global, local */
        luaL_vstr(L, "function `%s'", ar.name);
        break;
      case 'f':  /* field */
      case 'm':  /* method */
        luaL_vstr(L, "method `%s'", ar.name);
        break;
      default: {
        if (*ar.what == 'm')  /* main? */
          luaL_vstr(L, "main of %s", ar.short_src);
        else if (*ar.what == 'C')  /* C function? */
          luaL_vstr(L, "%s", ar.short_src);
        else
          luaL_vstr(L, "function <%d:%s>", ar.linedefined, ar.short_src);
        ar.source = NULL;  /* do not print source again */
      }
    }
    if (ar.currentline > 0)
      luaL_vstr(L, " at line %d", ar.currentline);
    if (ar.source)
      luaL_vstr(L, " [%s]", ar.short_src);
    lua_pushliteral(L, "\n");
    lua_concat(L, lua_gettop(L));
  }
  lua_concat(L, lua_gettop(L));
  return 1;
}


static const luaL_reg dblib[] = {
  {"getlocal", getlocal},
  {"getinfo", getinfo},
  {"setcallhook", setcallhook},
  {"setlinehook", setlinehook},
  {"setlocal", setlocal},
  {"debug", debug},
  {NULL, NULL}
};


LUALIB_API int lua_dblibopen (lua_State *L) {
  luaL_opennamedlib(L, "dbg", dblib, 0);
  lua_register(L, "_ERRORMESSAGE", errorfb);
  return 0;
}

