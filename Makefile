#SANITIZER := -fsanitize=thread,undefined
#SANITIZER := -fsanitize=address,undefined
CC        := $(shell if [ -n "$$CC" ]; then echo $$CC; elif command -v clang >/dev/null 2>&1; then echo clang; else echo cc; fi)
CCACHE    := $(shell if command -v ccache >/dev/null 2>&1; then echo ccache; fi)

CFLAGS ?= -Ofast -g

# ------------------------------------------------------------------------------

$(shell if [ "$$(id -u)" = 0 ]; then >&2 echo "don't run this as root idiot"; kill $$PPID; fi)

SRCS = src/main.c \
       src/buffer_list.c \
       src/buffers.c \
       src/lua.c \
       src/cfg.c \
       src/reloader.c \
       src/cli_input.c \
       src/cli_output.c \
       src/logtail.c \
       src/keys.c \
       src/click.c \
       src/attention.c \
       src/vcr.c

EXE := $(shell basename -- "$$(pwd)")
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

CPPFLAGS += -MMD -MP
CFLAGS += -fdiagnostics-color

ifeq (,$(findstring gcc,$(CC)))
# clang
CFLAGS += -Weverything \
          -Werror=implicit-function-declaration \
          -Wno-alloca \
          -Wno-atomic-implicit-seq-cst \
          -Wno-c++17-extensions \
          -Wno-c++98-compat \
          -Wno-disabled-macro-expansion \
          -Wno-format-nonliteral \
          -Wno-gnu-auto-type \
          -Wno-gnu-conditional-omitted-operand \
          -Wno-gnu-folding-constant \
          -Wno-gnu-statement-expression \
          -Wno-gnu-zero-variadic-macro-arguments \
          -Wno-language-extension-token \
          -Wno-padded \
          -Wno-reserved-id-macro \
          -Wno-vla
else
# gcc
CFLAGS += -Wall \
          -Wextra \
          -Werror=implicit-function-declaration \
          -Wno-bool-operation \
          -Wno-misleading-indentation \
          -Wno-unused-result
endif

# ------------------------------------------------------------------------------

## options

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

# agpl compliance (https://www.gnu.org/licenses/gpl-howto.en.html)
ifneq ($(AGPL_SOURCE_URL),)
 CPPFLAGS += -DAGPL_SOURCE_URL='"$(AGPL_SOURCE_URL)"'
endif
ifeq (,$(findstring AGPL_SOURCE_URL,$(CPPFLAGS)))
 CPPFLAGS += -DAGPL_SOURCE_URL='"$(shell git ls-remote --get-url)"'
endif

# ------------------------------------------------------------------------------

## requirancies

CFLAGS += -pthread
CPPFLAGS += -pthread
LIBS += -pthread

# assert in macros.h
CPPFLAGS += -DEXE='"$(EXE)"'

# readline (cli.c)
CFLAGS += $(shell pkg-config --cflags readline)
LIBS   += $(shell pkg-config --libs   readline)

# xlib (attention.c, click.c)
CFLAGS += $(shell pkg-config --cflags x11 xtst)
LIBS   += $(shell pkg-config --libs   x11 xtst)

# lua (lua.c)
# (it only works with lua 5.4)
LIBS += -llua -ldl -lm

# fuse (main.c)
CFLAGS += $(shell pkg-config --cflags fuse3)
CPPFLAGS += -DFUSE_USE_VERSION=35
ifeq ($(STATIC_LIBFUSE),)
 LIBS  += $(shell pkg-config --libs   fuse3)
else
 LIBS  += /usr/lib64/libfuse3.a -lpthread
endif

# malloc (a few places)
ifeq ($(STATIC_TCMALLOC),)
 LIBS += -ltcmalloc_minimal
else
 LIBS += /usr/lib64/libtcmalloc_minimal.a -lstdc++
endif
# tc    1,856.30 1,854.86 1,867.92
# glibc 1,872.29 1,882.45 1,855.77
# je    1,920.62 1,920.18 1,904.82

# ------------------------------------------------------------------------------

## make targets

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
	    *.profraw *.log perf.data* callgrind.out* *.d tf2sim *.so

watch:
	@while :; do\
		ls builtin.lua $(SRCS) $$(cat $(DEPS) | sed 's/^[^:]\+://;/^$$/d;s/\\//') | awk '!t[$$0]++' | entr -cds 'make||{ printf "\a";exit 1;};kill -HUP $$(pidof cfgfs) 2>/dev/null;:';\
	done

analyze:
	@set -e; \
	CC=gcc CFLAGS="-O2 -fanalyzer -flto" LDFLAGS="-O2 -fanalyzer -flto" make -B; \
	rm -f -- $(EXE) $(OBJS)

ubench: $(EXE) tf2sim
	@export CFGFS_NO_ATTENTION=1 CFGFS_NO_CLI=1 CFGFS_NO_CLICK=1 \
	        CFGFS_NO_LOGTAIL=1 CFGFS_NO_RELOADER=1; \
	sh scripts/runwith.sh ./cfgfs mnt -- hyperfine --warmup 1 --runs 20 -s basic ./tf2sim
# ^ measures tf2sim time, look at the lowest system and wall clock times

pubench: $(EXE) tf2sim
	@export CFGFS_NO_ATTENTION=1 CFGFS_NO_CLI=1 CFGFS_NO_CLICK=1 \
	        CFGFS_NO_LOGTAIL=1 CFGFS_NO_RELOADER=1; \
	sh scripts/runwith.sh perf stat ./cfgfs mnt -- sh -c 'loop 30 ./tf2sim >/dev/null'

# ------------------------------------------------------------------------------

MNTLNK := mnt

TF2MNT := ~/.local/share/Steam/steamapps/common/Team\ Fortress\ 2/tf/custom/!cfgfs/cfg
FOFMNT := ~/.local/share/Steam/steamapps/common/Fistful\ of\ Frags/fof/custom/!cfgfs/cfg

# start in game (tf2) directory
start: $(EXE)
	@set -e; \
	mount | grep -Po ' on \K(.+?)(?= type (fuse\.)cfgfs )' | xargs -n1 -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	[ -d $(TF2MNT) ] || mkdir -p $(TF2MNT); \
	ln -fs $(TF2MNT) $(MNTLNK); \
	exec ./$(EXE) -o auto_unmount $(TF2MNT); \
	#             ^^^^^^^^^^^^^^^ how to set this from C?

# start in game (fof) directory
startfof: $(EXE)
	@set -e; \
	mount | grep -Po ' on \K(.+?)(?= type (fuse\.)cfgfs )' | xargs -n1 -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	[ -d $(FOFMNT) ] || mkdir -p $(FOFMNT); \
	ln -fs $(FOFMNT) $(MNTLNK); \
	CFGFS_SCRIPT=./script_fof.lua exec ./$(EXE) -o auto_unmount $(FOFMNT)

# start it here
start2: $(EXE)
	@set -e; \
	mount | grep -Po ' on \K(.+?)(?= type (fuse\.)cfgfs )' | xargs -n1 -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	mkdir -p $(MNTLNK); \
	exec ./$(EXE) -o auto_unmount $(MNTLNK)
