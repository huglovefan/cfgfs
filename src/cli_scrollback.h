#pragma once

#include <stddef.h>

void cli_scrollback_load_and_print(void);
void cli_scrollback_add_output(char *line);
void cli_scrollback_add_input(const char *prompt, const char *text, size_t textlen);
void cli_scrollback_flush_and_free(void);
