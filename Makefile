#SANITIZER := -fsanitize=address,undefined
#SANITIZER := -fsanitize=thread,undefined
CC        := $(shell if [ -n "$$CC" ]; then echo $$CC; elif command -v clang >/dev/null 2>&1; then echo clang; else echo cc; fi)
CCACHE    := $(shell if command -v ccache >/dev/null 2>&1; then echo ccache; fi)

CFLAGS ?= -Ofast -g

STATIC_LIBFUSE := 1

#USE_ALLOCATOR := jemalloc
#STATIC_TCMALLOC := 1
#STATIC_JEMALLOC := 1

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
       src/attention.c

EXE := $(shell basename -- "$$(pwd)")
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

CPPFLAGS += -MMD -MP
CFLAGS += -fdiagnostics-color

# be able to override functions from static libraries
LDFLAGS += -Wl,-z,muldefs

ifneq (,$(findstring clang,$(CC)))
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
ifeq ($(USE_ALLOCATOR),jemalloc)
 ifeq ($(STATIC_JEMALLOC),)
  LIBS += -ljemalloc
 else
  LIBS += /usr/lib64/libjemalloc.a
 endif
else ifeq ($(USE_ALLOCATOR),tcmalloc)
 ifeq ($(STATIC_TCMALLOC),)
  LIBS += -ltcmalloc_minimal
 else
  #LIBS += /usr/lib64/libtcmalloc_minimal.a -lstdc++
  LIBS += /usr/lib64/libtcmalloc_minimal.a /usr/lib/gcc/x86_64-pc-linux-gnu/10.2.0/libstdc++.a
  CFLAGS += $(CXXFLAGS)
  LDFLAGS += $(CXXFLAGS)
 endif
endif

# ------------------------------------------------------------------------------

## make targets

$(EXE): $(OBJS)
	$(CCACHE) $(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

-include $(DEPS)
.c.o:
	$(CCACHE) $(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

# ~

tf2sim: tf2sim.c
	$(CCACHE) $(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ -o $@

# ~

clean:
	rm -f -- $(EXE) $(DEPS) $(OBJS) core.[0-9]* vgcore.[0-9]* *.profdata \
	    *.profraw *.log perf.data* callgrind.out* *.d tf2sim *.so plot \
	    .error

watch:
	@while ls builtin.lua $(SRCS) $$(cat $(DEPS) | sed 's/^[^:]\+://;/^$$/d;s/\\//') | awk '!t[$$0]++' | entr -cs 'make||{ rv=$$?;printf "\a";exit $$rv;};: >.cfgfs_reexec;pkill -INT cfgfs||rm .cfgfs_reexec'; do\
		continue;\
	done

analyze:
	@set -e; \
	CC=gcc CFLAGS="-O2 -fanalyzer -flto" LDFLAGS="-O2 -fanalyzer -flto" make -B; \
	rm -f -- $(EXE) $(OBJS)

# ~

ubench: $(EXE) tf2sim
	@export CFGFS_NO_ATTENTION=1 CFGFS_NO_CLI=1 CFGFS_NO_CLICK=1 \
	        CFGFS_NO_LOGTAIL=1 CFGFS_NO_RELOADER=1; \
	sh scripts/runwith.sh \
	  time ./$(EXE) $(CFGFS_FLAGS) mnt \
	  -- \
	  loop 30 ./tf2sim

pubench: $(EXE) tf2sim
	@export CFGFS_NO_ATTENTION=1 CFGFS_NO_CLI=1 CFGFS_NO_CLICK=1 \
	        CFGFS_NO_LOGTAIL=1 CFGFS_NO_RELOADER=1; \
	sh scripts/runwith.sh \
	  perf stat ./$(EXE) $(CFGFS_FLAGS) mnt \
	  -- \
	  sh -c 'loop 30 ./tf2sim >/dev/null'

plot: $(EXE) tf2sim
	@export CFGFS_NO_ATTENTION=1 CFGFS_NO_CLI=1 CFGFS_NO_CLICK=1 \
	        CFGFS_NO_LOGTAIL=1 CFGFS_NO_RELOADER=1; \
	( isolated sh scripts/runwith.sh \
	    ./$(EXE) $(CFGFS_FLAGS) mnt \
	    -- \
	    sh -c './tf2sim >/dev/null && loop 500 ./tf2sim' \
	) | dd bs=1M of=plot 2>/dev/null

graph: plot
	@awk '/^[.0-9]+ms$$/ { sub("ms", ""); print(++i, $$0); }' plot | \
	  graph -T X -y - - 0.0005 2>/dev/null

# ------------------------------------------------------------------------------

MNTLNK := mnt

TF2MNT := ~/.local/share/Steam/steamapps/common/Team\ Fortress\ 2/tf/custom/!cfgfs/cfg
FOFMNT := ~/.local/share/Steam/steamapps/common/Fistful\ of\ Frags/fof/custom/!cfgfs/cfg

# start in game (tf2) directory
start: $(EXE)
	@set -e; \
	mount | grep -Po ' on \K(.+?)(?= type (fuse\.)?cfgfs )' | xargs -n1 -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	[ -d $(TF2MNT) ] || mkdir -p $(TF2MNT); \
	ln -fs $(TF2MNT) $(MNTLNK); \
	exec ./$(EXE) $(CFGFS_FLAGS) $(TF2MNT)

# start in game (fof) directory
startfof: $(EXE)
	@set -e; \
	mount | grep -Po ' on \K(.+?)(?= type (fuse\.)?cfgfs )' | xargs -n1 -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	[ -d $(FOFMNT) ] || mkdir -p $(FOFMNT); \
	ln -fs $(FOFMNT) $(MNTLNK); \
	CFGFS_SCRIPT=./script_fof.lua exec ./$(EXE) $(CFGFS_FLAGS) $(FOFMNT)

# start it here
start2: $(EXE)
	@set -e; \
	mount | grep -Po ' on \K(.+?)(?= type (fuse\.)?cfgfs )' | xargs -n1 -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	mkdir -p $(MNTLNK); \
	exec ./$(EXE) $(CFGFS_FLAGS) $(MNTLNK)
