# C NATS Makefile

OBJ=build/stats.o build/pub.o build/sub.o build/srvpool.o build/conn.o build/msg.o \
    build/nats.o build/parser.o build/comsock.o build/buf.o build/status.o \
    build/unix/sock.o build/unix/mutex.o build/unix/thread.o build/unix/cond.o \
    build/util.o build/timer.o build/url.o build/opts.o build/asynccb.o build/hash.o build/time.o
    
EXAMPLES=publisher subscriber queuegroup requestor replier
TESTS=build/testsuite
LIBNAME=libnats


# Installation related variables and target
PREFIX?=install
INCLUDE_PATH?=include
LIBRARY_PATH?=lib
INSTALL_INCLUDE_PATH= $(DESTDIR)$(PREFIX)/$(INCLUDE_PATH)
INSTALL_LIBRARY_PATH= $(DESTDIR)$(PREFIX)/$(LIBRARY_PATH)

# Fallback to gcc when $CC is not in $PATH.
CC:=$(shell sh -c 'type $(CC) >/dev/null 2>/dev/null && echo $(CC) || echo gcc')
CXX:=$(shell sh -c 'type $(CXX) >/dev/null 2>/dev/null && echo $(CXX) || echo g++')
OPTIMIZATION?=-O3
WARNINGS=-Wall -W -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wstrict-prototypes -Wwrite-strings
DEBUG?=-g -ggdb
REAL_CFLAGS=$(OPTIMIZATION) -fPIC -D_REENTRANT -pthread $(CFLAGS) $(WARNINGS) $(ARCH)
REAL_LDFLAGS=$(LDFLAGS) $(ARCH)

NATS_OS=LINUX
DYLIBSUFFIX=so
STLIBSUFFIX=a
DYLIBNAME=$(LIBNAME).$(DYLIBSUFFIX)
DYLIB_MAKE_CMD=$(CC) -shared -Wl,-soname,$(DYLIBNAME) -o $(DYLIBNAME) $(LDFLAGS)
STLIBNAME=$(LIBNAME).$(STLIBSUFFIX)
STLIB_MAKE_CMD=ar rcs $(STLIBNAME)

# Platform-specific overrides
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(uname_S),Darwin)
  NATS_OS=DARWIN
  DYLIBSUFFIX=dylib
  DYLIB_MAKE_CMD=$(CC) -shared -Wl,-install_name,$(DYLIBNAME) -o $(DYLIBNAME) $(LDFLAGS) 
endif

all: $(DYLIBNAME) $(STLIBNAME) $(TESTS)

# Deps (use make dep to generate this)
asynccb.o: src/asynccb.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h src/conn.h
buf.o: src/buf.c src/mem.h src/buf.h src/status.h
comsock.o: src/comsock.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/comsock.h \
  src/mem.h
conn.o: src/conn.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/statusp.h \
  src/conn.h src/mem.h src/opts.h src/util.h src/sub.h src/comsock.h
hash.o: src/hash.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h
msg.o: src/msg.c src/natsp.h src/include/n-unix.h src/status.h src/buf.h \
  src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h
nats.o: src/nats.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h src/util.h
opts.o: src/opts.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h src/opts.h
parser.o: src/parser.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/conn.h src/util.h \
  src/mem.h
pub.o: src/pub.c src/natsp.h src/include/n-unix.h src/status.h src/buf.h \
  src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/conn.h src/sub.h
srvpool.o: src/srvpool.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h src/util.h
stats.o: src/stats.c src/status.h src/stats.h src/mem.h
status.o: src/status.c src/status.h src/statusp.h
sub.o: src/sub.c src/natsp.h src/include/n-unix.h src/status.h src/buf.h \
  src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h src/conn.h \
  src/sub.h src/util.h
test.o: src/test.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/opts.h src/util.h \
  src/conn.h src/sub.h
time.o: src/time.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h
timer.o: src/timer.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h src/util.h
url.o: src/url.c src/natsp.h src/include/n-unix.h src/status.h src/buf.h \
  src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h
util.o: src/util.c src/natsp.h src/include/n-unix.h src/status.h \
  src/buf.h src/parser.h src/timer.h src/url.h src/srvpool.h src/msg.h \
  src/asynccb.h src/hash.h src/stats.h src/time.h src/mem.h


$(DYLIBNAME): $(OBJ)
	$(DYLIB_MAKE_CMD) $(OBJ)

$(STLIBNAME): $(OBJ)
	$(STLIB_MAKE_CMD) $(OBJ)  

dynamic: $(DYLIBNAME)
static: $(STLIBNAME)

# Binaries:
publisher: examples/publisher.c $(DYLIBNAME)
	$(CC) -o examples/nats-$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I$(INSTALL_INCLUDE_PATH) $< -lpthread $(DYLIBNAME) 
subscriber: examples/subscriber.c $(DYLIBNAME)
	$(CC) -o examples/nats-$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I$(INSTALL_INCLUDE_PATH) $< -lpthread $(DYLIBNAME)
queuegroup: examples/queuegroup.c $(DYLIBNAME)
	$(CC) -o examples/nats-$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I$(INSTALL_INCLUDE_PATH) $< -lpthread $(DYLIBNAME)
requestor: examples/requestor.c $(DYLIBNAME)
	$(CC) -o examples/nats-$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I$(INSTALL_INCLUDE_PATH) $< -lpthread $(DYLIBNAME)
replier: examples/replier.c $(DYLIBNAME)
	$(CC) -o examples/nats-$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I$(INSTALL_INCLUDE_PATH) $< -lpthread $(DYLIBNAME)

examples: $(EXAMPLES)

build/testsuite: build/test.o $(STLIBNAME)
	$(CC) -o $@ $(OPTIMIZATION) -fPIC -D_REENTRANT $(CFLAGS) $(WARNINGS) $(ARCH) $(REAL_LDFLAGS) $< -lpthread $(STLIBNAME)


test: build/testsuite
	build/testsuite

build/%.o: src/%.c
	mkdir -p build/unix build/win
	$(CC) -std=c99 -pedantic -D$(NATS_OS) -c $(REAL_CFLAGS) -o $@ $<

clean:
	rm -rf $(STLIBNAME) $(DYLIBNAME) $(INSTALL_LIBRARY_PATH)/$(DYLIBNAME) \
$(INSTALL_LIBRARY_PATH)/$(STLIBNAME) $(INSTALL_INCLUDE_PATH)/*.h \
examples/*.o examples/nats-* build/* build/unix/*.o win/*.o

dep:
	$(CC) -MM src/*.c

INSTALL?= cp -a

install: $(DYLIBNAME) $(STLIBNAME)
	mkdir -p $(INSTALL_INCLUDE_PATH) $(INSTALL_LIBRARY_PATH)
	$(INSTALL) src/nats.h src/status.h $(INSTALL_INCLUDE_PATH)
	$(INSTALL) $(DYLIBNAME) $(INSTALL_LIBRARY_PATH)
	$(INSTALL) $(STLIBNAME) $(INSTALL_LIBRARY_PATH)

32bit:
	@echo ""
	@echo "WARNING: if this fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	$(MAKE) CFLAGS="-m32" LDFLAGS="-m32"

32bit-vars:
	$(eval CFLAGS=-m32)
	$(eval LDFLAGS=-m32)

debug:
	$(MAKE) OPTIMIZATION="$(DEBUG)"

.PHONY: all test clean dep install 32bit debug
