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

#STATIC_LIBFUSE := 1

ifneq (,$(findstring Cygwin,$(shell uname -o)))
 IS_CYGWIN := 1
 EXEEXT := .exe
 LUA_CFLAGS ?= -Ilua-5.4.3/src
 LUA_LIBS ?= lua-5.4.3/src/liblua.a
else
 CPPFLAGS += -DCFGFS_HAVE_ATTENTION
endif

# ------------------------------------------------------------------------------

EXE := $(shell basename -- "$$PWD")$(EXEEXT)

HOT_OBJS = \
       src/main.o \
       src/buffer_list.o \
       src/lua/state.o \
       src/cfg.o \
       src/lua/builtins.o \
       src/cli_output.o \
       src/click.o \
       src/buffers.o \
       src/cli_scrollback.o \

COLD_OBJS = \
       src/lua/rcon.o \
       src/rcon/session.o \
       src/rcon/srcrcon.o \
       src/pipe_io.o \
       src/attention.o \
       src/misc/string.o \
       src/reloader.o \
       src/cli_input.o \
       src/misc/caretesc.o \
       src/keys.o \
       src/lua/init.o \
       src/xlib.o \
       src/error.o \

ifneq (,$(IS_CYGWIN))
 COLD_OBJS := $(filter-out src/attention.o,$(COLD_OBJS))
 COLD_OBJS := $(filter-out src/xlib.o,$(COLD_OBJS))
endif

OBJS = $(HOT_OBJS) $(COLD_OBJS)
DEPS = $(OBJS:.o=.d)
SRCS = $(OBJS:.o=.c)

$(COLD_OBJS): CFLAGS += $(SIZE_OPT_CFLAGS)

CFLAGS += -std=gnu11
CPPFLAGS += -D_GNU_SOURCE

# ------------------------------------------------------------------------------

## compiler-specific flags

# clang
ifneq (,$(findstring clang,$(CC)))
CPPFLAGS += -MMD -MP
SIZE_OPT_CFLAGS = -Oz
CFLAGS += \
          -Weverything \
          -Werror=conditional-uninitialized \
          -Werror=format \
          -Werror=fortify-source \
          -Werror=implicit-function-declaration \
          -Werror=incompatible-function-pointer-types \
          -Werror=incompatible-pointer-types \
          -Werror=int-conversion \
          -Werror=return-type \
          -Werror=sometimes-uninitialized \
          -Werror=uninitialized \
          -Wno-atomic-implicit-seq-cst \
          -Wno-c++98-compat \
          -Wno-disabled-macro-expansion \
          -Wno-error=unknown-warning-option \
          -Wno-format-nonliteral \
          -Wno-gnu-auto-type \
          -Wno-gnu-binary-literal \
          -Wno-gnu-conditional-omitted-operand \
          -Wno-gnu-statement-expression \
          -Wno-gnu-zero-variadic-macro-arguments \
          -Wno-language-extension-token \
          -Wno-padded \
          -Wno-reserved-id-macro \
          -Wno-string-compare \
          -Wframe-larger-than=1024 \
# .
endif

# tcc
ifneq (,$(findstring tcc,$(CC)))
LDFLAGS += -rdynamic # keep tcc_backtrace() even if no direct calls
CFLAGS += \
          -Wall \
# .
endif

# gcc
ifneq (,$(findstring gcc,$(CC)))
CPPFLAGS += -MMD -MP
SIZE_OPT_CFLAGS = -Os
CFLAGS += \
          -Wall \
          -Wextra \
          -Wmissing-prototypes \
          -Wstrict-overflow=5 \
          -Werror=implicit-function-declaration \
          -Werror=incompatible-pointer-types \
          -Wno-address \
          -Wno-bool-operation \
          -Wno-format-zero-length \
          -Wno-misleading-indentation \
# .
endif

# set thinlto-cache-dir if using clang + lld + -flto=thin
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

# VV=1: very verbose messages
ifeq ($(VV),1)
 CPPFLAGS += -DVV="if(1)" -DWITH_VV
 # enable D and V if they're unset
 ifeq ($(D),)
  D := 1
 endif
 ifeq ($(V),)
  V := 1
 endif
endif

# V=1: verbose messages
ifeq ($(V),1)
 CPPFLAGS += -DV="if(1)" -DWITH_V
 # enable D if it's unset
 ifeq ($(D),)
  D := 1
 endif
endif

# D=1: debug checks
ifeq ($(D),1)
 CPPFLAGS += -DD="if(1)" -DWITH_D
endif

# WE=1: enable -Werror
ifeq ($(WE),1)
 CFLAGS += -Werror
 ifeq (,$(findstring clang,$(CC)))
  # ignore errors about -Wattributes if using gcc
  # > warning: always_inline function might not be inlinable
  # > warning: minsize attribute directive ignored
  CFLAGS += -Wno-error=attributes
 endif
endif

ifneq ($(REPORTED_CFG_SIZE),)
 CFLAGS += -DREPORTED_CFG_SIZE="$(REPORTED_CFG_SIZE)"
endif

# gcc analyzer
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
 CFLAGS   += $(SANITIZER)
 LDFLAGS  += $(SANITIZER)
 ifneq ($(WE),)
  CFLAGS += -Wno-error=frame-larger-than=
 endif
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
LDLIBS += -lm

CPPFLAGS += -DEXE='"$(EXE)"'

# readline
CPPFLAGS += -I/usr/include/readline
LDLIBS += -lreadline

# xlib (linux only)
ifeq (,$(IS_CYGWIN))
 LDLIBS += -lX11 -lXtst
endif

# lua
LUA_PKG    ?= lua5.4
LUA_CFLAGS ?= $(shell pkg-config --cflags $(LUA_PKG))
LUA_LIBS   ?= $(shell pkg-config --libs   $(LUA_PKG))
CFLAGS += $(LUA_CFLAGS)
LDLIBS += $(LUA_LIBS)

# fuse
CPPFLAGS += -DFUSE_USE_VERSION=35
CFLAGS += $(shell pkg-config --cflags fuse3)
ifeq ($(STATIC_LIBFUSE),)
 LDLIBS += $(shell pkg-config --libs fuse3)
else
 LDLIBS += -l:libfuse3.a
endif

# src/rcon/srcrcon.c (arc4random_uniform)
ifeq (,$(IS_CYGWIN))
 LDLIBS += -lbsd
endif

# ------------------------------------------------------------------------------

## make targets

all: $(EXE) cfgfs_run$(EXEEXT)

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

-include $(DEPS)
.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

# ~

DMD ?= dmd
DFLAGS ?= -g -wi

CFGFS_RUN_EXE := cfgfs_run$(EXEEXT)
ifneq (,$(IS_CYGWIN))
 DFLAGS += -L-Subsystem:Windows
endif

# unset CC if using tcc to prevent "tcc: error: invalid option -- '-Xlinker'"
$(CFGFS_RUN_EXE): cfgfs_run.d
	case "$$CC" in *tcc*) unset CC; esac; $(DMD) $(DFLAGS) $^ -of=$@

# ~

CFGFS_RM ?= rm -v
clean:
	@$(CFGFS_RM) -f -- $(EXE) $(CFGFS_RUN_EXE) $(OBJS) $(DEPS)

watch:
	@while ls builtin.lua $(SRCS) $$(cat $(DEPS) | sed 's/^[^:]\+://;/^$$/d;s/\\//') | awk '!t[$$0]++' | entr -cs 'make||{ rv=$$?;printf "\a";exit $$rv;};: >/tmp/.cfgfs_reexec;pkill -INT cfgfs||rm /tmp/.cfgfs_reexec;make -s postbuild'; do\
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

# ~

# print details about cfgfs.exe
# called by the watch target after a successful build
postbuild:
	make -s buildinfo;\
	make -s .fndata;\
	make -s compare_fndata;

buildinfo:
# total counts of functions and instructions
	@objdump -d --no-addresses --no-show-raw-insn $(EXE) | awk '\
	    /^<.*>:$$/ { if (p=$$0!~/@plt/) fns++; next; }\
	    /^$$|^Disa/ { next; }\
	    p&&$$1!="int3" { isns++; }\
	    END { printf("|   fns: %d\n|  isns: %d\n", fns, isns); }\
	'
# crc32 of function and instruction names (to tell if a code change had any effect at all)
# can't include any instruction operands because they usually contain addresses which aren't stable
	@crcnum=$$(objdump -d --no-addresses --no-show-raw-insn $(EXE) | \
	    awk ' \
	        /^<.*>:$$/ {p=$$0!~/@plt/;print;next} \
	        /^$$|^Disa/ {next} \
	        p {print$$1} \
	    ' | cksum);\
	printf '| crc32: %08x (function names and instructions without their operands)\n' "$${crcnum%% *}"
# sizes of code and data sections
	@if awk --version 2>/dev/null | grep -q '^GNU Awk'; then gawk_sucks='--non-decimal-data'; fi;\
	llvm-readelf -S $(EXE) | awk $$gawk_sucks '\
	    $$2~/^\./{s[$$2]=$$6}\
	    END {\
	        printf("|  code: %db\n",int("0x" s[".text"]));\
	        printf("@\n");\
	        printf("|   data: %db (initialized read-write data)\n",int("0x" s[".data"]));\
	        printf("| rodata: %db (initialized read-only data)\n",int("0x" s[".rodata"]));\
	        printf("|    bss: %db (zero-initialized read-write data)\n",int("0x" s[".bss"]));\
	    }\
	'

# count the size in bytes of each function and write the counts to a file
.fndata: cfgfs
	@mv -f .fndata .fndata.old 2>/dev/null; \
	objdump --disassemble --no-addresses --section=.text $(EXE) | \
	    awk ' \
	        /^<.*>:$$/ { fn=substr($$0,2,length($$0)-3); next; } \
	        !fn || $$1=="cc"&&$$2=="int3" { next; } \
	        /^\t/ { \
	            s = match($$0, /^([\t ][0-9a-f][0-9a-f])+/); \
	            if (s != 0) cnt[fn] += length(substr($$0, s, RLENGTH))/3; \
	        } \
	        END { for (fn in cnt) print(fn, cnt[fn]); } \
	    ' | \
	    sort >.fndata

# print changed functions between .fndata.old and .fndata
compare_fndata:
	@if ! [ -f .fndata -a -f .fndata.old ]; then exit 0; fi;\
	diff -u .fndata.old .fndata | \
	    awk ' \
	        NR<=2 || !/^[+-]/ { next; } \
	        { fn=substr($$1, 2); fns[fn]=1; } \
	        /^-/ { old[fn]=$$2; next; } \
	        /^\+/ { new[fn]=$$2; next; } \
	        END { \
	            total = 0; changed = 0; \
	            for (fn in fns) { \
	                printf("%s: %db -> %db\n", fn, old[fn], new[fn]); \
	                total += int(new[fn])-int(old[fn]); \
	                if (int(new[fn]) != int(old[fn])) changed = 1; \
	            } \
	            if (changed) printf("total %s%db\n", total>=0 ?"+":"",total); \
	        } \
	    '

# ~

.PHONY: test
test:
	@exec timeout 5 sh test/run.sh
testbuild:
	@exec timeout 60 sh test/build.sh

scan:
	@scan-build --use-cc=clang make -Bs VV=1
analyze:
	@CC=gcc CFLAGS='-O2 -fdiagnostics-color -flto' LDFLAGS='-O2 -fdiagnostics-color -flto' make -Bs A=1 VV=1

# ------------------------------------------------------------------------------

MNTLNK := mnt

ifeq (,$(IS_CYGWIN))
 STEAMDIR := ~/.local/share/Steam
else
 STEAMDIR := /cygdrive/c/Program\ Files\ \(x86\)/Steam
endif

TF2MNT := $(STEAMDIR)/steamapps/common/Team\ Fortress\ 2/tf/custom/!cfgfs/cfg

start: $(EXE)
	@set -e; \
	mount | grep -Po ' on \K(.+?)(?= type (fuse\.)?cfgfs )' | xargs -n1 -rd'\n' fusermount -u; \
	[ ! -L $(MNTLNK) ] || rm $(MNTLNK); \
	[ ! -d $(MNTLNK) ] || rmdir $(MNTLNK); \
	[ -d $(TF2MNT) ] || mkdir -p $(TF2MNT); \
	ln -fs $(TF2MNT) $(MNTLNK); \
	export CFGFS_DIR=$$PWD; \
	export CFGFS_MOUNTPOINT=$$PWD/mnt; \
	export CFGFS_NO_SCROLLBACK=1; \
	export GAMEDIR=$(STEAMDIR)/steamapps/common/Team\ Fortress\ 2/tf; \
	export GAMEROOT=$(STEAMDIR)/steamapps/common/Team\ Fortress\ 2; \
	export GAMENAME=Team\ Fortress\ 2; \
	export NO_LISTUPDATE=1; \
	export SteamAppId=440; \
	[ ! -e env.sh ] || . ./env.sh; \
	exec "$$CFGFS_DIR"/$(EXE) $(CFGFS_FLAGS) "$${GAMEDIR}/custom/!cfgfs/cfg"

startcyg:
	@set -e; \
	export CFGFS_DIR=$$PWD; \
	export CFGFS_MOUNTPOINT=$(TF2MNT); \
	export CFGFS_NO_SCROLLBACK=1; \
	export GAMEDIR=$(STEAMDIR)/steamapps/common/Team\ Fortress\ 2/tf; \
	export GAMEROOT=$(STEAMDIR)/steamapps/common/Team\ Fortress\ 2; \
	export GAMENAME=Team\ Fortress\ 2; \
	export SteamAppId=440; \
	[ ! -e env.sh ] || . ./env.sh; \
	exec "$$CFGFS_DIR"/$(EXE) $(TF2MNT)
