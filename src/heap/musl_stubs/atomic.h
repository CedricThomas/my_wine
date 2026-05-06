/*
 * musl_stubs/atomic.h — x86_64 atomics + generic fallbacks
 *
 * Combines musl's arch/x86_64/atomic_arch.h (full x86_64 atomics)
 * with the generic fallbacks from src/internal/atomic.h that x86_64
 * doesn't define (a_fetch_and, a_fetch_or, a_ctz_32, a_clz_32).
 *
 * Include order matters: x86_64 definitions come first, then generic
 * fallbacks guarded by #ifndef so they only compile when not already
 * defined.
 */

#ifndef MUSL_STUB_ATOMIC_H
#define MUSL_STUB_ATOMIC_H

#include <stdint.h>

/* ── x86_64 architecture-specific atomic primitives ─────────── */

#define a_cas a_cas
static inline int a_cas(volatile int *p, int t, int s)
{
	__asm__ __volatile__ (
		"lock ; cmpxchg %3, %1"
		: "=a"(t), "=m"(*p) : "a"(t), "r"(s) : "memory");
	return t;
}

#define a_cas_p a_cas_p
static inline void *a_cas_p(volatile void *p, void *t, void *s)
{
	__asm__( "lock ; cmpxchg %3, %1"
		: "=a"(t), "=m"(*(void *volatile *)p)
		: "a"(t), "r"(s) : "memory");
	return t;
}

#define a_swap a_swap
static inline int a_swap(volatile int *p, int v)
{
	__asm__ __volatile__(
		"xchg %0, %1"
		: "=r"(v), "=m"(*p) : "0"(v) : "memory");
	return v;
}

#define a_fetch_add a_fetch_add
static inline int a_fetch_add(volatile int *p, int v)
{
	__asm__ __volatile__(
		"lock ; xadd %0, %1"
		: "=r"(v), "=m"(*p) : "0"(v) : "memory");
	return v;
}

#define a_and a_and
static inline void a_and(volatile int *p, int v)
{
	__asm__ __volatile__(
		"lock ; and %1, %0"
		: "=m"(*p) : "r"(v) : "memory");
}

#define a_or a_or
static inline void a_or(volatile int *p, int v)
{
	__asm__ __volatile__(
		"lock ; or %1, %0"
		: "=m"(*p) : "r"(v) : "memory");
}

#define a_and_64 a_and_64
static inline void a_and_64(volatile uint64_t *p, uint64_t v)
{
	__asm__ __volatile__(
		"lock ; and %1, %0"
		: "=m"(*p) : "r"(v) : "memory");
}

#define a_or_64 a_or_64
static inline void a_or_64(volatile uint64_t *p, uint64_t v)
{
	__asm__ __volatile__(
		"lock ; or %1, %0"
		: "=m"(*p) : "r"(v) : "memory");
}

#define a_inc a_inc
static inline void a_inc(volatile int *p)
{
	__asm__ __volatile__(
		"lock ; incl %0"
		: "=m"(*p) : "m"(*p) : "memory");
}

#define a_dec a_dec
static inline void a_dec(volatile int *p)
{
	__asm__ __volatile__(
		"lock ; decl %0"
		: "=m"(*p) : "m"(*p) : "memory");
}

#define a_store a_store
static inline void a_store(volatile int *p, int x)
{
	__asm__ __volatile__(
		"mov %1, %0 ; lock ; orl $0,(%%rsp)"
		: "=m"(*p) : "r"(x) : "memory");
}

#define a_barrier a_barrier
static inline void a_barrier(void)
{
	__asm__ __volatile__( "" : : : "memory");
}

#define a_spin a_spin
static inline void a_spin(void)
{
	__asm__ __volatile__( "pause" : : : "memory");
}

#define a_crash a_crash
static inline void a_crash(void)
{
	__asm__ __volatile__( "hlt" : : : "memory");
}

#define a_ctz_64 a_ctz_64
static inline int a_ctz_64(uint64_t x)
{
	__asm__( "bsf %1,%0" : "=r"(x) : "r"(x));
	return x;
}

#define a_clz_64 a_clz_64
static inline int a_clz_64(uint64_t x)
{
	__asm__( "bsr %1,%0 ; xor $63,%0" : "=r"(x) : "r"(x));
	return x;
}

/* ── Generic fallbacks (x86_64 doesn't define these) ───────── */

#ifndef a_fetch_and
#define a_fetch_and a_fetch_and
static inline int a_fetch_and(volatile int *p, int v)
{
	int old;
	do old = *p;
	while (a_cas(p, old, old & v) != old);
	return old;
}
#endif

#ifndef a_fetch_or
#define a_fetch_or a_fetch_or
static inline int a_fetch_or(volatile int *p, int v)
{
	int old;
	do old = *p;
	while (a_cas(p, old, old | v) != old);
	return old;
}
#endif

#ifndef a_or_l
#define a_or_l a_or_l
static inline void a_or_l(volatile void *p, long v)
{
	if (sizeof(long) == sizeof(int)) a_or((volatile int *)p, v);
	else a_or_64(p, v);
}
#endif

#ifndef a_ctz_32
#define a_ctz_32 a_ctz_32
static inline int a_ctz_32(uint32_t x)
{
#ifdef a_clz_32
	return 31 - a_clz_32(x & -x);
#else
	static const char debruijn32[32] = {
		0, 1, 23, 2, 29, 24, 19, 3, 30, 27, 25, 11, 20, 8, 4, 13,
		31, 22, 28, 18, 26, 10, 7, 12, 21, 17, 9, 6, 16, 5, 15, 14
	};
	return debruijn32[(x & -x) * 0x076be629 >> 27];
#endif
}
#endif

#ifndef a_clz_32
#define a_clz_32 a_clz_32
static inline int a_clz_32(uint32_t x)
{
	x >>= 1;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x++;
	return 31 - a_ctz_32(x);
}
#endif

#endif /* MUSL_STUB_ATOMIC_H */
