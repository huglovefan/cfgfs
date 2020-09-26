#include <algorithm>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

char data[0xffff];

/*

cfgfs_getattr: /cfgfs
cfgfs_getattr: /cfgfs/keys
cfgfs_getattr: /cfgfs/keys/+100.cfg

cfgfs_open: /cfgfs/keys/+100.cfg
cfgfs_release: /cfgfs/keys/+100.cfg

cfgfs_open: /cfgfs/keys/+100.cfg
cfgfs_release: /cfgfs/keys/+100.cfg

cfgfs_open: /cfgfs/keys/+100.cfg
cfgfs_read: /cfgfs/keys/+100.cfg (size=1024, offset=0)
cfgfs_read: /cfgfs/keys/+100.cfg (size=1024, offset=14)
cfgfs_release: /cfgfs/keys/+100.cfg

i think the first 2 or 3 getattrs are cached somehow (tf2sim doesn't do that)

*/

// tf2 does basically this to read a config file

static double tdms(struct timespec t0, struct timespec t1) {
	return (t1.tv_sec-t0.tv_sec) * 1000.0 + (t1.tv_nsec-t0.tv_nsec) / 1000000.0;
}

int main() {
	int fd;
	ssize_t rv;
	struct stat st;

	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);

	const int tries = 1000;

	for (int i = 0; i < tries; i++) {

		stat("mnt/cfgfs", &st);
		stat("mnt/cfgfs/keys", &st);
		stat("mnt/cfgfs/keys/+100.cfg", &st);

		fd = open("mnt/cfgfs/keys/+100.cfg", O_RDONLY);
		if (fd == -1) return 1;
		close(fd);

		fd = open("mnt/cfgfs/keys/+100.cfg", O_RDONLY);
		close(fd);

		fd = open("mnt/cfgfs/keys/+100.cfg", O_RDONLY);
		rv = read(fd, data, 1024);
		if (rv > 0) rv = read(fd, data, 1024);
		close(fd);

	}

	struct timespec end;
	clock_gettime(CLOCK_MONOTONIC, &end);
	printf("%lfms\n", tdms(start, end)/(double)tries);

	return 0;
}
