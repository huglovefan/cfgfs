export CFGFS_SCRIPT=test/script.lua
export GAMENAME=test
export GAMEDIR=/var/empty
export GAMEROOT=/var/empty

fusermount -u test/mnt 2>/dev/null

rm -f test/rv || exit
mkdir -p test/mnt || exit

trap 'fusermount -u test/mnt 2>/dev/null &' EXIT
trap 'exit 1' HUP INT TERM

{
./cfgfs test/mnt
echo $? >test/rv
} &
pid=$!

#
# wait for it to start
#
i=1
while true; do
	if [ ! -d "/proc/$pid" ]; then
		exit 1
	fi
	if mountpoint -q test/mnt; then
		break
	fi
	if [ $i -gt 15 ]; then
		kill $pid
		exit 1
	elif [ $i -gt 10 ]; then
		sleep 1 || exit
	else
		command sleep 0.05
	fi
	i=$((i+1))
done

# ------------------------------------------------------------------------------

if ! { cat test/mnt/cfgfs/init.cfg | fgrep -q 'init.cfg'; }; then
	exit 10
fi

if ! { cat test/mnt/cfgfs/alias/testtest.cfg | fgrep -q '123123'; }; then
	exit 11
fi

# make sure ">" and ">>" both work

echo ping1 >test/mnt/.control || exit 121
if ! { cat test/mnt/cfgfs/buffer.cfg | fgrep -q 'pong1'; }; then
	exit 122
fi

echo ping2 >>test/mnt/.control || exit 131
if ! { cat test/mnt/cfgfs/buffer.cfg | fgrep -q 'pong2'; }; then
	exit 132
fi

# ------------------------------------------------------------------------------

fusermount -u test/mnt || exit

#
# wait for it to exit
#
i=1
while true; do
	if [ ! -d "/proc/$pid" ]; then
		break
	fi
	if [ $i -gt 15 ]; then
		kill $pid
		exit 1
	elif [ $i -gt 10 ]; then
		sleep 1 || exit
	else
		command sleep 0.05
	fi
	i=$((i+1))
done

rv=$(cat test/rv) || exit
if [ "$rv" != "0" ]; then
	exit 1
fi

echo "tests OK"

exit 0
