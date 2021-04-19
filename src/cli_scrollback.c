#include "cli_scrollback.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "cli_output.h"
#include "macros.h"

#define SCROLLBACK_MSG_FMT "=== scrollback: %.*s ===\n\n"

#define SCROLLBACK_FILE     ".cli_scrollback"
#define SCROLLBACK_FILE_TMP SCROLLBACK_FILE ".tmp"

#define LINES_MAX 128

static char **lines;
static int lines_idx;

__attribute__((minsize))
void cli_scrollback_load_and_print(void) {
	int quiet = 0;

	if (getenv("CFGFS_NO_SCROLLBACK")) return;

	// not a freshly launched cfgfs terminal?
	// don't need to print any old scrollback in that case, but we still
	//  need to read it so that we can write it to the file again later
	quiet |= (uintptr_t)(getenv("CFGFS_RESTARTED"));
	quiet |= (uintptr_t)(getenv("CFGFS_TERMINAL_CLOSED"));

	FILE *f = fopen(SCROLLBACK_FILE, "r");
	if (f == NULL) {
		return;
	}

	char *line;
	size_t n_unused_but_required;
	ssize_t len;
	while ((void)(line = NULL), -1 != (len = getline(&line, &n_unused_but_required, f))) {
		if (len != 0 && line[len-1] == '\n') line[--len] = '\0';
		cli_scrollback_add_output(line);
		if (quiet) continue;
		fprintf(stderr, "%s\n", line);
	}

	// did read something? (line is set in that case)
	if (line != NULL) {
		free(line);

		if (quiet) goto out_have_file;

		struct stat stbuf;
		if (-1 == fstat(fileno(f), &stbuf)) {
			goto out_have_file;
		}

		// note: this is printf'd using "%.*s" and a length of 24 to
		//  ignore the newline at the end. the ctime string is known to
		//  always be 25 characters long (at least until year 10000)
		//  and it doesn't depend on the locale.
		char *ds = ctime(&stbuf.st_mtime);
D		assert(strlen(ds) == 25);
		fprintf(stderr, SCROLLBACK_MSG_FMT, 24, ds);
	}
out_have_file:
	fclose(f);
}

#define CHECK_LINES() \
	({ if (unlikely(!lines)) lines = calloc(LINES_MAX, sizeof(char *)); })

void cli_scrollback_add_output(char *line) {
	CHECK_LINES();
	free(lines[lines_idx]);
	lines[lines_idx] = line;
	lines_idx = (lines_idx+1) % LINES_MAX;
}

__attribute__((minsize))
void cli_scrollback_add_input(const char *prompt, const char *text) {
	char *line;
	if (-1 != asprintf(&line, "%s%s", prompt, text)) {
		cli_scrollback_add_output(line);
	}
}

#if defined(SANITIZER) || defined(WITH_D)
 #define actually_free 1
 #define MAYBE_FREE(x) free(x)
#else
 // waste of space when it's freed on exit anyway
 #define actually_free 0
 #define MAYBE_FREE(x) ((void)(x))
#endif

__attribute__((minsize))
void cli_scrollback_flush_and_free(void) {
	if (lines == NULL) return;
	if (getenv("CFGFS_NO_SCROLLBACK")) goto just_free;
	FILE *outfile = fopen(SCROLLBACK_FILE_TMP, "w");
	if (outfile == NULL) goto just_free;
	for (int i = 0; i < LINES_MAX; i++) {
		char *line = lines[(lines_idx+i) % LINES_MAX];
		if (line == NULL) continue;
		fprintf(outfile, "%s\n", line);
		MAYBE_FREE(line);
	}
	fclose(outfile);
	rename(SCROLLBACK_FILE_TMP, SCROLLBACK_FILE);
free_out:
	if (!actually_free) return;
	MAYBE_FREE(lines);
	lines = NULL;
	return;
just_free:
	for (int i = 0; i < LINES_MAX; i++) MAYBE_FREE(lines[i]);
	goto free_out;
}
