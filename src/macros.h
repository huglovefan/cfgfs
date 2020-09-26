#pragma once

#include <sys/prctl.h> // prctl() for set_thread_name

#include "cli.h" // cli_println() for debug printing

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define assume(x) ({ typeof(x) x_ = (x); if (!x_) __builtin_unreachable(); x_; })

#ifdef SANITIZER
 extern void __sanitizer_print_stack_trace(void);
#else
 #define __sanitizer_print_stack_trace()
#endif

#define V  if (0)
#define VV if (0)
#define D  if (0)

#define Debug1(fmt, ...) cli_println("%s: " fmt, __func__, ##__VA_ARGS__)
#define Debug(fmt, ...)  cli_println(" %s: " fmt, __func__, ##__VA_ARGS__)

#define Dbg(fmt, ...) cli_println("%s: " fmt, __func__, ##__VA_ARGS__)

// https://en.cppreference.com/w/cpp/utility/exchange
#define exchange(T, var, newval) ({ T oldval = var; var = newval; oldval; })

#define set_thread_name(s) \
	_Static_assert(__builtin_strlen(s) <= 15, "thread name too long"); \
	prctl(PR_SET_NAME, s, NULL, NULL, NULL)

// -----------------------------------------------------------------------------

#undef assert

// https://stackoverflow.com/questions/10593492/catching-assert-with-side-effects
//extern char sideeffect_catcher;
// makes false positives

// https://stackoverflow.com/a/2671100
#define STRINGIZE_DETAIL(x) #x
#define STRINGIZE(x) STRINGIZE_DETAIL(x)

#ifdef SANITIZER
 extern void __sanitizer_print_stack_trace(void);
#else
 #define __sanitizer_print_stack_trace()
#endif

#define assert2(x, s) \
	({ typeof(x) _assert_rv = (x); \
	   if (unlikely(!_assert_rv)) { \
	       fprintf(stderr, EXE ": " __FILE__ ":" STRINGIZE(__LINE__) ": %s: Assertion failed: %s\n", __PRETTY_FUNCTION__, s); \
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
