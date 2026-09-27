#ifndef __LINUX_COMPILER_H
#error "Please don't include <linux/compiler-clang.h> directly, include <linux/compiler.h> instead."
#endif

/* Clang diagnoses x = x as an uninitialized use. */
#ifdef uninitialized_var
# undef uninitialized_var
# define uninitialized_var(x) x = *(&(x))
#endif

/* Clang has supported __COUNTER__ throughout its Linux kernel lifetime. */
#undef __UNIQUE_ID
#define __UNIQUE_ID(prefix) \
	__PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

/* Clang advertises GCC 4.2, but implements these facilities directly. */
#if __has_attribute(cold)
# define __cold __attribute__((__cold__))
#endif

#if __has_builtin(__builtin_unreachable)
# define unreachable() __builtin_unreachable()
#endif

/* Clang supports these attributes despite its GCC 4.2 compatibility value. */
#if __has_attribute(warning)
# undef __compiletime_warning
# define __compiletime_warning(message) __attribute__((warning(message)))
#endif

#if __has_attribute(error)
# undef __compiletime_error
# define __compiletime_error(message) __attribute__((error(message)))
#endif
