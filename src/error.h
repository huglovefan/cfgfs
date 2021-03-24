#pragma once

struct assertdata {
	const char *fmt;
	const char *func;
	const char *expr;
};

__attribute__((cold))
void assert_fail(const struct assertdata *);
