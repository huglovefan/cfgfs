#pragma once

#include <stddef.h>

// escapes control characters like when typed to the terminal
// example: ctrl+v + enter -> ^M (escaped \n)
// see https://en.wikipedia.org/wiki/Caret_notation

// buf: buffer that can hold a string at least twice the length of s
// return value: how many characters were written to buf (excluding null byte)
size_t caretesc(const char *restrict s, char *buf);
