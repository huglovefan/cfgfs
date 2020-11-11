#!/bin/sh

# % ./runwith.sh perf stat ./cfgfs mnt -- loop 20 ./tf2sim
# note: the mount point must be "mnt"

comlen=0
auxlen=0
seen=
for arg; do
	if [ -z "$seen" -a "$arg" = "--" ]; then
		seen=1
		continue
	fi
	if [ -z "$seen" ]; then
		comlen=$((comlen+1))
	else
		auxlen=$((auxlen+1))
	fi
done
if [ $auxlen -eq 0 -o $comlen -eq 0 -o -z "$seen" ]; then
	>&2 echo "usage: runwith.sh <cfgfs command> -- <tf2sim command>"
	exit 1
fi

child() {
	i=$comlen
	for arg; do
		shift
		if [ $i -gt 0 ]
		then set -- "$@" "$arg"
		else break
		fi
		i=$((i-1))
	done
	shift $((auxlen))
	if ! command "$@"; then
		: >.error
	fi
}

fusermount -u mnt 2>/dev/null
rm -f mnt 2>/dev/null
rmdir mnt 2>/dev/null
mkdir mnt || exit

rm -f .error
child "$@" &
while ! mountpoint -q mnt; do
	if [ -e .error ]; then
		rm -f .error
		exit 1
	fi
	continue
done
rm -f .error

shift $((comlen+1))
command "$@"
rv=$?

fusermount -u mnt
wait

exit $rv
