#pragma once

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define assume(v) \
	({ \
		__auto_type _assume_v = (v); \
		if (!_assume_v) __builtin_unreachable(); \
		_assume_v; \
	})

#ifndef V
 #define V if (0)
#endif
#ifndef VV
 #define VV if (0)
#endif
#ifndef D
 #define D if (0)
#endif

// https://en.cppreference.com/w/cpp/utility/exchange
#define exchange(var, newval) \
	({ \
		__auto_type _exchange_p = &(var); \
		__auto_type _exchange_oldval = *_exchange_p; \
		*_exchange_p = (newval); \
		_exchange_oldval; \
	})

// #include <sys/prctl.h>
#define set_thread_name(s) \
	({ \
		_Static_assert(__builtin_strlen(s) <= 15, "thread name too long"); \
		prctl(PR_SET_NAME, s, NULL, NULL, NULL); \
	})

#define mono_ms() \
	({ \
		struct timespec ts; \
		clock_gettime(CLOCK_MONOTONIC, &ts); \
		((double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0); \
	})
#define wall_ms() \
	({ \
		struct timespec ts; \
		clock_gettime(CLOCK_REALTIME, &ts); \
		((double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0); \
	})

// -----------------------------------------------------------------------------

// https://gist.github.com/4d2a407a2866402b6cb9678b0c18f71c
// if using post-increment on an _Atomic(_Bool) in an if statement, wrap this
// around it like:
//   if (!clang_atomics_bug(bool++)) ...
#define clang_atomics_bug(expr) ((int)(expr))

// -----------------------------------------------------------------------------

#undef assert

// https://stackoverflow.com/a/2671100
#define STRINGIZE_DETAIL(x) #x
#define STRINGIZE(x) STRINGIZE_DETAIL(x)

#if defined(SANITIZER)
 extern void __sanitizer_print_stack_trace(void);
#else
 #define __sanitizer_print_stack_trace()
#endif

#define assert2(v, s) \
	({ \
		__auto_type _assert_v = (v); \
		if (unlikely(!_assert_v)) { \
		    fprintf(stderr, EXE ": " __FILE__ ":" STRINGIZE(__LINE__) ": %s: Assertion failed: %s\n", __func__, (s)); \
		    __sanitizer_print_stack_trace(); \
		    __builtin_abort(); \
		} \
	})

#define assert1(x) assert2(x, #x)

// https://stackoverflow.com/a/8814003
#define assertX(x, a, b, FUNC, ...) FUNC  
#define assert(...) assertX(, ##__VA_ARGS__, assert2(__VA_ARGS__), assert1(__VA_ARGS__))

#ifdef NDEBUG
 #undef assert1
 #undef assert2
 #define assert1(x) assume(x)
 #define assert2(x, s) assume(x)
#endif

// -----------------------------------------------------------------------------

// helpers for repetitive error checking

// check that the return value from a function isn't -1
// if it is, do "perror(what)" and execute the statement in "orelse" (should be goto or return)
#define check_minus1(v_, what, orelse) \
	({ \
		__auto_type _check_v = (v_); \
		if (unlikely(_check_v == -1)) { \
			eprintln(what ": %s", strerror(errno)); \
			orelse; \
			__builtin_abort(); \
		} \
		_check_v; \
	})

// same as above, but for functions that return an errno value instead of setting it globally
#define check_errcode(v_, what, orelse) \
	({ \
		__auto_type _check_v = (v_); \
		if (unlikely(_check_v != 0)) { \
			eprintln(what ": %s", strerror(_check_v)); \
			orelse; \
			__builtin_abort(); \
		} \
	})

#define check_nonnull(v_, what, orelse) \
	({ \
		__auto_type _check_v = (v_); \
		if (unlikely(_check_v == NULL)) { \
			eprintln(what ": %s", strerror(errno)); \
			orelse; \
			__builtin_abort(); \
		} \
		_check_v; \
	})
