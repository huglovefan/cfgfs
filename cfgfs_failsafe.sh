# script used by cfgfs_run to start cfgfs. this is what's shown in the terminal

if [ $# -ne 1 ]; then
	>&2 echo "usage: cfgfs_failsafe.sh <mountpoint>"
	exit 1
fi

if [ -n "$GAMEDIR" -a -z "$GAMENAME" ]; then
	>&2 echo "warning: failed to parse game name from gameinfo.txt!"
fi

run() {
	if mount | fgrep -q " $1 "; then
		fusermount -u -- "$1" || return
	fi

	mkdir -p -- "$1" || return

	./cfgfs "$1"
	rv=$?

	if [ $rv -ne 0 ]; then
		echo "cfgfs exited with status $rv"
	fi

	return $rv
}

while ! run "$1"; do
	export CFGFS_RESTARTED=1
	printf '\a'
	sleep 1 || break
done
