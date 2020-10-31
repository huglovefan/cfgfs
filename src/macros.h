#pragma once

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

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
#define exchange(T, var, newval) ({ T oldval = var; var = newval; oldval; })

// #include <sys/prctl.h>
#define set_thread_name(s) \
	do { \
	_Static_assert(__builtin_strlen(s) <= 15, "thread name too long"); \
	prctl(PR_SET_NAME, s, NULL, NULL, NULL); \
	} while (0)

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

#define assert2(x, s) \
	({ __auto_type _assert_rv = (x); \
	   if (unlikely(!_assert_rv)) { \
	       fprintf(stderr, EXE ": " __FILE__ ":" STRINGIZE(__LINE__) ": %s: Assertion failed: %s\n", __func__, s); \
	       __sanitizer_print_stack_trace(); \
	       abort(); \
	   } \
	   _assert_rv; \
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
