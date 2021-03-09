#pragma once

#include "macros.h"

#define RDSELECT_MAX_FD 4

#define rdselect(...) \
	({ \
		int argc_ = sizeof((int[]){__VA_ARGS__})/sizeof(int); \
		assert_compiler_knows(argc_ >= 1 && argc_ <= RDSELECT_MAX_FD); \
		rdselect_real(argc_, ##__VA_ARGS__); \
	})
int rdselect_real(int count, ...);

// "warning: ISO C forbids forward references to 'enum' types"
#pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
 // forward-declare "enum msg". it is expected that the file including this
 //  header file will define it and use it with writech() and readch()
 enum msg;
#pragma GCC diagnostic pop

#define writech(fd, m) writech_real(fd, (char)(m))
void writech_real(int fd, char c);

#define readch(fd) (enum msg)(readch_real(fd))
char readch_real(int fd);

//
// .pipe = {out_fd, in_fd},
//          ↓       ↑
//          read()  write()
//
