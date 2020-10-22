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
       src/reload_thread.c \
       src/vcr.c

EXE := $(shell basename -- "$$(pwd)")
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)
CPPFLAGS += -DEXE='"$(EXE)"'
CFLAGS += -MMD

CFLAGS += -Weverything \
          -Werror=implicit-function-declaration \
          -Wno-alloca \
          -Wno-atomic-implicit-seq-cst \
          -Wno-c++98-compat \
          -Wno-disabled-macro-expansion \
          -Wno-format-nonliteral \
          -Wno-gnu-auto-type \
          -Wno-gnu-conditional-omitted-operand \
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

# vcr.c
ifneq ($(VCR),)
 CPPFLAGS += -DWITH_VCR
endif

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

clean:
	rm -f -- $(EXE) $(DEPS) $(OBJS) core.[0-9]* vgcore.[0-9]* *.profdata *.profraw *.log

libvcr.so:
	clang -O2 -shared -fPIC libvcr.c -o libvcr.so -lpthread

# ------------------------------------------------------------------------------

MNTLNK := mnt

TF2MNT := ~/.local/share/Steam/steamapps/common/Team\ Fortress\ 2/tf/custom/!cfgfs/cfg
FOFMNT := ~/.local/share/Steam/steamapps/common/Fistful\ of\ Frags/fof/custom/!cfgfs/cfg

# start in game (tf2) directory
start: cfgfs
	@set -e; \
	mount -t fuse.cfgfs | grep -Po ' on \K(.+)(?= type )' | xargs -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	[ -d $(TF2MNT) ] || mkdir -p $(TF2MNT); \
	ln -fs $(TF2MNT) $(MNTLNK); \
	exec ./cfgfs $(TF2MNT)

# start in game (fof) directory
startfof: cfgfs
	@set -e; \
	mount -t fuse.cfgfs | grep -Po ' on \K(.+)(?= type )' | xargs -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	[ -d $(FOFMNT) ] || mkdir -p $(FOFMNT); \
	ln -fs $(FOFMNT) $(MNTLNK); \
	CFGFS_SCRIPT=./script_fof.lua exec ./cfgfs $(FOFMNT)

# start it here
start2: cfgfs
	@set -e \
	mount -t fuse.cfgfs | grep -Po ' on \K(.+)(?= type )' | xargs -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	mkdir -p $(MNTLNK); \
	exec ./cfgfs $(MNTLNK)
