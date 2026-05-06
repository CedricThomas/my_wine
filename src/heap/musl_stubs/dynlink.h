/*
 * musl_stubs/dynlink.h — Stub for musl's dynlink.h
 *
 * aligned_alloc.c checks __malloc_replaced and __aligned_alloc_replaced
 * to detect replacement allocator scenarios. Both are 0 (false) here.
 */

#ifndef MUSL_STUB_DYNLINK_H
#define MUSL_STUB_DYNLINK_H

extern int __malloc_replaced;
extern int __aligned_alloc_replaced;

#endif
