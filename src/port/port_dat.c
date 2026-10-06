/**
 * @file
 * HSD archives on the port, through the native archive interface
 * (tools/dat-cli/native).
 *
 * On the console an archive is used in place: its pointers are offsets the
 * loader relocates into addresses (archive.c), and its data is read as the
 * game's structs. Here those structs are 64-bit and little-endian, so the
 * data can't be read in place; instead each public symbol the game asks for
 * is converted, with everything it reaches, into native objects. Raw data
 * (texels, display lists, vertex arrays, keyframes) stays as the disc has
 * it, which Aurora reads as the console's GPU would.
 *
 * The roots' types come from the schema `melee-dat native codegen` writes
 * from the game's types, so the game asks for symbols by name as it always
 * has: by the file's roots, or, where the file isn't known (archives the
 * game unpacks from others), by the type it reads the symbol as.
 */

#include "port_internal.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dat/archive.h>
#include <dolphin/os.h>

#include <sysdolphin/baselib/archive.h>

/// Generated with the schema (melee_dat.h).
extern const DatSchema melee_dat_schema;

#define HEADER_SIZE 0x20

/// An archive the port has open, by the buffer the game parsed it from.
typedef struct OpenArchive {
    const uint8_t* src;
    DatArchive* dat;
    struct OpenArchive* next;
} OpenArchive;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static OpenArchive* open_archives;
static char next_file[64];

static uint32_t be32(const uint8_t* p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 |
           (uint32_t) p[2] << 8 | p[3];
}

void port_dat_set_next_file(const char* name)
{
    const char* base = strrchr(name, '/');
    base = base != NULL ? base + 1 : name;
    snprintf(next_file, sizeof(next_file), "%s", base);
}

/// Close whatever was open from `src`: the game reuses buffers (a fighter's
/// animation slot), and what was read from one is gone once it does.
static void close_from(const uint8_t* src)
{
    OpenArchive** link = &open_archives;
    while (*link != NULL) {
        OpenArchive* open = *link;
        if (open->src == src) {
            *link = open->next;
            dat_close(open->dat);
            free(open);
        } else {
            link = &open->next;
        }
    }
}

int32_t port_dat_parse(HSD_Archive* archive, uint8_t* src, size_t size)
{
    const char* error = NULL;
    DatArchive* dat;
    OpenArchive* open;

    if (size < HEADER_SIZE || be32(src) != size) {
        OSReport("HSD_ArchiveParse: not an archive, or truncated (%x %x)\n",
                 size >= 4 ? be32(src) : 0, (u32) size);
        next_file[0] = '\0';
        return -1;
    }

    pthread_mutex_lock(&lock);
    close_from(src);
    dat = dat_open(&melee_dat_schema, src, size, &error);
    if (dat == NULL) {
        pthread_mutex_unlock(&lock);
        OSReport("HSD_ArchiveParse: %s\n", error != NULL ? error : "?");
        next_file[0] = '\0';
        return -1;
    }
    if (next_file[0] != '\0') {
        dat_load_roots(dat, next_file, 0);
        next_file[0] = '\0';
    }
    open = malloc(sizeof(*open));
    open->src = src;
    open->dat = dat;
    open->next = open_archives;
    open_archives = open;
    pthread_mutex_unlock(&lock);

    /* What the game reads of the archive itself, natively */
    archive->header.file_size = be32(src + 0x00);
    archive->header.data_size = be32(src + 0x04);
    archive->header.nb_reloc = be32(src + 0x08);
    archive->header.nb_public = be32(src + 0x0C);
    archive->header.nb_extern = be32(src + 0x10);
    memcpy(archive->header.version, src + 0x14, 4);
    archive->data = src + HEADER_SIZE;
    archive->top_ptr = src;
    archive->native = dat;
    return 0;
}

/// The type of the public root `name` in any file: the game's public
/// symbols are named for what they are (`ftDataMario`, `map_ptcl`), so one
/// name has one type wherever it is, which spares knowing the file.
static int32_t root_type_by_name(const char* name)
{
    const DatSchema* s = &melee_dat_schema;
    for (uint32_t i = 0; i < s->nmodules; i++) {
        const DatModule* mod = &s->modules[i];
        for (uint32_t j = 0; j < mod->nfiles; j++) {
            const DatFileRoots* f = &mod->files[j];
            for (uint32_t k = 0; k < f->nroots; k++) {
                if (!f->roots[k].alias &&
                    strcmp(f->roots[k].name, name) == 0)
                {
                    return f->roots[k].type;
                }
            }
        }
    }
    return DAT_NONE;
}

void* port_dat_public(HSD_Archive* archive, const char* name,
                      const char* type_name)
{
    DatArchive* dat = archive->native;
    void* p;

    if (dat == NULL) {
        return NULL;
    }
    p = dat_public_root(dat, name);
    if (p == NULL) {
        int32_t type = root_type_by_name(name);
        if (type != DAT_NONE) {
            p = dat_public(dat, name, type);
        }
    }
    if (p == NULL && type_name != NULL) {
        int32_t type = dat_type_by_name(&melee_dat_schema, type_name);
        if (type != DAT_NONE) {
            p = dat_public(dat, name, type);
        }
    }
    if (p == NULL) {
        OSReport("port: no type for %s (%s); is it in dat.yml?\n", name,
                 type_name != NULL ? type_name : "untyped");
    }
    return p;
}

/// The extern table and the symbol names, from the header's counts.
static const uint8_t* extern_table(HSD_Archive* archive)
{
    const uint8_t* src = archive->top_ptr;
    return src + HEADER_SIZE + archive->header.data_size +
           archive->header.nb_reloc * 4 + archive->header.nb_public * 8;
}

char* port_dat_extern(HSD_Archive* archive, int index)
{
    const uint8_t* table;
    const char* names;

    if (index < 0 || archive->top_ptr == NULL ||
        archive->header.nb_extern <= (u32) index)
    {
        return NULL;
    }
    table = extern_table(archive);
    names = (const char*) (table + archive->header.nb_extern * 8);
    return (char*) names + be32(table + index * 8 + 4);
}

void port_dat_locate_extern(HSD_Archive* archive, const char* name,
                            void* addr)
{
    (void) archive;
    /* The native objects leave externs null, which is what the game locates
     * them to; anything else would need the slot's type */
    if (addr != NULL) {
        OSReport("port: extern %s not located\n", name);
    }
}

uint32_t port_dat_raw_extent(const void* ptr)
{
    const uint8_t* p = ptr;
    uint32_t extent = 0;

    pthread_mutex_lock(&lock);
    for (OpenArchive* open = open_archives; open != NULL; open = open->next) {
        const uint8_t* raw = dat_raw(open->dat, 0);
        uint32_t size = dat_size(open->dat);
        if (p >= raw && p < raw + size) {
            extent = (uint32_t) (raw + size - p);
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return extent;
}
