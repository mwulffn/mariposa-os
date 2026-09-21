/*
 * keymap.h - what is printed on the keys
 *
 * A keymap is data: for each raw key code, the Unicode code point it
 * produces plain, shifted and with alt. 0 means none - the key still
 * produces events, it just has no character. Latin-1 is the first 256 code
 * points, so an 8-bit consumer truncates and a UTF-8 one encodes.
 */
#ifndef KEYMAP_H
#define KEYMAP_H

#define KEYMAP_NKEYS  0x60      /* $60 and up are the qualifier keys */

#define KF_CAPS        0x01     /* caps lock acts as shift: a letter */
#define KF_DEAD_PLAIN  0x02     /* that level is a dead key: it produces */
#define KF_DEAD_SHIFT  0x04     /* nothing until the next key, then the  */
#define KF_DEAD_ALT    0x08     /* two are combined                      */

struct key {
    unsigned short plain, shift, alt;
    unsigned short flags;
};

struct keymap {
    const char       *name;
    const struct key *keys;     /* KEYMAP_NKEYS of them */
};

extern const struct keymap keymap_us;
extern const struct keymap keymap_dk;

/* accent + base letter -> the combined character, or 0 if there is none. */
unsigned short keymap_compose(unsigned short accent, unsigned short base);

#endif /* KEYMAP_H */
