#include "reloader.h"

void reloader_init(void) {}
void reloader_deinit(void) {}

_Bool reloader_add_watch(const char *path, const char **errmsg) {
	(void)path;
	*errmsg = "reloader not supported on this platform";
	return 0;
}

void reloader_reload(void) {
	reloader_do_reload_internal(NULL);
}
