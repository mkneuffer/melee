/**
 * @file
 * How long a shape's vertex arrays are, for GXSetArray on the port.
 *
 * The console's GPU reads vertex arrays through the indices the display
 * lists give, so the data never says how long an array is. Aurora uploads
 * each array whole and needs its size: the largest index the shape's display
 * list uses, plus one, times the stride. This scans each display list once
 * for the largest index of every attribute and remembers it.
 */

#include "port_internal.h"

#include <stdlib.h>
#include <string.h>

#include <dolphin/gx.h>

#include <sysdolphin/baselib/pobj.h>

#define NATTRS 32

typedef struct Extents {
    const u8* display;
    const HSD_VtxDescList* verts;
    /// The largest index of each attribute, plus one; 0 if none.
    u32 count[NATTRS];
} Extents;

static Extents* table;
static size_t table_cap, table_len;

static u16 be16(const u8* p)
{
    return (u16) (p[0] << 8 | p[1]);
}

static u32 comp_size(GXCompType type)
{
    switch (type) {
    case GX_U8:
    case GX_S8:
        return 1;
    case GX_U16:
    case GX_S16:
        return 2;
    default:
        return 4;
    }
}

/// The bytes one vertex's direct (inline) data of an attribute takes.
static u32 direct_size(const HSD_VtxDescList* d)
{
    switch (d->attr) {
    case GX_VA_PNMTXIDX:
    case GX_VA_TEX0MTXIDX:
    case GX_VA_TEX1MTXIDX:
    case GX_VA_TEX2MTXIDX:
    case GX_VA_TEX3MTXIDX:
    case GX_VA_TEX4MTXIDX:
    case GX_VA_TEX5MTXIDX:
    case GX_VA_TEX6MTXIDX:
    case GX_VA_TEX7MTXIDX:
        return 1;
    case GX_VA_POS:
        return (d->comp_cnt == GX_POS_XY ? 2 : 3) * comp_size(d->comp_type);
    case GX_VA_NRM:
    case GX_VA_NBT:
        return (d->comp_cnt == GX_NRM_XYZ ? 3 : 9) * comp_size(d->comp_type);
    case GX_VA_CLR0:
    case GX_VA_CLR1:
        switch (d->comp_type) {
        case GX_RGB565:
        case GX_RGBA4:
            return 2;
        case GX_RGB8:
        case GX_RGBA6:
            return 3;
        default:
            return 4;
        }
    default:
        return (d->comp_cnt == GX_TEX_S ? 1 : 2) * comp_size(d->comp_type);
    }
}

/// The order the GPU reads a vertex's attributes in, whatever the order of
/// the list.
static int attr_order(GXAttr attr)
{
    return attr == GX_VA_NBT ? GX_VA_NRM : attr;
}

static int compare_order(const void* a, const void* b)
{
    const HSD_VtxDescList* x = *(const HSD_VtxDescList* const*) a;
    const HSD_VtxDescList* y = *(const HSD_VtxDescList* const*) b;
    return attr_order(x->attr) - attr_order(y->attr);
}

static void scan(Extents* e, const u8* p, const u8* end)
{
    const HSD_VtxDescList* order[NATTRS];
    int n = 0;

    for (const HSD_VtxDescList* d = e->verts; d->attr != GX_VA_NULL; d++) {
        if (d->attr_type != GX_NONE && n < NATTRS) {
            order[n++] = d;
        }
    }
    qsort(order, n, sizeof(*order), compare_order);

    while (p < end) {
        u8 op = *p++;
        if (op == GX_NOP || op == 0x48 /* invalidate vertex cache */) {
            continue;
        }
        if ((op & 0x80) == 0) {
            /* HSD's display lists are primitives and padding; anything
             * else ends what we can read */
            break;
        }
        if (p + 2 > end) {
            break;
        }
        u16 nverts = be16(p);
        p += 2;
        for (u16 v = 0; v < nverts && p < end; v++) {
            for (int i = 0; i < n; i++) {
                const HSD_VtxDescList* d = order[i];
                int nidx = d->attr == GX_VA_NRM || d->attr == GX_VA_NBT
                               ? (d->comp_cnt == GX_NRM_NBT3 ? 3 : 1)
                               : 1;
                switch (d->attr_type) {
                case GX_DIRECT:
                    p += direct_size(d);
                    break;
                case GX_INDEX8:
                case GX_INDEX16:
                    for (int k = 0; k < nidx; k++) {
                        u32 idx;
                        if (d->attr_type == GX_INDEX8) {
                            idx = *p++;
                        } else {
                            idx = be16(p);
                            p += 2;
                        }
                        if (idx + 1 > e->count[d->attr]) {
                            e->count[d->attr] = idx + 1;
                        }
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }
}

static size_t slot_of(const u8* display, const HSD_VtxDescList* verts)
{
    uintptr_t h = (uintptr_t) display ^ ((uintptr_t) verts >> 4);
    h ^= h >> 17;
    h *= 0x9E3779B97F4A7C15ULL;
    return (size_t) (h >> 7) & (table_cap - 1);
}

static Extents* lookup(const u8* display, const HSD_VtxDescList* verts)
{
    size_t i;

    if (table_len * 2 >= table_cap) {
        Extents* old = table;
        size_t old_cap = table_cap;
        table_cap = table_cap ? table_cap * 2 : 1024;
        table = calloc(table_cap, sizeof(*table));
        for (size_t j = 0; j < old_cap; j++) {
            if (old[j].display != NULL) {
                i = slot_of(old[j].display, old[j].verts);
                while (table[i].display != NULL) {
                    i = (i + 1) & (table_cap - 1);
                }
                table[i] = old[j];
            }
        }
        free(old);
    }

    i = slot_of(display, verts);
    while (table[i].display != NULL) {
        if (table[i].display == display && table[i].verts == verts) {
            return &table[i];
        }
        i = (i + 1) & (table_cap - 1);
    }
    return &table[i];
}

/// Forget what was scanned from display lists in `[start, start + size)`:
/// the archive they were in is gone, and its memory may hold another's.
void port_gx_forget(const void* start, size_t size)
{
    const u8* lo = start;
    const u8* hi = lo + size;
    Extents* old = table;
    size_t old_cap = table_cap;

    if (table == NULL) {
        return;
    }
    table = calloc(table_cap, sizeof(*table));
    table_len = 0;
    for (size_t j = 0; j < old_cap; j++) {
        if (old[j].display != NULL &&
            !(old[j].display >= lo && old[j].display < hi))
        {
            Extents* e = lookup(old[j].display, old[j].verts);
            *e = old[j];
            table_len++;
        }
    }
    free(old);
}

void port_gx_set_array(const HSD_PObj* pobj, const HSD_VtxDescList* desc)
{
    Extents* e = lookup(pobj->display, pobj->verts);
    u32 size;

    if (e->display == NULL) {
        e->display = pobj->display;
        e->verts = pobj->verts;
        memset(e->count, 0, sizeof(e->count));
        scan(e, pobj->display, pobj->display + (pobj->n_display << 5));
        table_len++;
    }
    /* With NBT3, N, B and T each index the array on their own; the scan
     * counted the largest of them */
    size = e->count[desc->attr] * desc->stride;
    /* Big-endian, as the disc has it */
    GXSETARRAY(desc->attr, desc->vertex, size, desc->stride, false);
}
