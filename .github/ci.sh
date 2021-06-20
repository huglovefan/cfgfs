#
# script for github actions to build and test cfgfs
#

luaver="5.4.3"
luadir="lua-${luaver}"
luatar="lua-${luaver}.tar.gz"

echo "== downloading and compiling lua ${luaver} =="
{ curl --no-progress-meter "https://www.lua.org/ftp/${luatar}" | tar -xz; } || exit
( cd "$luadir" && make -s MYCFLAGS="-g" ) || exit

# last release (2.3.1 from 2019) has a memory leak which makes sanitizers complain
libkqueue_commit="b46d1017486e18813b7a749ebddbd49e6f41769a"
libkqueue_dir="libkqueue-${libkqueue_commit}"

echo "== downloading and compiling libkqueue ${libkqueue_commit} =="
{ curl -L --no-progress-meter "https://github.com/mheily/libkqueue/archive/${libkqueue_commit}.tar.gz" | tar -xz; } || exit
( cd "$libkqueue_dir" && cmake . && make -s ) || exit

ln -st. "$libkqueue_dir"/libkqueue.so*
export LD_LIBRARY_PATH="$PWD"

export CFLAGS="-O2 -g -fstack-protector-strong"
export CPPFLAGS="-D_FORTIFY_SOURCE=2"

for cc in clang gcc; do
	echo "== compiling cfgfs with ${cc} =="
	make clean CFGFS_RM=rm || exit
	make \
	    CC="$cc" \
	    D=1 \
	    LUA_CFLAGS="-I${luadir}/src" \
	    LUA_LIBS="${luadir}/src/liblua.a -lm" \
	    LIBKQUEUE_CFLAGS="-I${libkqueue_dir}/include" \
	    LIBKQUEUE_LIBS="${libkqueue_dir}/libkqueue.so" \
	    SANITIZER="-fsanitize=address,undefined" \
	    WE=1 \
	    cfgfs \
	    || exit
	echo "== testing ${cc} build of cfgfs =="
	make test || exit
done
