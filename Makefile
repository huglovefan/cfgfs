#SANITIZER := -fsanitize=address,undefined
#SANITIZER := -fsanitize=thread,undefined
CC        := $(shell echo $${CC:-clang})

CFLAGS ?= -Ofast -g

STATIC_LIBFUSE := 1

# ------------------------------------------------------------------------------

EXE := $(shell echo $${PWD\#\#*/})

OBJS = src/main.o \
       src/buffer_list.o \
       src/buffers.o \
       src/lua.o \
       src/cfg.o \
       src/reloader.o \
       src/cli_input.o \
       src/cli_output.o \
       src/keys.o \
       src/click.o \
       src/attention.o
DEPS = $(OBJS:.o=.d)
SRCS = $(OBJS:.o=.c)

# make it make dependency files for make
CPPFLAGS += -MMD -MP

# be able to override functions from static libraries
LDFLAGS += -Wl,-z,muldefs

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

CPPFLAGS += -DEXE='"$(EXE)"'

# readline
CPPFLAGS += -I/usr/include/readline
LDLIBS += -lreadline

# xlib
LDLIBS += -lX11 -lXtst

# lua
# -export-dynamic: required to export symbols so that external C modules work
LDLIBS += -Wl,-export-dynamic -llua -ldl -lm

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
