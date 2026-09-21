/*
 * keymap.c - the layouts, and what dead keys combine into
 *
 * Indexed by raw key code, which is a position: $10 is the key where Q is
 * on a US board. The tables say what is printed there.
 *
 * The Danish table follows the standard Danish layout. It has not been
 * checked key by key against a Danish Amiga keyboard, which may differ in
 * the corners: $00, $0D, $2B and $30 are the ones to look at.
 */
#include "keymap.h"

static const struct key us_keys[KEYMAP_NKEYS] = {
    /* $00 */ { '`', '~', 0, 0 },
    /* $01 */ { '1', '!', 0, 0 },
    /* $02 */ { '2', '@', 0, 0 },
    /* $03 */ { '3', '#', 0, 0 },
    /* $04 */ { '4', '$', 0, 0 },
    /* $05 */ { '5', '%', 0, 0 },
    /* $06 */ { '6', '^', 0, 0 },
    /* $07 */ { '7', '&', 0, 0 },
    /* $08 */ { '8', '*', 0, 0 },
    /* $09 */ { '9', '(', 0, 0 },
    /* $0A */ { '0', ')', 0, 0 },
    /* $0B */ { '-', '_', 0, 0 },
    /* $0C */ { '=', '+', 0, 0 },
    /* $0D */ { 0x5C, '|', 0, 0 },
    /* $0E */ { 0, 0, 0, 0 },
    /* $0F */ { '0', '0', 0, 0 },  /* keypad */
    /* $10 */ { 'q', 'Q', 0, KF_CAPS },
    /* $11 */ { 'w', 'W', 0, KF_CAPS },
    /* $12 */ { 'e', 'E', 0, KF_CAPS },
    /* $13 */ { 'r', 'R', 0, KF_CAPS },
    /* $14 */ { 't', 'T', 0, KF_CAPS },
    /* $15 */ { 'y', 'Y', 0, KF_CAPS },
    /* $16 */ { 'u', 'U', 0, KF_CAPS },
    /* $17 */ { 'i', 'I', 0, KF_CAPS },
    /* $18 */ { 'o', 'O', 0, KF_CAPS },
    /* $19 */ { 'p', 'P', 0, KF_CAPS },
    /* $1A */ { '[', '{', 0, 0 },
    /* $1B */ { ']', '}', 0, 0 },
    /* $1C */ { 0, 0, 0, 0 },
    /* $1D */ { '1', '1', 0, 0 },  /* keypad */
    /* $1E */ { '2', '2', 0, 0 },  /* keypad */
    /* $1F */ { '3', '3', 0, 0 },  /* keypad */
    /* $20 */ { 'a', 'A', 0, KF_CAPS },
    /* $21 */ { 's', 'S', 0, KF_CAPS },
    /* $22 */ { 'd', 'D', 0, KF_CAPS },
    /* $23 */ { 'f', 'F', 0, KF_CAPS },
    /* $24 */ { 'g', 'G', 0, KF_CAPS },
    /* $25 */ { 'h', 'H', 0, KF_CAPS },
    /* $26 */ { 'j', 'J', 0, KF_CAPS },
    /* $27 */ { 'k', 'K', 0, KF_CAPS },
    /* $28 */ { 'l', 'L', 0, KF_CAPS },
    /* $29 */ { ';', ':', 0, 0 },
    /* $2A */ { 0x27, '"', 0, 0 },
    /* $2B */ { 0, 0, 0, 0 },
    /* $2C */ { 0, 0, 0, 0 },
    /* $2D */ { '4', '4', 0, 0 },  /* keypad */
    /* $2E */ { '5', '5', 0, 0 },  /* keypad */
    /* $2F */ { '6', '6', 0, 0 },  /* keypad */
    /* $30 */ { 0, 0, 0, 0 },
    /* $31 */ { 'z', 'Z', 0, KF_CAPS },
    /* $32 */ { 'x', 'X', 0, KF_CAPS },
    /* $33 */ { 'c', 'C', 0, KF_CAPS },
    /* $34 */ { 'v', 'V', 0, KF_CAPS },
    /* $35 */ { 'b', 'B', 0, KF_CAPS },
    /* $36 */ { 'n', 'N', 0, KF_CAPS },
    /* $37 */ { 'm', 'M', 0, KF_CAPS },
    /* $38 */ { ',', '<', 0, 0 },
    /* $39 */ { '.', '>', 0, 0 },
    /* $3A */ { '/', '?', 0, 0 },
    /* $3B */ { 0, 0, 0, 0 },
    /* $3C */ { '.', '.', 0, 0 },  /* keypad */
    /* $3D */ { '7', '7', 0, 0 },  /* keypad */
    /* $3E */ { '8', '8', 0, 0 },  /* keypad */
    /* $3F */ { '9', '9', 0, 0 },  /* keypad */
    /* $40 */ { 0x20, 0x20, 0, 0 },
    /* $41 */ { 0x08, 0x08, 0, 0 },  /* backspace */
    /* $42 */ { 0x09, 0x09, 0, 0 },  /* tab */
    /* $43 */ { 0x0D, 0x0D, 0, 0 },  /* keypad enter */
    /* $44 */ { 0x0D, 0x0D, 0, 0 },  /* return */
    /* $45 */ { 0x1B, 0x1B, 0, 0 },  /* esc */
    /* $46 */ { 0x7F, 0x7F, 0, 0 },  /* del */
    /* $47 */ { 0, 0, 0, 0 },
    /* $48 */ { 0, 0, 0, 0 },
    /* $49 */ { 0, 0, 0, 0 },
    /* $4A */ { '-', '-', 0, 0 },  /* keypad */
    /* $4B */ { 0, 0, 0, 0 },
    /* $4C */ { 0, 0, 0, 0 },
    /* $4D */ { 0, 0, 0, 0 },
    /* $4E */ { 0, 0, 0, 0 },
    /* $4F */ { 0, 0, 0, 0 },
    /* $50 */ { 0, 0, 0, 0 },
    /* $51 */ { 0, 0, 0, 0 },
    /* $52 */ { 0, 0, 0, 0 },
    /* $53 */ { 0, 0, 0, 0 },
    /* $54 */ { 0, 0, 0, 0 },
    /* $55 */ { 0, 0, 0, 0 },
    /* $56 */ { 0, 0, 0, 0 },
    /* $57 */ { 0, 0, 0, 0 },
    /* $58 */ { 0, 0, 0, 0 },
    /* $59 */ { 0, 0, 0, 0 },
    /* $5A */ { '(', '(', 0, 0 },  /* keypad */
    /* $5B */ { ')', ')', 0, 0 },  /* keypad */
    /* $5C */ { '/', '/', 0, 0 },  /* keypad */
    /* $5D */ { '*', '*', 0, 0 },  /* keypad */
    /* $5E */ { '+', '+', 0, 0 },  /* keypad */
    /* $5F */ { 0, 0, 0, 0 },
};

static const struct key dk_keys[KEYMAP_NKEYS] = {
    /* $00 */ { 0xBD, 0xA7, 0, 0 },  /* half, section */
    /* $01 */ { '1', '!', 0, 0 },
    /* $02 */ { '2', '"', '@', 0 },
    /* $03 */ { '3', '#', 0xA3, 0 },
    /* $04 */ { '4', 0xA4, '$', 0 },
    /* $05 */ { '5', '%', 0, 0 },
    /* $06 */ { '6', '&', 0, 0 },
    /* $07 */ { '7', '/', '{', 0 },
    /* $08 */ { '8', '(', '[', 0 },
    /* $09 */ { '9', ')', ']', 0 },
    /* $0A */ { '0', '=', '}', 0 },
    /* $0B */ { '+', '?', 0, 0 },
    /* $0C */ { 0xB4, '`', '|', KF_DEAD_PLAIN | KF_DEAD_SHIFT },  /* dead acute, dead grave */
    /* $0D */ { 0x5C, '|', 0, 0 },
    /* $0E */ { 0, 0, 0, 0 },
    /* $0F */ { '0', '0', 0, 0 },  /* keypad */
    /* $10 */ { 'q', 'Q', 0, KF_CAPS },
    /* $11 */ { 'w', 'W', 0, KF_CAPS },
    /* $12 */ { 'e', 'E', 0, KF_CAPS },
    /* $13 */ { 'r', 'R', 0, KF_CAPS },
    /* $14 */ { 't', 'T', 0, KF_CAPS },
    /* $15 */ { 'y', 'Y', 0, KF_CAPS },
    /* $16 */ { 'u', 'U', 0, KF_CAPS },
    /* $17 */ { 'i', 'I', 0, KF_CAPS },
    /* $18 */ { 'o', 'O', 0, KF_CAPS },
    /* $19 */ { 'p', 'P', 0, KF_CAPS },
    /* $1A */ { 0xE5, 0xC5, 0, KF_CAPS },  /* a ring */
    /* $1B */ { 0xA8, '^', '~', KF_DEAD_PLAIN | KF_DEAD_SHIFT | KF_DEAD_ALT },  /* dead diaeresis, circumflex, tilde */
    /* $1C */ { 0, 0, 0, 0 },
    /* $1D */ { '1', '1', 0, 0 },  /* keypad */
    /* $1E */ { '2', '2', 0, 0 },  /* keypad */
    /* $1F */ { '3', '3', 0, 0 },  /* keypad */
    /* $20 */ { 'a', 'A', 0, KF_CAPS },
    /* $21 */ { 's', 'S', 0, KF_CAPS },
    /* $22 */ { 'd', 'D', 0, KF_CAPS },
    /* $23 */ { 'f', 'F', 0, KF_CAPS },
    /* $24 */ { 'g', 'G', 0, KF_CAPS },
    /* $25 */ { 'h', 'H', 0, KF_CAPS },
    /* $26 */ { 'j', 'J', 0, KF_CAPS },
    /* $27 */ { 'k', 'K', 0, KF_CAPS },
    /* $28 */ { 'l', 'L', 0, KF_CAPS },
    /* $29 */ { 0xE6, 0xC6, 0, KF_CAPS },  /* ae */
    /* $2A */ { 0xF8, 0xD8, 0, KF_CAPS },  /* o slash */
    /* $2B */ { 0x27, '*', 0, 0 },
    /* $2C */ { 0, 0, 0, 0 },
    /* $2D */ { '4', '4', 0, 0 },  /* keypad */
    /* $2E */ { '5', '5', 0, 0 },  /* keypad */
    /* $2F */ { '6', '6', 0, 0 },  /* keypad */
    /* $30 */ { '<', '>', 0x5C, 0 },
    /* $31 */ { 'z', 'Z', 0, KF_CAPS },
    /* $32 */ { 'x', 'X', 0, KF_CAPS },
    /* $33 */ { 'c', 'C', 0, KF_CAPS },
    /* $34 */ { 'v', 'V', 0, KF_CAPS },
    /* $35 */ { 'b', 'B', 0, KF_CAPS },
    /* $36 */ { 'n', 'N', 0, KF_CAPS },
    /* $37 */ { 'm', 'M', 0, KF_CAPS },
    /* $38 */ { ',', ';', 0, 0 },
    /* $39 */ { '.', ':', 0, 0 },
    /* $3A */ { '-', '_', 0, 0 },
    /* $3B */ { 0, 0, 0, 0 },
    /* $3C */ { '.', '.', 0, 0 },  /* keypad */
    /* $3D */ { '7', '7', 0, 0 },  /* keypad */
    /* $3E */ { '8', '8', 0, 0 },  /* keypad */
    /* $3F */ { '9', '9', 0, 0 },  /* keypad */
    /* $40 */ { 0x20, 0x20, 0, 0 },
    /* $41 */ { 0x08, 0x08, 0, 0 },  /* backspace */
    /* $42 */ { 0x09, 0x09, 0, 0 },  /* tab */
    /* $43 */ { 0x0D, 0x0D, 0, 0 },  /* keypad enter */
    /* $44 */ { 0x0D, 0x0D, 0, 0 },  /* return */
    /* $45 */ { 0x1B, 0x1B, 0, 0 },  /* esc */
    /* $46 */ { 0x7F, 0x7F, 0, 0 },  /* del */
    /* $47 */ { 0, 0, 0, 0 },
    /* $48 */ { 0, 0, 0, 0 },
    /* $49 */ { 0, 0, 0, 0 },
    /* $4A */ { '-', '-', 0, 0 },  /* keypad */
    /* $4B */ { 0, 0, 0, 0 },
    /* $4C */ { 0, 0, 0, 0 },
    /* $4D */ { 0, 0, 0, 0 },
    /* $4E */ { 0, 0, 0, 0 },
    /* $4F */ { 0, 0, 0, 0 },
    /* $50 */ { 0, 0, 0, 0 },
    /* $51 */ { 0, 0, 0, 0 },
    /* $52 */ { 0, 0, 0, 0 },
    /* $53 */ { 0, 0, 0, 0 },
    /* $54 */ { 0, 0, 0, 0 },
    /* $55 */ { 0, 0, 0, 0 },
    /* $56 */ { 0, 0, 0, 0 },
    /* $57 */ { 0, 0, 0, 0 },
    /* $58 */ { 0, 0, 0, 0 },
    /* $59 */ { 0, 0, 0, 0 },
    /* $5A */ { '(', '(', 0, 0 },  /* keypad */
    /* $5B */ { ')', ')', 0, 0 },  /* keypad */
    /* $5C */ { '/', '/', 0, 0 },  /* keypad */
    /* $5D */ { '*', '*', 0, 0 },  /* keypad */
    /* $5E */ { '+', '+', 0, 0 },  /* keypad */
    /* $5F */ { 0, 0, 0, 0 },
};

const struct keymap keymap_us = { "us", us_keys };
const struct keymap keymap_dk = { "dk", dk_keys };

/* Latin-1 has these and no others. */
static const struct {
    unsigned short accent, base, result;
} compose[] = {
    { 0xB4, 'a', 0xE1 },  /* latin small letter a with acute */
    { 0xB4, 'e', 0xE9 },  /* latin small letter e with acute */
    { 0xB4, 'i', 0xED },  /* latin small letter i with acute */
    { 0xB4, 'o', 0xF3 },  /* latin small letter o with acute */
    { 0xB4, 'u', 0xFA },  /* latin small letter u with acute */
    { 0xB4, 'y', 0xFD },  /* latin small letter y with acute */
    { 0xB4, 'A', 0xC1 },  /* latin capital letter a with acute */
    { 0xB4, 'E', 0xC9 },  /* latin capital letter e with acute */
    { 0xB4, 'I', 0xCD },  /* latin capital letter i with acute */
    { 0xB4, 'O', 0xD3 },  /* latin capital letter o with acute */
    { 0xB4, 'U', 0xDA },  /* latin capital letter u with acute */
    { 0xB4, 'Y', 0xDD },  /* latin capital letter y with acute */
    { 0x60, 'a', 0xE0 },  /* latin small letter a with grave */
    { 0x60, 'e', 0xE8 },  /* latin small letter e with grave */
    { 0x60, 'i', 0xEC },  /* latin small letter i with grave */
    { 0x60, 'o', 0xF2 },  /* latin small letter o with grave */
    { 0x60, 'u', 0xF9 },  /* latin small letter u with grave */
    { 0x60, 'A', 0xC0 },  /* latin capital letter a with grave */
    { 0x60, 'E', 0xC8 },  /* latin capital letter e with grave */
    { 0x60, 'I', 0xCC },  /* latin capital letter i with grave */
    { 0x60, 'O', 0xD2 },  /* latin capital letter o with grave */
    { 0x60, 'U', 0xD9 },  /* latin capital letter u with grave */
    { 0xA8, 'a', 0xE4 },  /* latin small letter a with diaeresis */
    { 0xA8, 'e', 0xEB },  /* latin small letter e with diaeresis */
    { 0xA8, 'i', 0xEF },  /* latin small letter i with diaeresis */
    { 0xA8, 'o', 0xF6 },  /* latin small letter o with diaeresis */
    { 0xA8, 'u', 0xFC },  /* latin small letter u with diaeresis */
    { 0xA8, 'y', 0xFF },  /* latin small letter y with diaeresis */
    { 0xA8, 'A', 0xC4 },  /* latin capital letter a with diaeresis */
    { 0xA8, 'E', 0xCB },  /* latin capital letter e with diaeresis */
    { 0xA8, 'I', 0xCF },  /* latin capital letter i with diaeresis */
    { 0xA8, 'O', 0xD6 },  /* latin capital letter o with diaeresis */
    { 0xA8, 'U', 0xDC },  /* latin capital letter u with diaeresis */
    { 0x5E, 'a', 0xE2 },  /* latin small letter a with circumflex */
    { 0x5E, 'e', 0xEA },  /* latin small letter e with circumflex */
    { 0x5E, 'i', 0xEE },  /* latin small letter i with circumflex */
    { 0x5E, 'o', 0xF4 },  /* latin small letter o with circumflex */
    { 0x5E, 'u', 0xFB },  /* latin small letter u with circumflex */
    { 0x5E, 'A', 0xC2 },  /* latin capital letter a with circumflex */
    { 0x5E, 'E', 0xCA },  /* latin capital letter e with circumflex */
    { 0x5E, 'I', 0xCE },  /* latin capital letter i with circumflex */
    { 0x5E, 'O', 0xD4 },  /* latin capital letter o with circumflex */
    { 0x5E, 'U', 0xDB },  /* latin capital letter u with circumflex */
    { 0x7E, 'a', 0xE3 },  /* latin small letter a with tilde */
    { 0x7E, 'n', 0xF1 },  /* latin small letter n with tilde */
    { 0x7E, 'o', 0xF5 },  /* latin small letter o with tilde */
    { 0x7E, 'A', 0xC3 },  /* latin capital letter a with tilde */
    { 0x7E, 'N', 0xD1 },  /* latin capital letter n with tilde */
    { 0x7E, 'O', 0xD5 },  /* latin capital letter o with tilde */
};

unsigned short keymap_compose(unsigned short accent, unsigned short base)
{
    unsigned int i;

    for (i = 0; i < sizeof compose / sizeof compose[0]; i++)
        if (compose[i].accent == accent && compose[i].base == base)
            return compose[i].result;
    return 0;
}
