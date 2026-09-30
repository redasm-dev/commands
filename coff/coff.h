#pragma once

#include <redasm/redasm.h>

// COFF Symbol Storage Classes
#define IMAGE_SYM_CLASS_EXTERNAL 2
#define IMAGE_SYM_CLASS_STATIC 3
#define IMAGE_SYM_CLASS_LABEL 6

// COFF Symbol Types
// bits 0-3: base type, bits 4-5: complex (derived) type.
// Microsoft and GNU tools only emit 0x00 (not a function) or 0x20 (function).
#define IMAGE_SYM_DTYPE_FUNCTION 2
#define COFF_N_BTSHFT 4
#define COFF_N_TMASK 0x0030
#define COFF_ISFCN(t)                                                          \
    (((t) & COFF_N_TMASK) == (IMAGE_SYM_DTYPE_FUNCTION << COFF_N_BTSHFT))

// COFF Symbol Section Numbers (section_number is signed)
#define IMAGE_SYM_UNDEFINED 0
#define IMAGE_SYM_ABSOLUTE ((i16) - 1)
#define IMAGE_SYM_DEBUG ((i16) - 2)

#define COFF_SYMBOL_SIZE 18
#define COFF_SYMBOL_SHORT_NAME_LEN 8

typedef struct CoffSymbol {
    union {
        char short_name[COFF_SYMBOL_SHORT_NAME_LEN];
        struct {
            u32 zeroes;
            u32 offset;
        } long_name;
    } name;

    u32 value;
    i16 section_number;
    u16 type;
    u8 storage_class;
    u8 n_aux_symbols;
} CoffSymbol;

// Returns a pointer into strtab (long names) or into buf (short names).
// strtab must be NUL-terminated at strtab_size and indexed from the start
// of the string table, including its 4-byte size field.
const char* coff_get_name(const CoffSymbol* sym, const char* strtab,
                          u32 strtab_size,
                          char buf[COFF_SYMBOL_SHORT_NAME_LEN + 1]);
