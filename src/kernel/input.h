/*
 * input.h - input events
 *
 * See docs/input_design.md. Drivers deliver raw codes; this layer turns them
 * into events that carry both the raw code and the character, because both
 * have users: a window manager or a game binds by position and must work
 * the same on any layout, a text field wants the letter.
 */
#ifndef INPUT_H
#define INPUT_H

#define INPUT_KEY      1

/* value */
#define KEY_UP         0
#define KEY_DOWN       1
#define KEY_REPEAT     2

/* qual - the Amiga's own bit order */
#define QUAL_LSHIFT    0x0001
#define QUAL_RSHIFT    0x0002
#define QUAL_CAPS      0x0004
#define QUAL_CTRL      0x0008
#define QUAL_LALT      0x0010
#define QUAL_RALT      0x0020
#define QUAL_LAMIGA    0x0040
#define QUAL_RAMIGA    0x0080
#define QUAL_SHIFT     (QUAL_LSHIFT | QUAL_RSHIFT)
#define QUAL_ALT       (QUAL_LALT | QUAL_RALT)

#define KEY_FIRST_QUALIFIER 0x60

struct input_event {
    unsigned char  type;    /* INPUT_KEY */
    unsigned char  code;    /* raw key code: a position, not a letter */
    unsigned short value;   /* KEY_UP, KEY_DOWN, KEY_REPEAT */
    unsigned short qual;    /* qualifiers held when it happened */
    unsigned short ch;      /* Unicode code point, or 0: key up, a key with
                             * no character, or a dead key waiting */
};

#define INPUT_QUEUE_SIZE 64     /* a power of two */

#define REPEAT_DELAY   30       /* ticks held before repeating: 600ms */
#define REPEAT_PERIOD  3        /* ticks between repeats: ~17 a second */

void input_init(void);

/* Block until there is an event. Task context only. */
void input_read(struct input_event *ev);
unsigned long input_pending(void);

/* "us", "dk". 0, or -1 for a name nobody has heard of. */
int input_set_keymap(const char *name);
const char *input_keymap_name(void);

/* For drivers and the tick, from interrupt handlers: a raw key code with
 * bit 7 set for key up; and once per vertical blank. */
void input_key(unsigned char raw);
void input_tick(void);

/* Events thrown away because nobody was reading. */
extern volatile unsigned long input_dropped;

#endif /* INPUT_H */
