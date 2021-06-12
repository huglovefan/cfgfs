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

// -----------------------------------------------------------------------------

// tcc support corner

#if defined(__TINYC__)
 #define __builtin_unreachable() do asm("hlt"); while (1)
#endif

#if defined(__TINYC__)
 #define cfgfs_noreturn() __builtin_unreachable()
#else
 #define cfgfs_noreturn() compiler_enforced_unreachable()
#endif

// the provided _Static_assert() doesn't work outside functions
#if defined(__TINYC__)
 #undef _Static_assert
 // https://stackoverflow.com/a/1667564
 #define MERGE_(a,b)  a##b
 #define LABEL_(a) MERGE_(unique_name_, a)
 #define _Static_assert(c, s) struct LABEL_(__LINE__) { int static_assert_failed : (c)?1:-1; }
#endif

// -----------------------------------------------------------------------------

// https://en.cppreference.com/w/cpp/utility/exchange
#define exchange(var, newval) \
	({ \
		typeof(var) _exchange_oldval = var; \
		var = newval; \
		_exchange_oldval; \
	})

#define set_thread_name(buf) \
	do { \
		assert(strlen(buf) <= 15, "thread name too long"); \
		pthread_setname_np(pthread_self(), (buf)); \
	} while (0)

#define get_thread_name(buf) \
	do { \
		_Static_assert(sizeof(buf) >= 16, "buffer too small"); \
		pthread_getname_np(pthread_self(), (buf), 16); \
	} while (0)

// get milliseconds since the start of the program
#define mono_ms() \
	({ \
		struct timespec _mono_ms_ts; \
		clock_gettime(CLOCK_MONOTONIC, &_mono_ms_ts); \
		ts2ms(_mono_ms_ts); \
	})

// get milliseconds from a virtual wall clock
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

// -----------------------------------------------------------------------------

// heavy duty optimization hints
// danger zone: these can cause wrong code to be generated if used incorrectly
// for example, a switch statement can execute a random branch for an unhandled
//  value if the default case was marked as unreachable using unsafe_unreachable()
// the conditions are checked when cfgfs is built with D=1 but not otherwise

// optimizer may assume that this condition is always true
// do not use
#define unsafe_optimization_hint(v) \
	({ \
		typeof(v) _assume_v = (v); \
		if (unlikely(!_assume_v)) { \
			D ASSERT_FAIL_WITH_FMT("Wrong unsafe_optimization_hint(): %s", #v); \
			__builtin_unreachable(); \
		} \
		_assume_v; \
	})

// optimizer may assume that this code path is never reached
// do not use
#define unsafe_unreachable() \
	({ \
		D ASSERT_FAIL_NO_FMT("Wrong unsafe_unreachable()"); \
		__builtin_unreachable(); \
	})

// -----------------------------------------------------------------------------

// assert that the compiler knows "cond" will always evaluate to true
// if it does, then the call to the nonexistent function will be optimized away
//  and won't cause a link error
// note: using weird compiler flags like -fexceptions can make this always fail
#define assert_compiler_knows(cond) \
	({ \
		if (!(cond)) ERROR_this_call_to_AssertCompilerKnows_could_not_be_proven_true(); \
	})

__attribute__((noreturn))
extern void ERROR_this_call_to_AssertCompilerKnows_could_not_be_proven_true(void);

// tcc doesn't know much
// just make it a real assert. better than not checking it at all
#if defined(__TINYC__)
 #undef assert_compiler_knows
 #define assert_compiler_knows(x) assert(x)
#endif

// -----------------------------------------------------------------------------

// assert that the compiler knows this line of code will never be executed
// uses the same trick as assert_compiler_knows() above
#define compiler_enforced_unreachable() \
	ERROR_this_call_to_CompilerEnforcedUnreachable_may_be_reachable()

__attribute__((noreturn))
extern void ERROR_this_call_to_CompilerEnforcedUnreachable_may_be_reachable(void);

// -----------------------------------------------------------------------------

// tool for assuring control flow
// if a function uses one_true_entry(), then from that point on, all paths out
//  of the function must pass through a one_true_exit()
// this way you can be sure there are no hidden returns or unexpected jumps
//  hiding in that code

#if !defined(SANITIZER)

#define one_true_entry() \
	__attribute__((cleanup(check_otx))) \
	int otx = 0

#define one_true_exit() \
	otx++

__attribute__((noreturn))
extern void missing_call_to_one_true_exit(void);

static inline void check_otx(const int *v) {
	if (*v != 1) missing_call_to_one_true_exit();
}

#else

// doesn't seem to work in gcc sanitizer builds

#define one_true_entry() ((void)0)
#define one_true_exit() ((void)0)

#endif

// -----------------------------------------------------------------------------

#define assert_unreachable() ASSERT_FAIL_NO_FMT("Unreachable code hit")

// -----------------------------------------------------------------------------

// make the compiler check that a format string and the arguments match
// note: fmt must be a string literal for this to work

#define CHECK_FMT_STR(fmt, ...) check_format_string("" fmt, ##__VA_ARGS__)

__attribute__((format(printf, 1, 2)))
static inline void check_format_string(const char *fmt, ...) {
	(void)fmt;
}

// -----------------------------------------------------------------------------

#include "error.h"

#undef assert

// https://stackoverflow.com/a/2671100
#define STRINGIZE_DETAIL(x) #x
#define STRINGIZE(x) STRINGIZE_DETAIL(x)

#define MAKE_ASSERTDATA(ident, fmt_, func_, expr_) \
	static const struct assertdata ident = { \
		.fmt = fmt_, \
		.func = func_, \
		.expr = "" expr_ "", \
	}; \
	CHECK_FMT_STR(fmt_, func_, expr_)

#define ASSERT_FAIL_NO_FMT(s) ASSERT_FAIL_WITH_FMT("%s", s)
#define ASSERT_FAIL_WITH_FMT(fmt, s) \
	do { \
		MAKE_ASSERTDATA(assertdata, \
		    EXE ": " __FILE__ ":" STRINGIZE(__LINE__) ": %s: " fmt "\n", \
		    __func__, \
		    s); \
		assert_fail(&assertdata); \
		asm("hlt"); \
		__builtin_unreachable(); \
	} while (0)
// ^ hlt: make cutter's graph view understand that control flow ends here
// otherwise it would fall through into whatever code comes after

#define assert2(v, s) \
	({ \
		if (unlikely(!(v))) { \
			ASSERT_FAIL_WITH_FMT("Assertion failed: %s", s); \
			compiler_enforced_unreachable(); \
		} \
	})

#define assert1(x) assert2(x, #x)

// https://stackoverflow.com/a/8814003
#define assertX(x, a, b, FUNC, ...) FUNC  
#define assert(...) assertX(, ##__VA_ARGS__, assert2(__VA_ARGS__), assert1(__VA_ARGS__))

#ifdef NDEBUG
 #undef assert1
 #undef assert2
 #define assert1(x) unsafe_optimization_hint(x)
 #define assert2(x, s) unsafe_optimization_hint(x)
#endif

// -----------------------------------------------------------------------------

// helpers for repetitive error checking

// check that the return value from a function isn't -1
// if it is, do "perror(what)" and execute the statement in "orelse" (should be goto or return)
#define check_minus1(v, what, orelse) \
	({ \
		typeof(v) _check_v = (v); \
		if (unlikely(_check_v == -1)) { \
			perror(what); \
			orelse; \
			compiler_enforced_unreachable(); \
		} \
		_check_v; \
	})

// same as above, but for functions that return an errno value instead of setting it globally
#define check_errcode(v, what, orelse) \
	({ \
		typeof(v) _check_v = (v); \
		if (unlikely(_check_v != 0)) { \
			eprintln(what ": %s", strerror(_check_v)); \
			orelse; \
			compiler_enforced_unreachable(); \
		} \
	})

#define check_nonnull(v, what, orelse) \
	({ \
		typeof(v) _check_v = (v); \
		if (unlikely(_check_v == NULL)) { \
			perror(what); \
			orelse; \
			compiler_enforced_unreachable(); \
		} \
		_check_v; \
	})
