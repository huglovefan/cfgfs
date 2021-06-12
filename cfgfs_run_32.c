#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(__FreeBSD__)
 #error cfgfs_run_32 is for freebsd only
#endif

#define getenvdup(s) (getenv(s) ? strdup(getenv(s)) : NULL)

int main(int argc, char **argv) {
	(void)argc;

	char *ld_library_path = getenvdup("LD_LIBRARY_PATH");
	if (ld_library_path) {
		unsetenv("LD_LIBRARY_PATH");
		setenv("LD_LIBRARY_PATH_TMP", ld_library_path, 1);
	}

	char *ld_preload = getenvdup("LD_PRELOAD");
	if (ld_preload) {
		unsetenv("LD_PRELOAD");
		setenv("LD_PRELOAD_TMP", ld_preload, 1);
	}

	char *path = getenvdup("PATH");
	char *freebsd_path = getenvdup("FREEBSD_PATH");
	if (path && freebsd_path) {
		unsetenv("PATH");
		setenv("PATH_TMP", path, 1);

		setenv("PATH", freebsd_path, 1);
	}

#define runexe "cfgfs_run"
#define runext "/" runexe

	char *slash = strrchr(argv[0], '/');
	if (slash) {
		*slash = '\0';
		size_t bufsz = strlen(argv[0])+strlen(runext)+1;
		char *buf = malloc(bufsz);
		snprintf(buf, bufsz, "%s%s", argv[0], runext);
		argv[0] = buf;
	} else {
		argv[0] = runexe;
	}
	execvp(argv[0], argv);

	perror("exec");

	return 1;
}
