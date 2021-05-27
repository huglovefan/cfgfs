#
# script for github actions to build and test cfgfs
#
# it doesn't have lua 5.4 so that needs to be downloaded and built here
#

luaver="5.4.3"
luadir="lua-${luaver}"
luatar="lua-${luaver}.tar.gz"

echo "== downloading and compiling lua ${luaver} =="
{ curl --no-progress-meter "https://www.lua.org/ftp/${luatar}" | tar -xz; } || exit
( cd "$luadir" && make -s MYCFLAGS="-g" ) || exit

# add lua to path
PATH="$PWD/$luadir/src:$PATH"
( cd / && lua -e 'assert(_VERSION == "Lua 5.4")' ) || exit
# ^^^^ ci machine doesn't have /var/empty

export CFLAGS="-O2 -g -fstack-protector-strong"
export CPPFLAGS="-D_FORTIFY_SOURCE=2"

for cc in clang gcc; do
	echo "== compiling cfgfs with ${cc} =="
	make clean CFGFS_RM=rm || exit
	make \
	    CC="$cc" \
	    D=1 \
	    LUA_CFLAGS="-I${luadir}/src" \
	    LUA_LIBS="${luadir}/src/liblua.a" \
	    SANITIZER="-fsanitize=address,undefined" \
	    WE=1 \
	    cfgfs \
	    || exit
	echo "== testing ${cc} build of cfgfs =="
	make test || exit
done
