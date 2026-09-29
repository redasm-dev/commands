#include "coff.h"
#include <redasm/redasm.h>
#include <string.h>

/*
 * Interpretation with Storage Class:
 *   The meaning of the Section Number field is interlinked
 *   with the Storage Class (n_sclass or e_sclass) and
 *   Value (n_value or e_value) fields:
 *
 * - For External Symbols (C_EXT): If the section number is 0, the Value field
 *   indicates the size of an uninitialized global variable.
 *   If the section number is positive, the Value field specifies
 *   the offset within that section.
 * - For Static Symbols (C_STAT): If the section number is positive, the Value
 *   field specifies the offset within the section.
 *   If the Value is zero, it often represents a Section Symbol indicating
 *   the start of that section.
 * - For Function Entry Points: Typically marked as External (C_EXT) with the
 *   Section Number pointing to the .text section and the Value field holding
 *   the offset into that section.
 */

static RDCommandValue coff_execute(RDContext* ctx, const RDCommandValue* args) {
    RDOffset offset = args[0].off;
    u64 count = args[1].u;
    if(!count) return (RDCommandValue){0};

    RDReader* r = rd_get_input_reader(ctx);

    // read string table: immediately follows symbol table
    RDOffset strtab_offset = offset + (count * COFF_SYMBOL_SIZE);
    rd_reader_seek(r, strtab_offset);

    u32 strtab_size = 0;
    rd_reader_read_le32(r, &strtab_size);

    char* strtab = NULL;
    if(strtab_size > 4) {
        usize data_size = strtab_size - 4;

        strtab = rd_alloc(data_size + 1);

        if(!rd_reader_read_exact(r, strtab, data_size)) {
            rd_free(strtab);
            return (RDCommandValue){0};
        }

        strtab[data_size] = '\0';
        // adjust pointer so offset is relative to start of string table data
        strtab -= 4;
    }

    rd_reader_seek(r, offset);

    for(u64 i = 0; i < count;) {
        CoffSymbol sym;
        rd_reader_read_exact(r, sym.name.short_name,
                             sizeof(sym.name.short_name));
        rd_reader_read_le32(r, &sym.value);
        rd_reader_read_le16(r, (u16*)&sym.section_number);
        rd_reader_read_le16(r, &sym.type);
        rd_reader_read_byte(r, &sym.storage_class);
        rd_reader_read_byte(r, &sym.n_aux_symbols);

        if(rd_reader_has_error(r)) break;

        i += 1 + sym.n_aux_symbols;

        // skip aux records
        if(sym.n_aux_symbols) {
            rd_reader_seek(r, rd_reader_tell(r) +
                                  ((u64)sym.n_aux_symbols * COFF_SYMBOL_SIZE));
        }

        // only care about external symbols in real sections
        if(sym.storage_class != IMAGE_SYM_CLASS_EXTERNAL) continue;
        if(sym.section_number <= 0) continue;
        if(!sym.value) continue;

        const char* name = coff_get_name(&sym, strtab, strtab_size);
        if(!name || !(*name)) continue;

        RDSegmentSlice segments = rd_get_all_segments(ctx);
        if(sym.section_number - 1 >= rd_slice_length(segments)) continue;

        RDAddress addr = rd_segment_get_start(
                             rd_slice_at(segments, sym.section_number - 1)) +
                         sym.value;

        if(COFF_ISFCN(sym.type)) rd_set_function(ctx, addr);

        rd_library_name(ctx, addr, name);
    }

    if(strtab) rd_free(strtab + 4);

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
