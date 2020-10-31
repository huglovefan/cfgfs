// microbenchmark

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

static const int tries = 1000;

static char data[0xffff];

static double tdms(struct timespec t0, struct timespec t1) {
	return (t1.tv_sec-t0.tv_sec)*1000.0 + (t1.tv_nsec-t0.tv_nsec)/1000000.0;
}

int main(void) {
	int fd;
	ssize_t rv;
	struct stat st;

	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);

	for (int i = 0; i < tries; i++) {

		stat("mnt/cfgfs/keys/+100.cfg", &st);

		fd = open("mnt/cfgfs/keys/+100.cfg", O_RDONLY);
		if (fd == -1) return 1;
		close(fd);

		fd = open("mnt/cfgfs/keys/+100.cfg", O_RDONLY);
		if (fd == -1) return 1;
		close(fd);

		fd = open("mnt/cfgfs/keys/+100.cfg", O_RDONLY);
		rv = read(fd, data, 1024);
		if (rv > 0) rv = read(fd, data, 512);
		close(fd);

	}

	struct timespec end;
	clock_gettime(CLOCK_MONOTONIC, &end);
	printf("%lfms\n", tdms(start, end)/(double)tries);

	return 0;
}
