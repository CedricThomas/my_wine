/*
 * musl_stubs/pthread_impl.h — No-op pthread synchronization primitives
 *
 * musl's lock()/unlock() check libc.need_locks first. Since we set
 * libc.need_locks = 0 in libc.h, these functions are never called.
 * But they must be defined for compilation.
 */

#ifndef MUSL_STUB_PTHREAD_IMPL_H
#define MUSL_STUB_PTHREAD_IMPL_H

#include <time.h>

static inline void __wait(volatile int *lk, volatile int *wk, int val, int priv)
{
    (void)lk; (void)wk; (void)val; (void)priv;
}

static inline int __wake(volatile int *lk, int cnt, int priv)
{
    (void)lk; (void)cnt; (void)priv;
    return 0;
}

static inline int __timedwait(volatile int *lk, volatile int *wk, int val, int priv,
                              const struct timespec *ts)
{
    (void)lk; (void)wk; (void)val; (void)priv; (void)ts;
    return 0;
}

#endif
