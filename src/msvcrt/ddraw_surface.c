/*
 * ddraw_surface.c
 *
 * Internal DirectDraw surface object support:
 * - surface object allocation and owner-list registration
 * - surface COM lifetime and teardown
 */

#include <string.h>

#include "ddraw_priv.h"

extern int rb_surface_destroy(rb_surface_t surf) __attribute__((weak));

static void ddraw_unlink_surface_from_owner(my_surface_t *surf)
{
    my_dd_t *dd;
    my_surface_t **link;

    if (!surf || !surf->dd_owner)
        return;

    dd = surf->dd_owner;
    link = &dd->surface_list;
    while (*link) {
        if (*link == surf) {
            *link = surf->owner_list_next;
            break;
        }
        link = &(*link)->owner_list_next;
    }

    surf->dd_owner = NULL;
    surf->owner_list_next = NULL;
}

static void ddraw_destroy_surface(my_surface_t *surf)
{
    if (!surf)
        return;

    if (surf->next) {
        my_surface_t *attached = surf->next;
        surf->next = NULL;
        ddraw_surface_release(attached);
    }

    if (surf->palette) {
        my_palette_t *pal = surf->palette;
        surf->palette = NULL;
        ddraw_palette_release(pal);
    }
    ddraw_surface_clear_clipper(surf);

    ddraw_unlink_surface_from_owner(surf);

    if (surf->rb_surface && rb_surface_destroy)
        rb_surface_destroy(surf->rb_surface);

    ddraw_free(surf);
}

my_surface_t *ddraw_surface_alloc(my_dd_t *dd, rb_surface_t rb_surface,
                                  uint32_t caps)
{
    my_surface_t *surf = ddraw_alloc_mem(sizeof(*surf));

    if (!surf)
        return NULL;

    memset(surf, 0, sizeof(*surf));
    surf->lpVtbl = (IDirectDrawSurfaceVtbl *)&surface_vtbl;
    surf->ref_count = 1;
    surf->rb_surface = rb_surface;
    surf->caps = caps;
    surf->dd_owner = dd;
    if (dd) {
        surf->owner_list_next = dd->surface_list;
        dd->surface_list = surf;
    }
    return surf;
}

uint32_t KERNEL32_STUB ddraw_surface_add_ref(my_surface_t *surf)
{
    if (surf)
        surf->ref_count++;
    return surf ? surf->ref_count : 0;
}

uint32_t KERNEL32_STUB ddraw_surface_release(my_surface_t *surf)
{
    if (!surf)
        return 0;

    if (surf->ref_count > 0)
        surf->ref_count--;
    if (surf->ref_count != 0)
        return surf->ref_count;

    if (surf->dd_owner && surf->dd_owner->primary_surface == surf)
        surf->dd_owner->primary_surface = NULL;
    ddraw_destroy_surface(surf);
    return 0;
}
