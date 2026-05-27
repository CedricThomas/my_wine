#include <string.h>

#include "ddraw_priv.h"

extern int rb_window_destroy(rb_window_t win) __attribute__((weak));

my_dd_t *g_ddraw_instance = NULL;

my_dd_t *ddraw_create_instance(void)
{
    my_dd_t *dd = ddraw_alloc_mem(sizeof(*dd));

    if (!dd)
        return NULL;

    memset(dd, 0, sizeof(*dd));
    dd->lpVtbl = (IDirectDrawVtbl *)&ddraw_vtbl;
    dd->ref_count = 1;
    dd->current_mode_w = 320;
    dd->current_mode_h = 200;
    dd->current_mode_bpp = 8;
    dd->cooperative_level = DDSCL_NORMAL;

    g_ddraw_instance = dd;
    return dd;
}

HRESULT KERNEL32_STUB ddraw_query_interface(my_dd_t *dd, const GUID *riid,
                                            void **ppvObj)
{
    if (!dd || !riid || !ppvObj)
        return DDERR_INVALIDPARAMS;

    *ppvObj = NULL;
    if (IsEqualGUID(riid, &IID_IDirectDraw)) {
        dd->ref_count++;
        *ppvObj = FORCE_PTR_RETURN(dd);
        return DD_OK;
    }
    return DDERR_UNSUPPORTED;
}

uint32_t KERNEL32_STUB ddraw_add_ref(my_dd_t *dd)
{
    if (dd)
        dd->ref_count++;
    return dd ? dd->ref_count : 0;
}

uint32_t KERNEL32_STUB ddraw_release(my_dd_t *dd)
{
    my_surface_t *surf;

    if (!dd)
        return 0;

    if (dd->ref_count > 0)
        dd->ref_count--;
    if (dd->ref_count != 0)
        return dd->ref_count;

    surf = dd->surface_list;
    while (surf) {
        my_surface_t *next = surf->owner_list_next;
        surf->dd_owner = NULL;
        surf->owner_list_next = NULL;
        surf = next;
    }
    dd->surface_list = NULL;

    if (dd->primary_surface) {
        my_surface_t *primary = dd->primary_surface;
        dd->primary_surface = NULL;
        ddraw_surface_release(primary);
    }

    if (dd->owns_window && dd->rb_window && rb_window_destroy)
        rb_window_destroy(dd->rb_window);

    if (g_ddraw_instance == dd)
        g_ddraw_instance = NULL;
    ddraw_free(dd);
    return 0;
}
