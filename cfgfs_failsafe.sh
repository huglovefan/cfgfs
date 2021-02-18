# script used by cfgfs_run to start cfgfs. this is what's shown in the terminal

if [ $# -ne 1 ]; then
	>&2 echo "usage: cfgfs_failsafe.sh <mountpoint>"
	exit 1
fi

if [ -n "$GAMEDIR" -a -z "$GAMENAME" ]; then
	>&2 echo "warning: failed to parse game name from gameinfo.txt!"
fi

sudo_or_doas() {
	if command -v sudo >/dev/null; then
		( set -x; sudo "$@" )
	elif command -v doas >/dev/null; then
		( set -x; doas "$@" )
	else
		# print the "command not found" message i guess
		( set -x; sudo "$@" )
	fi
}

run() {
	if mount | fgrep -q " $1 "; then
		if ! out=$(LANG=C fusermount -u -- "$1" 2>&1); then
			case $out in
			*'Device or resource busy'*)
				sudo_or_doas umount --force --lazy "$1" || return
				;;
			*)
				if [ -n "$out" ]; then >&2 echo "$out"; fi
				return 1
				;;
			esac
		fi
	fi

	mkdir -p -- "$1" || return

	( . ./env.sh; exec ./cfgfs "$1" )
	rv=$?

	if [ $rv -ne 0 ]; then
		echo "cfgfs exited with status $rv"
	fi

	export CFGFS_RESTARTED=1

	# re-enable echo in case readline had disabled it
	stty echo

	return $rv
}

while ! run "$1"; do
	printf '\a'
	sleep 1 || break
done
