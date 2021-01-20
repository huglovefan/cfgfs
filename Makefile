#SANITIZER := -fsanitize=address,undefined
#SANITIZER := -fsanitize=thread,undefined
CCACHE    ?= $(shell if command -v ccache >/dev/null; then echo ccache; fi)
CC        := $(shell echo $${CC:-clang})

ifneq (,$(CCACHE))
 ifeq (,$(findstring ccache,$(CC)))
  CC := $(CCACHE) $(CC)
 endif
endif

CFLAGS ?= -O2 -g

STATIC_LIBFUSE := 1

# ------------------------------------------------------------------------------

EXE := $(shell echo $${PWD##*/})

OBJS = \
       src/main.o \
       src/buffers.o \
       src/buffer_list.o \
       src/lua.o \
       src/cli_output.o \
       src/cfg.o \
       src/click.o \
       src/pipe_io.o \
       src/attention.o \
       src/reloader.o \
       src/cli_input.o \
       src/keys.o \

DEPS = $(OBJS:.o=.d)
SRCS = $(OBJS:.o=.c)

# make it make dependency files for make
CPPFLAGS += -MMD -MP

CFLAGS += -std=gnu11
CPPFLAGS += -D_GNU_SOURCE

ifneq (,$(findstring clang,$(CC)))
CFLAGS += \
          -Weverything \
          -Werror=format \
          -Werror=implicit-function-declaration \
          -Werror=incompatible-function-pointer-types \
          -Werror=return-type \
          -Werror=sometimes-uninitialized \
          -Werror=uninitialized \
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
          -Wno-vla \
# .
else
CFLAGS += \
          -Wall \
          -Wextra \
          -Werror=implicit-function-declaration \
          -Wno-bool-operation \
          -Wno-misleading-indentation \
# .
endif

ifneq (,$(findstring clang,$(CC)))
 ifneq (,$(findstring -flto=thin,$(CFLAGS)))
  ifneq (,$(findstring lld,$(LD)))
   LDFLAGS += -Wl,--thinlto-cache-dir=.thinlto-cache
   LDFLAGS += -Wl,--thinlto-cache-policy=cache_size_bytes=500m
  endif
 endif
endif

# ------------------------------------------------------------------------------

## options

# verbosity/debug enablings

ifeq ($(VV),1)
 CPPFLAGS += -DVV="if(1)"
 # enable D and V if they're unset
 ifeq ($(D),)
  D := 1
 endif
 ifeq ($(V),)
  V := 1
 endif
endif

ifeq ($(V),1)
 CPPFLAGS += -DV="if(1)"
 # enable D if it's unset
 ifeq ($(D),)
  D := 1
 endif
endif

ifeq ($(D),1)
 CPPFLAGS += -DD="if(1)"
endif

ifeq ($(A),1)
 CFLAGS += --analyzer
 LDFLAGS += --analyzer
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

CPPFLAGS += -pthread
LDLIBS += -pthread
LDLIBS += -ldl

CPPFLAGS += -DEXE='"$(EXE)"'

# readline
CPPFLAGS += -I/usr/include/readline
LDLIBS += -lreadline

# xlib
LDLIBS += -lX11 -lXtst

# lua
LUA_CFLAGS ?= $(shell pkg-config --cflags lua5.4)
LUA_LIBS   ?= $(shell pkg-config --libs   lua5.4)
CFLAGS += $(LUA_CFLAGS)
LDLIBS += $(LUA_LIBS)

# fuse
CPPFLAGS += -I/usr/include/fuse3 -DFUSE_USE_VERSION=35
ifeq ($(STATIC_LIBFUSE),)
 LDLIBS += -lfuse3
else
 LDLIBS += /usr/lib64/libfuse3.a
endif

# ------------------------------------------------------------------------------

## make targets

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

-include $(DEPS)
.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

# ~

clean:
	@rm -fv -- $(EXE) $(OBJS) $(DEPS)

watch:
	@while ls builtin.lua $(SRCS) $$(cat $(DEPS) | sed 's/^[^:]\+://;/^$$/d;s/\\//') | awk '!t[$$0]++' | entr -cs 'make||{ rv=$$?;printf "\a";exit $$rv;};: >.cfgfs_reexec;pkill -INT cfgfs||rm .cfgfs_reexec'; do\
		continue;\
	done

install:
	@set -e; \
	for dir in /usr/local/bin ~/.local/bin ~/bin; do \
		case ":$$PATH:" in \
		*":$$dir:"*) \
			if [ -d "$$dir" -a -w "$$dir" ]; then \
				ln -fsv "$$PWD/cfgfs_run" "$$dir/"; \
				exit 0; \
			fi;; \
		esac; \
	done; \
	>&2 echo "error: no suitable install directory found in "'$$'"PATH"; \
	>&2 echo "add a directory like ~/bin to your path (google it) or try running this as root"; \
	exit 1;

.PHONY: test
test:
	@exec timeout 5 sh test/run.sh

scan:
	@scan-build --use-cc=clang make -s -B VV=1
analyze:
	@CC=gcc CFLAGS='-O2 -fdiagnostics-color -flto' LDFLAGS='-O2 -fdiagnostics-color -flto' make -B A=1 VV=1

# ------------------------------------------------------------------------------

MNTLNK := mnt

TF2MNT := ~/.local/share/Steam/steamapps/common/Team\ Fortress\ 2/tf/custom/!cfgfs/cfg

start: $(EXE)
	@set -e; \
	mount | grep -Po ' on \K(.+?)(?= type (fuse\.)?cfgfs )' | xargs -n1 -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	[ -d $(TF2MNT) ] || mkdir -p $(TF2MNT); \
	ln -fs $(TF2MNT) $(MNTLNK); \
	export CFGFS_SCRIPT=script_440.lua; \
	export GAMEDIR=~/.local/share/Steam/steamapps/common/Team\ Fortress\ 2/tf; \
	export GAMEROOT=~/.local/share/Steam/steamapps/common/Team\ Fortress\ 2; \
	export GAMENAME=Team\ Fortress\ 2; \
	exec ./$(EXE) $(CFGFS_FLAGS) "$${GAMEDIR}/custom/!cfgfs/cfg"
