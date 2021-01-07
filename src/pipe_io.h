#include "macros.h"

#define rdselect(...) \
	({ \
		int argc_ = sizeof((int[]){__VA_ARGS__})/sizeof(int); \
		assert_known(argc_ >= 1 && argc_ <= 126); \
		int rv_ = rdselect_real(argc_, ##__VA_ARGS__); \
		assume(rv_ >= -argc_); \
		assume(rv_ <= argc_); \
		rv_; \
	})
int rdselect_real(int count, ...);

// "warning: ISO C forbids forward references to 'enum' types"
#pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
 enum msg;
#pragma GCC diagnostic pop

#define writech(fd, m) \
	({ \
		enum msg m_ = (m); \
		assert_known(m_ > 0); \
		char c_ = (char)(m_); \
		while (unlikely(1 != write(fd, &c_, 1))) continue; \
	})

#define readch(fd) \
	({ \
		char c_; \
		while (unlikely(1 != read(fd, &c_, 1))) continue; \
		(enum msg)c_; \
	})

//
// .pipe = {out_fd, in_fd},
//          ↓       ↑
//          read()  write()
//
