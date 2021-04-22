unset CC CFLAGS CPPFLAGS LDFLAGS
works=
broken=
untested=
for cc in clang gcc tcc; do
	if ! command -v "$cc" >/dev/null; then
		>&2 echo "skipping tests for cc $cc"
		untested="$untested $cc"
		continue
	fi
	for lua_pkg in lua5.4; do
		if ! pkg-config --libs "$lua_pkg" >/dev/null 2>&1; then
			>&2 echo "skipping tests for lua_pkg $lua_pkg"
			untested="$untested $lua_pkg"
			continue
		fi
		echo "--- $cc+$lua_pkg  ---"
		make clean CFGFS_RM=rm || exit
		if CC=$cc make -s D=1 WE=1 LUA_PKG="$lua_pkg" && make test; then
			works="$works $cc+$lua_pkg"
		else
			broken="$broken $cc+$lua_pkg"
		fi
	done
done
make clean CFGFS_RM=rm

echo "---"

echo "works:"
printf '* %s\n' ${works:-(none)}

echo "broken:"
printf '* %s\n' ${broken:-(none)}

echo "not installed:"
printf '* %s\n' ${untested:-(none)}
