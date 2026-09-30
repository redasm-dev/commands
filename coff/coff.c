#include "coff.h"
#include <string.h>

const char* coff_get_name(const CoffSymbol* sym, const char* strtab,
                          u32 strtab_size,
                          char buf[COFF_SYMBOL_SHORT_NAME_LEN + 1]) {
    if(!sym->name.long_name.zeroes) {
        u32 off = sym->name.long_name.offset;
        // offsets below 4 point into the size field
        if(strtab && off >= 4 && off < strtab_size) return strtab + off;
        return NULL;
    }

    // short_name may not be null-terminated if exactly 8 chars
    memcpy(buf, sym->name.short_name, COFF_SYMBOL_SHORT_NAME_LEN);
    buf[COFF_SYMBOL_SHORT_NAME_LEN] = 0;
    return buf;
}
