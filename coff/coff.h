#pragma once

#include <redasm/redasm.h>

// COFF Symbol Storage Classes
#define IMAGE_SYM_CLASS_EXTERNAL 2

// COFF Symbol Types (high byte of Type field)
#define IMAGE_SYM_DTYPE_FUNCTION 2

// COFF Symbol Section Numbers
#define IMAGE_SYM_UNDEFINED 0
#define IMAGE_SYM_ABSOLUTE ((u16) - 1)
#define IMAGE_SYM_DEBUG ((u16) - 2)

#define COFF_SYMBOL_SIZE 18
#define COFF_SYMBOL_SHORT_NAME_LEN 8

#define COFF_N_BTSHFT 4
#define COFF_N_TMASK 0x0030

#define COFF_ISFCN(t)                                                          \
    (((t) & COFF_N_TMASK) == (IMAGE_SYM_DTYPE_FUNCTION << COFF_N_BTSHFT))

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

const char* coff_get_name(const CoffSymbol* sym, const char* strtab,
                          u32 strtab_size);
