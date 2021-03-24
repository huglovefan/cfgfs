#pragma once

struct optstring {
	char *s;
	size_t sz;
};
static inline void *optstring_new(void) {
	struct optstring *os = malloc(sizeof(struct optstring));
	memset(os, 0, sizeof(struct optstring));
	return os;
}
static inline void optstring_append(struct optstring *os,
                                    const char *s,
                                    size_t len) {
	assert(len <= 0xff);
	os->s = realloc(os->s, os->sz+1+len);
	uint8_t len8 = (uint8_t)len;
	memcpy(os->s+os->sz, &len8, 1);
	memcpy(os->s+os->sz+1, s, len);
	os->sz += 1+len;
}
static inline char *optstring_finalize(struct optstring *os) {
	optstring_append(os, "", 0);
	char *s = os->s;
	free(os);
	return s;
}
static inline void optstring_free(struct optstring *os) {
	if (os != NULL) {
		free(os->s);
		free(os);
	}
}

static inline bool optstring_test(const char *p, const char *s, size_t len) {
	while (*p != '\0') {
		uint8_t sz = (uint8_t)*p;
		if (sz == len && 0 == memcmp(s, p+1, sz)) {
			return true;
		}
		p = p+1+sz;
	}
	return false;
}
