build:
	tarantoolctl rocks make
clean:
	rm -rf hello.o hello.so .rocks config.prepare tmp
