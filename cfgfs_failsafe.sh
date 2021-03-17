# script used by cfgfs_run to start cfgfs. this is what's shown in the terminal

if [ $# -ne 1 ]; then
	>&2 echo "usage: cfgfs_failsafe.sh <mountpoint>"
	exit 1
fi

if [ -n "$CFGFS_TERMINAL_CLOSED" ]; then
	>&2 echo "warning: looks like the terminal window was closed. don't do that"
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

is_game_running() {
	case $(pstree -aAclntT -p "$CFGFS_RUN_PID") in
	*'+exec cfgfs/init'*)
		return 0;;
	*)
		return 1;;
	esac
}
is_cfgfs_run_running() {
	[ -e "/proc/$CFGFS_RUN_PID" ]
}

run() {
	# is it still mounted?
	if ! out=$(LANG=C fusermount -u -- "$1" 2>&1); then
		case $out in
		# ok: wasn't mounted
		'fusermount: entry for '*' not found in /etc/mtab')
			;;
		# bad: had crashed with a file still open in some process
		'fusermount: failed to unmount '*': Device or resource busy')
			>&2 printf '\a\n'
			>&2 echo "cfgfs: Looks like the filesystem has crashed."
			>&2 echo "cfgfs: Root privileges are required to forcibly unmount it due to some files"
			>&2 echo "cfgfs:  still being open."
			>&2 echo "cfgfs: The following command may ask for your password."
			>&2 echo ""
			if ! sudo_or_doas umount --force --lazy "$1"; then
				return 1
			fi
			;;
		# fantasy scenarios that shouldn't ever happen but are handled
		#  here anyway
		'') ;;
		*)  >&2 echo "$out";;
		esac
	fi

	mkdir -p -- "$1" || return

	trap '' INT
	( trap - INT; . ./env.sh; exec ./cfgfs "$1" )
	rv=$?
	trap - INT

	# re-enable echo in case readline had disabled it
	stty echo

	if [ $rv -ne 0 ]; then
		printf '\a'
		echo "cfgfs exited with status $rv"
	fi

	export CFGFS_RESTARTED=1

	return $rv
}

# sleeps one second between run() calls
# this also detects if cfgfs_run or the game exit during the wait (we should
#  also exit if that happens)
sleep_one_second() {
	# cfgfs_run already exited -> nothing to wait about
	if ! is_cfgfs_run_running; then
		return 1
	fi

	game_was_running=
	if is_game_running; then
		game_was_running=1
	fi

	for delay in 0.1 0.2 0.2 0.5; do
		command sleep "$delay"
		if ! is_cfgfs_run_running; then
			# cfgfs_run just exited
			return 1
		fi
		if [ -n "$game_was_running" ]; then
			if ! is_game_running; then
				# game just exited
				return 1
			fi
		else
			if is_game_running; then
				# game just started! finish the wait early
				break
			fi
		fi
	done

	return 0
}

# normal sequence of events:
# - this script is started (in a restart loop)
# - cfgfs_run waits for the filesystem to become mounted and working
# - cfgfs_run starts the game
# - the game exits
# - cfgfs_run unmounts the filesystem (in the background using &)
# - cfgfs_run exits

while is_cfgfs_run_running; do
	if run "$1"; then
		if is_game_running; then
			>&2 printf '\a'
			>&2 echo "warning: game is still running, restarting cfgfs..."
		fi
		sleep_one_second || break
	else
		sleep_one_second || break
	fi
done
