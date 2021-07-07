export CFGFS_DIR="$PWD"
export CFGFS_MOUNTPOINT="$PWD/test/mnt"
export CFGFS_SCRIPT="$PWD/test/script.lua"
export GAMENAME="Team Fortress 2"
export GAMEDIR=/var/empty
export MODNAME=tf
export SteamAppId=440 STEAMAPPID=440
export CFGFS_NO_SCROLLBACK=1

osname=$(uname -o)

v() {
	>&2 echo ">>> $*"
	"$@"
	>&2 echo "<<< $*"
}

case $osname in
*Linux*)
	prepare_mnt() {
		[ -e test/mnt ] || mkdir -p test/mnt
	}
	do_unmount() {
		fusermount -u test/mnt
	}
	;;
*FreeBSD*)
	prepare_mnt() {
		[ -e test/mnt ] || mkdir -p test/mnt
	}
	do_unmount() {
		umount test/mnt
	}
	;;
*Cygwin*|*Msys*)
	prepare_mnt() {
		[ ! -e test/mnt ] || rmdir test/mnt
	}
	do_unmount() {
		kill $(ps -ef | awk '$6~/\/cfgfs(\.exe)?$/{print $2}')
	}
	;;
esac

wait_mounted() {
	while ! sh -c 'exec < test/mnt/cfgfs/buffer.cfg' 2>/dev/null; do
		env sleep 0.5
	done
}
wait_unmounted() {
	while [ ! -e test/rv ]; do
		env sleep 0.5
	done
}

v prepare_mnt

trap 'do_unmount 2>/dev/null' EXIT
trap 'exit 1' HUP INT TERM

rm -f test/rv
{
./cfgfs test/mnt
echo $? >test/rv
} &
pid=$!

v wait_mounted

# ------------------------------------------------------------------------------

if ! { cat test/mnt/cfgfs/init.cfg | fgrep -q 'init.cfg'; }; then
	exit 10
fi

if ! { cat test/mnt/cfgfs/alias/testtest.cfg | fgrep -q '123123'; }; then
	exit 11
fi

# make sure ">" and ">>" both work

echo ping1 >test/mnt/message/ping.1 || exit 121
if ! { cat test/mnt/cfgfs/buffer.cfg | fgrep -q 'pong1'; }; then
	exit 122
fi

echo ping2 >>test/mnt/message/ping.2 || exit 131
if ! { cat test/mnt/cfgfs/buffer.cfg | fgrep -q 'pong2'; }; then
	exit 132
fi

# ------------------------------------------------------------------------------

v do_unmount

v wait_unmounted

rv=$(cat test/rv) || exit
rm -f test/rv
if [ "$rv" != "0" ]; then
	exit 1
fi

echo "tests OK"

exit 0
