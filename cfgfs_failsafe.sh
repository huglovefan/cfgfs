#!/bin/sh

# script used by cfgfs_run to start cfgfs. this is what's shown in the terminal

run() {
	if mountpoint -q -- "$1"; then
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
	printf '\a'
	sleep 1 || break
done
