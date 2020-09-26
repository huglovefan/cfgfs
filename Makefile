#SANITIZER := -fsanitize=thread,undefined
#SANITIZER := -fsanitize=address,undefined
#PGO       := 1
CC        := clang
CCACHE    := ccache

CFLAGS ?= -Ofast -g

$(shell if [ "$$(id -u)" = 0 ]; then >&2 echo "don't run this as root idiot"; kill $$PPID; fi)

SRCS = src/buffer_list.c \
       src/buffers.c \
       src/cli.c \
       src/click.c \
       src/logtail.c \
       src/lua.c \
       src/main.c \
       src/reload_thread.c

EXE := $(shell basename -- "$$(pwd)")
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)
CPPFLAGS += -DEXE='"$(EXE)"'
CFLAGS += -MMD

CFLAGS += -Weverything \
          -Werror=implicit-function-declaration \
          -Wno-alloca \
          -Wno-atomic-implicit-seq-cst \
          -Wno-disabled-macro-expansion \
          -Wno-gnu-statement-expression \
          -Wno-gnu-zero-variadic-macro-arguments \
          -Wno-language-extension-token \
          -Wno-padded \
          -Wno-reserved-id-macro \
          -Wno-vla

# ------------------------------------------------------------------------------

# cli.c
CFLAGS += $(shell pkg-config --cflags readline)
LIBS   += $(shell pkg-config --libs   readline)

# click.c
CFLAGS += $(shell pkg-config --cflags x11 xtst)
LIBS   += $(shell pkg-config --libs   x11 xtst)

# lua.c
LIBS += -llua -ldl -lm

# main.c
CFLAGS += $(shell pkg-config --cflags fuse3)
LIBS   += $(shell pkg-config --libs   fuse3)
CPPFLAGS += -DFUSE_USE_VERSION=35

# ------------------------------------------------------------------------------

# pgo
ifeq ($(PGO),1)
 CFLAGS   += -fprofile-generate
 LDFLAGS  += -fprofile-generate
 CPPFLAGS += -DPGO=1
else ifeq ($(PGO),2)
 $(shell llvm-profdata merge -output=default.profdata default_*.profraw)
 CFLAGS  += -fprofile-use
 LDFLAGS += -fprofile-use
endif

# sanitizer
ifneq ($(SANITIZER),)
 CPPFLAGS += -DSANITIZER=\"$(SANITIZER)\"
 LDFLAGS  += $(SANITIZER)
endif

# ------------------------------------------------------------------------------

$(EXE): $(OBJS)
	$(CCACHE) $(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

-include $(DEPS)
.c.o:
	$(CCACHE) $(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

tf2sim: tf2sim.cpp
	g++ -O2 $^ -o $@

clean:
	rm -- $(EXE) $(DEPS) $(OBJS) core.[0-9]* vgcore.[0-9]* *.profdata *.profraw

# ------------------------------------------------------------------------------

GAMEDIR := ~/.local/share/Steam/steamapps/common/Team\ Fortress\ 2/tf
MNTPNT := $(GAMEDIR)/custom/!cfgfs/cfg

MNTPNT2 := mnt

# start in game directory
start: cfgfs
	@set -e; \
	fusermount -u $(MNTPNT) || true; \
	fusermount -u $(MNTPNT2) || true; \
	! [ -d $(MNTPNT2) -a ! -L $(MNTPNT2) ] || rmdir $(MNTPNT2); \
	! [ ! -e $(MNTPNT2) ] || ln -fs $(MNTPNT) $(MNTPNT2); \
	exec ./cfgfs $(MNTPNT)

# start it here
start2: cfgfs
	@set -e \
	fusermount -u $(MNTPNT) || true; \
	fusermount -u $(MNTPNT2) || true; \
	! [ -L $(MNTPNT2) ] || rm -f $(MNTPNT2); \
	! [ ! -e $(MNTPNT2) ] || mkdir $(MNTPNT2); \
	exec ./cfgfs $(MNTPNT2)
