/*
 * null_deref.c — Test crash handler (SIGSEGV on NULL deref)
 *
 * Dereferences a NULL pointer to trigger a page fault.
 * crash_handlers.c should catch this and dump register state.
 */
int main(void)
{
    int *p = 0; /* NULL */
    *p = 42;    /* trigger SIGSEGV */
    return 0;   /* never reached */
}
