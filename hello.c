#include <stdio.h>
#include "lauxlib.h"
#include "lua.h"
#include "module.h"
#include <string.h>

#define GREET "Hello, "

int say_hello(lua_State *L) {
  const char* name = luaL_checkstring(L, -1);
  int strl = strlen(name) - 1;
  int greet_l = strlen(GREET) - 1;
  char out[greet_l + strl + 1];
  sprintf(out, "Hello, %s", name);
  lua_pushstring(L, out);
  return 1;
}

static const struct luaL_Reg functions[] = {
  {"say_hello", say_hello},
  {NULL, NULL}
};


// NOTE: why i need to use "luaopen_" prefix?
int luaopen_hello(lua_State *L) {
  luaL_register(L, "hello", functions);
  return 1;
}
