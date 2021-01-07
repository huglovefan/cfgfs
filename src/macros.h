#pragma once

#ifndef V
 #define V if (0)
#endif
#ifndef VV
 #define VV if (0)
#endif
#ifndef D
 #define D if (0)
#endif

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define assume(v) \
	({ \
		__auto_type _assume_v = (v); \
		D assert(_assume_v, "bad assume()"); \
		if (!_assume_v) __builtin_unreachable(); \
		_assume_v; \
	})

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
		struct timespec _mono_ms_ts; \
		clock_gettime(CLOCK_MONOTONIC, &_mono_ms_ts); \
		ts2ms(_mono_ms_ts); \
	})
#define wall_ms() \
	({ \
		struct timespec _wall_ms_ts; \
		clock_gettime(CLOCK_REALTIME, &_wall_ms_ts); \
		ts2ms(_wall_ms_ts); \
	})

#define ts2ms(ts) \
	((double)(ts).tv_sec * 1000.0 + (double)(ts).tv_nsec / 1000000.0)

#define ms2ts(ts, ms) \
	({ \
		(ts).tv_sec = (time_t)((ms)/1000.0); \
		(ts).tv_nsec = (long)(fmod((ms),1000.0)*1000000.0); \
	})

#define UNREACHABLE() \
	({ \
		D cfgfs_assert_fail(EXE ": " __FILE__ ":" STRINGIZE(__LINE__) ": %s: Unreachable code hit\n", __func__); \
		__builtin_unreachable(); \
	})

// assert that the compiler/optimizer can figure out "cond" is always true
// if it is true, the call to the nonexistent function will be optimized away and won't cause an error at link time
#define assert_known(cond) \
	({ \
		if (!(cond)) __constant_assert_failed(); \
	})
__attribute__((noreturn))
extern void __constant_assert_failed(void);

#define assert_known_unreachable() assert_known(0)

// -----------------------------------------------------------------------------

#undef assert

// https://stackoverflow.com/a/2671100
#define STRINGIZE_DETAIL(x) #x
#define STRINGIZE(x) STRINGIZE_DETAIL(x)

// defined in cli_output.c
__attribute__((cold))
__attribute__((format(printf, 1, 2)))
__attribute__((noreturn))
void cfgfs_assert_fail(const char *fmt, ...);

#define assert2(v, s) \
	({ \
		if (unlikely(!(v))) { \
			cfgfs_assert_fail(EXE ": " __FILE__ ":" STRINGIZE(__LINE__) ": %s: Assertion failed: %s\n", __func__, (s)); \
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
#define check_minus1(v, what, orelse) \
	({ \
		__auto_type _check_v = (v); \
		if (unlikely(_check_v == -1)) { \
			perror(what); \
			orelse; \
			__builtin_abort(); \
		} \
		_check_v; \
	})

// same as above, but for functions that return an errno value instead of setting it globally
#define check_errcode(v, what, orelse) \
	({ \
		__auto_type _check_v = (v); \
		if (unlikely(_check_v != 0)) { \
			eprintln(what ": %s", strerror(_check_v)); \
			orelse; \
			__builtin_abort(); \
		} \
	})

#define check_nonnull(v, what, orelse) \
	({ \
		__auto_type _check_v = (v); \
		if (unlikely(_check_v == NULL)) { \
			perror(what); \
			orelse; \
			__builtin_abort(); \
		} \
		_check_v; \
	})
