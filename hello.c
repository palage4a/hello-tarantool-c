#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

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

// TODO: make sure that len has correct value (and used correct in "for")
static int mktree(char** dirs, int len) {
  // FIXME: here possible buffer overflow
  char current_dir[256] = ".";
  for (int i = 0; i <= len - 1; i++) {
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
  return 0;
}

/*
local function file_write(path, data, opts, perm)
    checks('string', 'string', '?table', '?number')
    opts = opts or {'O_CREAT', 'O_WRONLY', 'O_TRUNC'}
    perm = perm or tonumber(644, 8)
    local file = fio.open(path, opts, perm)
    if file == nil then
        return nil, OpenFileError:new('%s: %s', path, errno.strerror())
    end

    local res = file:write(data)
    if not res then
        local err = WriteFileError:new('%s: %s', path, errno.strerror())
        fio.unlink(path)
        return nil, err
    end

    local res = file:close()
    if not res then
        local err = WriteFileError:new('%s: %s', path, errno.strerror())
        fio.unlink(path)
        return nil, err
    end

    return data
end
*/
static int file_write(const char* path, const char* data) {
  // NOTE: Flags like in clusterwide-config.save
  // NOTE: Mode is ok for this purpose, i guess
  int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_SYNC,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if(fd == -1) {
    say_error("cant open file: %s, err: %s", path, strerror(errno));
    return -1;
  }
  int count = strlen(data);
  ssize_t nr = write(fd, data, count);
  if(nr == -1){
    say_error("error while write(): %s", strerror(errno));
    return -1;
  }
  if(nr == 0 || nr != count) {
    say_warn("data wasn't written correctly, count of written bytes: %ld, expected: %d", nr, count);
  }
  if(close(fd) == -1) {
    say_error("close error: %s", strerror(errno));
    return -1;
  }
  return 0;
}

static int va_file_write(va_list argp) {
  const char* path = va_arg(argp, char*);
  const char* data = va_arg(argp, char*);
  return file_write(path, data);
}

static int lua_file_write(lua_State *L) {
  const char* path = luaL_checkstring(L, -2);
  const char* data = luaL_checkstring(L, -1);
  if(coio_call(va_file_write, path, data) == -1 ) {
    say_error("coio call error: %s", strerror(errno));
    return -1;
  }
  return 0;
}

/*
  local function save(clusterwide_config, path)
    checks('ClusterwideConfig', 'string')
    local random_path = utils.randomize_path(path)

    local ok, err = utils.mktree(random_path)
    if not ok then
        return nil, err
    end

    for section, content in pairs(clusterwide_config._plaintext) do
        local abspath = fio.pathjoin(random_path, section)
        local dirname = fio.dirname(abspath)

        ok, err = utils.mktree(dirname)
        if not ok then
            goto rollback
        end

        ok, err = utils.file_write(
            abspath, content,
            {'O_CREAT', 'O_EXCL', 'O_WRONLY', 'O_SYNC'}
        )
        if not ok then
            goto rollback
        end
    end

    ok = fio.rename(random_path, path)
    if not ok then
        err = SaveConfigError:new(
            '%s: %s',
            path, errno.strerror()
        )
        goto rollback
    else
        return true
    end

::rollback::
    local ok, _err = fio.rmtree(random_path)
    if not ok then
        log.warn(
            "Error removing %s: %s",
            random_path, _err
        )
    end

    return nil, err
end
*/
// TODO: rollback scenario (remove tmp directory)
// FIXME: proper "mktree"
// FIXME: remove "config.prepare" harcode string
static int cw_save(char* random_path, char** sections_k, char** sections_v, int section_l) {
  char* dirs[1];
  dirs[0] = random_path;
  if(mktree(dirs, 1) == -1 ) {
    say_warn("cant create tree");
    return -1;
  }
  for (int i = 0; i < section_l; i++) {
    char* path = malloc((sizeof random_path + sizeof sections_k[i]) * sizeof(char*));
    sprintf(path, "%s/%s", random_path, sections_k[i]);
    say_info("path: %s", path);
    if(file_write(path, sections_v[i]) == -1) {
      say_error("file write error: %s", strerror(errno));
      free(path);
      return -1;
    };
    free(path);
  }
  if(rename(random_path, "config.prepare") == -1) {
    say_error("rename() error: %s", strerror(errno));
    return -1;
  }
  return 0;
}

static int va_cw_save(va_list argp) {
  char* path = va_arg(argp, char*);
  char** keys = va_arg(argp, char**);
  char** values = va_arg(argp, char**);
  int l = va_arg(argp, int);
  return cw_save(path, keys, values, l);
}

static int lua_cw_save(lua_State *L) {
  const char* path = luaL_checkstring(L, 1);
  say_info("%s", path);
  int v = 1;
  const char* sections_v[100];
    do {
    lua_pushnumber(L, v);
    lua_gettable(L, 3);
    if(lua_isnoneornil(L, -1))
      break;

    if(!lua_isstring(L, -1)) {
      const char* _type = luaL_typename(L, -1);
      say_error("wrong format of table field at index %d: expect string, actual is %s", v, _type);
      return -1;
    }
    sections_v[v-1] = lua_tostring(L, -1);
    lua_pop(L, 1);
    v++;
  } while(true);

  const char* sections_k[100];
  int k = 1;
  do {
    lua_pushnumber(L, k);
    lua_gettable(L, 2);
    if(lua_isnoneornil(L, -1))
      break;

    if(!lua_isstring(L, -1)) {
      const char* _type = luaL_typename(L, -1);
      say_error("wrong format of table field at index %d: expect string, actual is %s", k, _type);
      return -1;
    }
    sections_k[k-1] = lua_tostring(L, -1);
    lua_pop(L, 1);
    k++;
  } while(true);

  if(k != v) {
    say_error("count of keys and count of values are different");
    return -1;
  }

  if(coio_call(va_cw_save, path, sections_k, sections_v, k-1) == -1) {
    say_error("error coio_call");
    say_error("%s", strerror(errno));
    return 1;
  }

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
  {"file_write", lua_file_write},
  {"cw_save", lua_cw_save},
  {NULL, NULL}
};

// NOTE: why i need to use "luaopen_" prefix?
int luaopen_hello(lua_State *L) {
  luaL_register(L, "hello", functions);
  return 1;
}
