package = "hello-tarantool-c"
version = "scm-1"
source = {
  url = "git://github.com/palagec4a/hello-tarantool-c.git"
}

dependencies = {
  "tarantool"
}

build = {
  type = "builtin",
  modules = {
    hello = {
      sources = {"hello.c"}
    }
  }
}