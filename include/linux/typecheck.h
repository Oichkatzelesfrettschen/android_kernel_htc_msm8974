#ifndef TYPECHECK_H_INCLUDED
#define TYPECHECK_H_INCLUDED

/*
 * Check at compile time that something is of a particular type.
 * Always evaluates to 1 so you may use it easily in comparisons.
 * Comparing pointers to the two types draws the compiler's distinct
 * pointer types diagnostic on a mismatch; qualifiers on x's type are
 * accepted, and x is never evaluated.
 */
#define typecheck(type,x) \
({	(void)((type *)0 == (typeof(x) *)0); \
	1; \
})

/*
 * Check at compile time that 'function' is a certain type, or is a pointer
 * to that type (needs to use typedef for the function type.)
 */
#define typecheck_fn(type,function) \
({	typeof(type) __tmp = function; \
	(void)__tmp; \
})

#endif		/* TYPECHECK_H_INCLUDED */
