#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>


#include <sys/stat.h>
#include <sys/types.h>
#include <sys/poll.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "module.h"

pthread_mutex_t m;


// NOTE: Open flags like in clusterwide-config.save
// NOTE: Open mode value is ok for this purpose, i guess
static int file_write(const char* path, const char* data) {
  int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_SYNC,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if(fd == -1) {
    say_error("open() error: %s, path: %s", strerror(errno), path);
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
    say_error("close() error: %s", strerror(errno));
    return -1;
  }
  say_verbose("%s has written", path);
  return 0;
}

static ssize_t va_file_write(va_list argp) {
  const char* path = va_arg(argp, char*);
  const char* data = va_arg(argp, char*);
  return file_write(path, data);
}

static int lua_file_write(lua_State *L) {
  const char* path = luaL_checkstring(L, -2);
  const char* data = luaL_checkstring(L, -1);
  if(coio_call(va_file_write, path, data) == -1 ) {
    say_error("coio_call() error: %s", strerror(errno));
    return -1;
  }
  return 0;
}

// FIXME: strtok is thread-unsafe, that's why there mutex is needed,
// need to find thread-safe replacement
static int mktree(char* path) {
  char* tmp_path = strdup(path);
  // FIXME: here possible buffer overflow
  char current_dir[512] = "."; // FIXME: must be "/"
  char* ctxptr;
  /* struct stat* st = malloc(sizeof(struct stat)); */
  struct stat st;
  char* dir = strtok(tmp_path, "/");
  while(dir != NULL) {
    char* tmp_dir = strdup(current_dir);
    sprintf(current_dir, "%s/%s", tmp_dir, dir);
    mode_t mode = 0744;
    int stat_rc = stat(current_dir, &st);
    say_info("current_dir: %s", current_dir);
    if(stat_rc == -1) {
      if(mkdir(current_dir, mode) ==  -1) {
        say_error("mkdir() error: %s, path: %s, mode: %x", strerror(errno), current_dir, mode);
        return -1;
      } else {
        say_info("Directory '%s' has created", current_dir);
      }
    } else if(!S_ISDIR(st.st_mode)) {
      say_warn("path: %s : %s", current_dir, strerror(EEXIST));
      return -1;
    }
    dir = strtok(NULL, "/");
  }
  return 0;
}

static ssize_t va_mktree(va_list ap) {
  char* path = va_arg(ap, char*);
  int n = va_arg(ap, int);
  return mktree(path);
}


static int lua_mktree(lua_State *L) {
  const char* path = luaL_checkstring(L, 1);
  if(coio_call(va_mktree, path) == -1) {
    say_error("coio_call() error");
    return 1;
  }
  return 0;
}

// FIXME: proper "mktree"
// FIXME: remove "config.prepare" harcode string
static int cw_save(char* path, char** sections_k, char** sections_v, int section_l) {
  /* pthread_mutex_lock(&m); */
  if(mktree(path) == -1 ) {
    say_error("mktree() error");
    return -1;
  }

  for (int i = 0; i < section_l; i++) {
    char tmp_path[512];
    sprintf(tmp_path, "%s/%s", path, sections_k[i]);
    if(file_write(tmp_path, sections_v[i]) == -1) {
      say_error("file_write() error: %s", strerror(errno));
      goto rollback;
    }
  }

  if(rename(path, "tmp/config.prepare") == -1) {
    say_error("rename() error: %s", strerror(errno));
    goto rollback;
  }

  say_verbose("%s has renamed to tmp/config.prepare", path);
  goto exit;
rollback:
  if(remove(path) == -1) {
    say_error("remove error: %s, path: %s", strerror(errno), path);
  }
  return -1;
exit:
  return 0;
}

static ssize_t va_cw_save(va_list argp) {
  char* path = va_arg(argp, char*);
  char** keys = va_arg(argp, char**);
  char** values = va_arg(argp, char**);
  int l = va_arg(argp, int);
  return cw_save(path, keys, values, l);
}

static int lua_cw_save(lua_State *L) {
  const char* path = luaL_checkstring(L, 1);
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
    say_error("coio_call() error");
    return -1;
  }

  return 0;
}

static ssize_t _say_info() {
  say_info("debug");
  return 0;
}

static int lua_say_info(lua_State *L) {
  if(coio_call(_say_info) == -1) {
    say_error("coio_call() error");
    return -1;
  }
  return 0;
}

static const struct luaL_Reg functions[] = {
  {"say_info", lua_say_info},
  {"mktree", lua_mktree},
  {"file_write", lua_file_write},
  {"cw_save", lua_cw_save},
  {NULL, NULL}
};


// NOTE: why i need to use "luaopen_" prefix?
int luaopen_hello(lua_State *L) {
  pthread_mutex_init(&m, NULL);
  luaL_register(L, "hello", functions);
  return 1;
}
