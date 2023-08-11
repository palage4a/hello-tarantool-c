#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/poll.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "module.h"

static int va_mkdir(va_list ap) {
  const char* dirname = va_arg(ap, char*);
  mode_t mode = 777;
  pthread_t pid = pthread_self();
  for (int i = 0; i <= 10; i++) {
    if (mkdir(dirname, mode) == -1) {
      say_warn("Pid: %d: Directory %s has already created", (int)pid, dirname);
    } else {
      say_warn("Pid: %d: Directory %s has created", (int)pid, dirname);
    }
  }
  return 0;
}

/*
  Lua analog:
  local function mktree(path)
      checks('string')
      path = fio.abspath(path)

      local path = string.gsub(path, '^/', '')
      local dirs = string.split(path, "/")

      local current_dir = "/"
      for _, dir in ipairs(dirs) do
          current_dir = fio.pathjoin(current_dir, dir)
          local stat = fio.stat(current_dir)
          if stat == nil then
              local _, err = fio.mkdir(current_dir)
              local _errno = errno()
              if err ~= nil and not fio.path.is_dir(current_dir) then
                  return nil, errors.new('MktreeError',
                      'Error creating directory %q: %s',
                      current_dir, errno.strerror(_errno)
                  )
              end
          elseif not stat:is_dir() then
              local EEXIST = assert(errno.EEXIST)
              return nil, errors.new('MktreeError',
                  'Error creating directory %q: %s',
                  current_dir, errno.strerror(EEXIST)
              )
          end
      end
      return true
  end
*/

static int mktree(char** dirs, int len) {
  // FIXME: here possible buffer overflow
  char current_dir[256] = ".";
  for (int i = 0; i < len - 1; i++) {
    char* tmp_dir = current_dir;
    sprintf(current_dir, "%s/%s", tmp_dir, dirs[i]);
    mode_t mode = 0777;
    struct stat* st;
    say_info("curdir: %s", current_dir);
    int stat_rc = stat(current_dir, st);
    if(stat_rc == -1) {
      if(mkdir(current_dir, mode) ==  -1) {
        say_error("cant 'mkdir': %s", strerror(errno));
        return -1;
      } else {
        say_info("Directory '%s' has created", current_dir);
      }
    } else if(!S_ISDIR(st->st_mode)) {
      say_warn("'%s' isn't directory", current_dir);
      return -1;
    }
  }
  say_info("finished");
  return 0;
}

static int va_mktree(va_list ap) {
  char** dirs = va_arg(ap, char**);
  int n = va_arg(ap, int);
  return mktree(dirs, n);
}


static int lua_mktree(lua_State *L) {
  const char* dirs[100];
  int i = 1;
  do {
    lua_pushnumber(L, i);
    lua_gettable(L, -2);
    if(lua_isnoneornil(L, -1))
      break;

    if(!lua_isstring(L, -1)) {
      const char* _type = luaL_typename(L, -1);
      say_error("wrong format of table field at index %d: expect string, actual is %s", i, _type);
      return -1;
    }
    dirs[i-1] = lua_tostring(L, -1);
    lua_pop(L, 1);
    i++;
  } while(true);

  long long before = fiber_clock64();
  if(coio_call(va_mktree, dirs, i) == -1) {
    say_error("error coio_call");
    say_error("%s", strerror(errno));
    return 1;
  }
  long long after = fiber_clock64();
  long long dur = (after - before) / 1e3;
  say_info("coio_call duration ms: %lld", dur);
  return 0;
}


static int run(lua_State *L) {
  const char* dirname = luaL_checkstring(L, -1);
  long long before = fiber_clock64();
  if(coio_call(va_mkdir, dirname)) {
    say_error("error coio_call");
    return 1;
  }
  long long after = fiber_clock64();
  long long dur = (after - before) / 1e3;
  say_info("coio_call duration ms: %lld", dur);
  return 0;
}


static const struct luaL_Reg functions[] = {
  {"run", run},
  {"mktree", lua_mktree},
  {NULL, NULL}
};

// NOTE: why i need to use "luaopen_" prefix?
int luaopen_hello(lua_State *L) {
  luaL_register(L, "hello", functions);
  return 1;
}
