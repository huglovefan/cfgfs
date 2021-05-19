#pragma once

#if defined(__linux__)
 #include <X11/X.h> // KeySym
#else
 typedef unsigned int KeyCode;
 typedef unsigned int KeySym;
#endif

struct key_list_entry {
	const char *const name;
	const KeySym xkey;
};

extern const struct key_list_entry keys[];

KeySym keys_name2keysym(const char *name);
