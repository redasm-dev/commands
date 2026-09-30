#include "coff.h"
#include <redasm/redasm.h>
#include <stdlib.h>
#include <string.h>

/*
 * COFF symbol table.
 *
 * What is applied
 *   Every symbol that names a location inside a section:
 *     - storage class EXTERNAL, STATIC or LABEL
 *     - Section Number > 0 (1-based into the section table) and
 *       Value < section size (Value is an offset inside the section; 0 is
 *       valid, and a Value equal to the size is an end-of-section marker such
 *       as __bss_end__, which names nothing in that section)
 *   Functions (ISFCN(type), EXTERNAL or STATIC) are also marked as function
 *   starts.
 *   Everything else (data, statics, labels, import-library stubs and markers)
 *   is only a name.
 *
 * What is skipped
 *   Section symbols: STATIC, not a function, and either carrying a
 *   section-definition aux record (the spec) or named after an *input*
 *   section (GNU ld also keeps aux-less ones: .idata$5, .ctors, ...). No C
 *   identifier starts with '.', so this drops section names and nothing else.
 *   Absolute/debug/undefined symbols and the .file/.bf/.ef/... classes never
 *   reach the filter (they have no section or a different class).
 *
 * Confidence
 *   COFF is a subordinate source.
 *   A linked image's symbol table carries far more than REDasm can use, and
 *   none of it is authoritative next to what the format itself declares
 *   (import/export tables, entry point), so every name is AUTO and a
 *   same-address LIBRARY name always wins. Names are never stripped or
 *   undecorated here; the '@N' of stdcall symbols is information for the core.
 *
 * Aliases
 *   Several symbols can name one address (_fpreset/__fpreset, __data_start__/
 *   __CRT_glob).
 *   The table order says nothing about which one is the "real" one, so the
 *   pick is deterministic and not name-based: function first, then EXTERNAL
 *   over STATIC over LABEL, then table order.
 *
 * Ordering
 *   Must run AFTER the format's directories have been read, so their names
 *   are already in place when COFF's are applied.
 */

typedef struct CoffName {
    RDAddress address;
    u64 index;   // symbol table order
    u8 priority; // lower wins, see _coff_priority
    bool is_func;
    // long names point into strtab (stable); NULL means the name lives in
    // short_buf. Never store a pointer to short_buf: qsort moves the struct.
    const char* long_name;
    char short_buf[COFF_SYMBOL_SHORT_NAME_LEN + 1];
} CoffName;

static const char* _coff_name(const CoffName* n) {
    return n->long_name ? n->long_name : n->short_buf;
}

// function first, then EXTERNAL < STATIC < LABEL
static u8 _coff_priority(const CoffSymbol* sym, bool is_func) {
    u8 rank = 0;

    switch(sym->storage_class) {
        case IMAGE_SYM_CLASS_EXTERNAL: rank = 0; break;
        case IMAGE_SYM_CLASS_STATIC: rank = 1; break;
        default: rank = 2; break; // IMAGE_SYM_CLASS_LABEL
    }

    return (u8)((is_func ? 0 : 3) + rank);
}

static int _coff_name_cmp(const void* a, const void* b) {
    const CoffName* na = a;
    const CoffName* nb = b;
    if(na->address != nb->address) return na->address < nb->address ? -1 : 1;
    if(na->priority != nb->priority)
        return na->priority < nb->priority ? -1 : 1;
    if(na->index != nb->index) return na->index < nb->index ? -1 : 1;
    return 0;
}

static bool _coff_is_section_symbol(const CoffSymbol* sym, const char* name) {
    if(sym->storage_class != IMAGE_SYM_CLASS_STATIC || COFF_ISFCN(sym->type))
        return false;

    if(sym->n_aux_symbols) return true; // section-definition aux record
    return name[0] == '.'; // aux-less, named after an input section
}

static bool _coff_read_symbol(RDReader* r, CoffSymbol* sym) {
    rd_reader_read_exact(r, sym->name.short_name, sizeof(sym->name.short_name));
    rd_reader_read_le32(r, &sym->value);
    rd_reader_read_le16(r, (u16*)&sym->section_number);
    rd_reader_read_le16(r, &sym->type);
    rd_reader_read_byte(r, &sym->storage_class);
    rd_reader_read_byte(r, &sym->n_aux_symbols);
    return !rd_reader_has_error(r);
}

static char* _coff_read_strtab(RDReader* r, RDOffset offset, u32* size) {
    rd_reader_seek(r, offset);

    u32 strtab_size = 0;
    rd_reader_read_le32(r, &strtab_size);
    if(rd_reader_has_error(r) || strtab_size <= 4) {
        *size = 0;
        return NULL;
    }

    // keep the 4-byte size field slot so symbol offsets index directly
    char* strtab = rd_alloc((usize)strtab_size + 1);
    memset(strtab, 0, 4);

    if(!rd_reader_read_exact(r, strtab + 4, strtab_size - 4)) {
        rd_free(strtab);
        *size = 0;
        return NULL;
    }

    strtab[strtab_size] = '\0';
    *size = strtab_size;
    return strtab;
}

static RDCommandValue coff_execute(RDContext* ctx, const RDCommandValue* args) {
    RDOffset offset = args[0].off;
    u64 count = args[1].u;
    if(!count) return (RDCommandValue){0};

    RDReader* r = rd_get_input_reader(ctx);

    // the string table immediately follows the symbol table and its size
    // field is always present: failing to read it means offset/count are
    // bogus, so bail out before allocating anything proportional to count
    RDOffset strtab_offset = offset + (count * COFF_SYMBOL_SIZE);
    rd_reader_seek(r, strtab_offset);
    u32 probe;
    rd_reader_read_le32(r, &probe);
    if(rd_reader_has_error(r)) return (RDCommandValue){0};

    u32 strtab_size = 0;
    char* strtab = _coff_read_strtab(r, strtab_offset, &strtab_size);

    RDSegmentSlice segments = rd_get_all_segments(ctx);
    CoffName* names = rd_alloc(count * sizeof(*names));
    usize nnames = 0;

    rd_reader_seek(r, offset);

    for(u64 i = 0; i < count;) {
        CoffSymbol sym;
        if(!_coff_read_symbol(r, &sym)) break;

        u64 idx = i;
        i += 1 + sym.n_aux_symbols;

        // skip aux records
        if(sym.n_aux_symbols) {
            rd_reader_seek(r, rd_reader_tell(r) +
                                  ((u64)sym.n_aux_symbols * COFF_SYMBOL_SIZE));
        }

        if(sym.storage_class != IMAGE_SYM_CLASS_EXTERNAL &&
           sym.storage_class != IMAGE_SYM_CLASS_STATIC &&
           sym.storage_class != IMAGE_SYM_CLASS_LABEL)
            continue;

        if(sym.section_number <= 0) continue; // undefined/absolute/debug
        if((usize)(sym.section_number - 1) >= rd_slice_length(segments))
            continue;

        CoffName* n = &names[nnames];
        const char* name =
            coff_get_name(&sym, strtab, strtab_size, n->short_buf);
        if(!name || !*name) continue;
        if(_coff_is_section_symbol(&sym, name)) continue;

        const RDSegment* seg =
            rd_slice_at(segments, (usize)(sym.section_number - 1));
        RDAddress start = rd_segment_get_start(seg);
        RDAddress end = rd_segment_get_end(seg);
        if(sym.value >= end - start) continue;

        // a label is never a function start, even if its type says so
        bool is_func =
            COFF_ISFCN(sym.type) && sym.storage_class != IMAGE_SYM_CLASS_LABEL;

        n->address = start + sym.value;
        n->index = idx;
        n->is_func = is_func;
        n->priority = _coff_priority(&sym, is_func);
        n->long_name = (name == n->short_buf) ? NULL : name;
        nnames++;
    }

    // one name per address: the best-ranked candidate comes first
    qsort(names, nnames, sizeof(*names), _coff_name_cmp);

    for(usize i = 0; i < nnames;) {
        const CoffName* best = &names[i];

        if(best->is_func) rd_set_function(ctx, best->address);
        rd_auto_name(ctx, best->address, _coff_name(best));

        while(i < nnames && names[i].address == best->address)
            i++;
    }

    rd_free(names);
    if(strtab) rd_free(strtab);
    return (RDCommandValue){0};
}

static const RDCommandParam COFF_PARAMS[] = {
    {RD_CMDARG_OFFSET, "offset"},
    {RD_CMDARG_UINT, "count"},
    {RD_CMDARG_VOID},
};

static const RDCommandPlugin COFF = {
    .id = "coff_parse",
    .name = "COFF Parser",
    .params = COFF_PARAMS,
    .execute = coff_execute,
};

static void coff_module_load(void) { rd_register_command(&COFF); }

RD_MODULE_EXPORT = {
    .api_version = RD_API_VERSION,
    .load = coff_module_load,
};
