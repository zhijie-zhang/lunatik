/*
** $Id: lapi.h,v 1.11 1999/12/14 18:33:29 roberto Exp roberto $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "lobject.h"


extern const lua_Type luaA_normtype[];

#define luaA_normalizedtype(o)	(luaA_normtype[-ttype(o)])


void luaA_setnormalized (TObject *d, const TObject *s);
void luaA_checkCparams (lua_State *L, int nParams);
const TObject *luaA_protovalue (const TObject *o);
void luaA_pushobject (lua_State *L, const TObject *o);
GlobalVar *luaA_nextvar (lua_State *L, TaggedString *g);
int luaA_next (lua_State *L, const Hash *t, int i);
lua_Object luaA_putluaObject (lua_State *L, const TObject *o);
lua_Object luaA_putObjectOnTop (lua_State *L);

#endif
