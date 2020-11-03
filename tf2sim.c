// microbenchmark

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

static const int tries = 1000;

static char data[0xffff];

static double tdms(struct timespec t0, struct timespec t1) {
	return (double)(t1.tv_sec-t0.tv_sec)*1000.0 + (double)(t1.tv_nsec-t0.tv_nsec)/1000000.0;
}

int main(void) {
	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);

	const char *path = "mnt/cfgfs/keys/+100.cfg"; // f9 if keys start from 1
	FILE *f;
	struct stat st;
	for (int i = 0; i < tries; i++) {

		f = fopen(path, "r");
		if (f == NULL) return 1;
		stat(path, &st);
		fclose(f);

		f = fopen(path, "r");
		if (f == NULL) return 1;
		stat(path, &st);
		fclose(f);

		f = fopen(path, "r");
		if (f == NULL) return 1;
		stat(path, &st);
		fread(&data, 1, sizeof(data), f);
		fclose(f);

	}

	struct timespec end;
	clock_gettime(CLOCK_MONOTONIC, &end);
	printf("%lfms\n", tdms(start, end)/(double)tries);

	return 0;
}
