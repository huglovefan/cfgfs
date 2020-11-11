#pragma once

#include <X11/X.h> // KeySym

struct key_list_entry {
	const char *const name;
	const KeySym xkey;
};

extern struct key_list_entry keys[];

KeySym keys_name2keysym(const char *name);
