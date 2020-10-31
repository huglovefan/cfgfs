#SANITIZER := -fsanitize=thread,undefined
#SANITIZER := -fsanitize=address,undefined
#PGO       := 1
CC        := clang
CCACHE    := ccache

CFLAGS ?= -Ofast -g

$(shell if [ "$$(id -u)" = 0 ]; then >&2 echo "don't run this as root idiot"; kill $$PPID; fi)

SRCS = src/main.c \
       src/buffer_list.c \
       src/buffers.c \
       src/lua.c \
       src/reloader.c \
       src/cli_input.c \
       src/cli_output.c \
       src/click.c \
       src/logtail.c \
       src/vcr.c \
       src/attention.c

EXE := $(shell basename -- "$$(pwd)")
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

CFLAGS += -MMD -MP

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

# agpl compliance (https://www.gnu.org/licenses/gpl-howto.en.html)
ifneq ($(AGPL_SOURCE_URL),)
 CPPFLAGS += -DAGPL_SOURCE_URL='"$(AGPL_SOURCE_URL)"'
endif
ifeq (,$(findstring AGPL_SOURCE_URL,$(CPPFLAGS)))
 CPPFLAGS += -DAGPL_SOURCE_URL='"$(shell git ls-remote --get-url)"'
endif

# ------------------------------------------------------------------------------

# verbosity/debug enablings
ifneq ($(V),)
 CPPFLAGS += -DV="if(1)"
endif
ifneq ($(VV),)
 CPPFLAGS += -DVV="if(1)"
endif
ifneq ($(D),)
 CPPFLAGS += -DD="if(1)"
endif

# vcr.c
ifneq ($(VCR),)
 CPPFLAGS += -DWITH_VCR
endif

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

CFLAGS += -pthread
CPPFLAGS += -pthread
LIBS += -pthread

# barely measurable but: jemalloc > tcmalloc > glibc one
#LIBS += -ltcmalloc_minimal
LIBS += -ljemalloc

# assert in macros.h
CPPFLAGS += -DEXE='"$(EXE)"'

# cli.c
CFLAGS += $(shell pkg-config --cflags readline)
LIBS   += $(shell pkg-config --libs   readline)

# attention.c, click.c
CFLAGS += $(shell pkg-config --cflags x11 xtst)
LIBS   += $(shell pkg-config --libs   x11 xtst)

# lua.c
# (it only works with lua 5.4)
LIBS += -llua -ldl -lm

# main.c
CFLAGS += $(shell pkg-config --cflags fuse3)
LIBS   += $(shell pkg-config --libs   fuse3)
CPPFLAGS += -DFUSE_USE_VERSION=35

# ------------------------------------------------------------------------------

$(EXE): $(OBJS)
	$(CCACHE) $(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

-include $(DEPS)
.c.o:
	$(CCACHE) $(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

# ~

libvcr.so: libvcr.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ -pthread

tf2sim: tf2sim.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ -o $@

# ~

clean:
	rm -f -- $(EXE) $(DEPS) $(OBJS) core.[0-9]* vgcore.[0-9]* *.profdata \
	    *.profraw *.log perf.data* callgrind.out* *.d tf2sim

watch:
	@while ls $(SRCS) $$(cat $(DEPS) | sed 's/^[^:]\+://;/^$$/d;s/\\//') | awk '!t[$$0]++' | entr -c make; do\
		continue;\
	done

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
