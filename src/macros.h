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
		__auto_type _assume_v = (v); \
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

// -----------------------------------------------------------------------------

#define assert_unreachable() ({ ASSERT_FAIL_NO_FMT("Unreachable code hit"); })

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
	({ \
		MAKE_ASSERTDATA(assertdata, \
		    EXE ": " __FILE__ ":" STRINGIZE(__LINE__) ": %s: " fmt "\n", \
		    __func__, \
		    s); \
		assert_fail(&assertdata); \
		asm("hlt"); \
		__builtin_unreachable(); \
	})

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
		__auto_type _check_v = (v); \
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
		__auto_type _check_v = (v); \
		if (unlikely(_check_v != 0)) { \
			eprintln(what ": %s", strerror(_check_v)); \
			orelse; \
			compiler_enforced_unreachable(); \
		} \
	})

#define check_nonnull(v, what, orelse) \
	({ \
		__auto_type _check_v = (v); \
		if (unlikely(_check_v == NULL)) { \
			perror(what); \
			orelse; \
			compiler_enforced_unreachable(); \
		} \
		_check_v; \
	})
