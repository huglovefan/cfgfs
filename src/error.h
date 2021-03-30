#pragma once

struct assertdata {
	const char *const fmt;
	const char *const func;
	const char *const expr;
};

__attribute__((cold))
void assert_fail(const struct assertdata *);

void print_c_backtrace_unlocked(void);
