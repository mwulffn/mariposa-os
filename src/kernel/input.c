/*
 * input.c - input events: qualifiers, repeat, and layout translation
 *
 * Everything the keyboard driver is not. It is fed raw key codes from the
 * driver's handler and ticks from the vertical blank, and both run with
 * interrupts masked; readers take a critical section.
 */
#include "input.h"
#include "keymap.h"
#include "kstring.h"
#include "task.h"
#include "cpu.h"

#define QUEUE_MASK (INPUT_QUEUE_SIZE - 1)

static struct input_event queue[INPUT_QUEUE_SIZE];
static volatile unsigned short q_head, q_tail;
static struct waitq readers;

volatile unsigned long input_dropped;

static const struct keymap *const keymaps[] = { &keymap_us, &keymap_dk };
static const struct keymap *keymap = &keymap_us;

static unsigned short qual;         /* qualifiers held now */
static unsigned short dead;         /* accent waiting for a letter, or 0 */
static unsigned char  repeat_code;  /* key being held, if repeat_ticks != 0 */
static unsigned short repeat_ticks; /* until the next repeat; 0 = none held */

/* ---------------------------------------------------------------- queue --- */

static void post(unsigned char code, unsigned short value, unsigned short ch)
{
    unsigned short next = (q_head + 1) & QUEUE_MASK;
    struct input_event *ev;

    if (next == q_tail) {
        input_dropped++;            /* nobody reading; the newest goes */
        return;
    }
    ev = &queue[q_head];
    ev->type  = INPUT_KEY;
    ev->code  = code;
    ev->value = value;
    ev->qual  = qual;
    ev->ch    = ch;
    q_head = next;
    wake_one(&readers);
}

unsigned long input_pending(void)
{
    unsigned long n;

    CRITICAL_ENTER();
    n = (unsigned long)((q_head - q_tail) & QUEUE_MASK);
    CRITICAL_EXIT();
    return n;
}

void input_read(struct input_event *ev)
{
    CRITICAL_ENTER();
    while (q_head == q_tail)
        task_wait(&readers);
    *ev = queue[q_tail];
    q_tail = (q_tail + 1) & QUEUE_MASK;
    CRITICAL_EXIT();
}

/* ---------------------------------------------------------- translation --- */

/* What the key at `code` types with the current qualifiers. *is_dead is set
 * if that is an accent waiting for its letter and not a character yet. */
static unsigned short lookup(unsigned char code, int *is_dead)
{
    const struct key *k;
    unsigned short ch;
    int shifted;

    *is_dead = 0;
    if (code >= KEYMAP_NKEYS)
        return 0;
    k = &keymap->keys[code];

    if ((qual & QUAL_ALT) && k->alt) {
        *is_dead = (k->flags & KF_DEAD_ALT) != 0;
        return k->alt;
    }

    /* Caps lock is shift for letters and nothing at all for the rest. */
    shifted = (qual & QUAL_SHIFT) != 0;
    if ((qual & QUAL_CAPS) && (k->flags & KF_CAPS))
        shifted = 1;

    ch = shifted ? k->shift : k->plain;
    *is_dead = (k->flags & (shifted ? KF_DEAD_SHIFT : KF_DEAD_PLAIN)) != 0;

    /* ctrl-C is 3: the letter's code point with the top bits off. */
    if ((qual & QUAL_CTRL) && ch >= '@' && ch <= 0x7F)
        ch &= 0x1F;
    return ch;
}

/* A key went down: the character it produces, dead keys resolved. */
static unsigned short translate(unsigned char code)
{
    unsigned short ch, combined;
    int is_dead;

    ch = lookup(code, &is_dead);
    if (is_dead) {
        dead = ch;                  /* say nothing yet */
        return 0;
    }
    if (!dead || !ch)
        return ch;

    /* accent + letter -> the combined letter; accent + space -> the accent
     * itself; accent + anything else -> the anything else, accent dropped. */
    combined = keymap_compose(dead, ch);
    if (!combined && ch == ' ')
        combined = dead;
    dead = 0;
    return combined ? combined : ch;
}

/* ------------------------------------------------------------- from ISRs --- */

void input_key(unsigned char raw)
{
    unsigned char code = raw & 0x7F;
    int down = !(raw & 0x80);

    if (code >= KEY_FIRST_QUALIFIER) {
        /* $60-$67 are the qualifier keys, in QUAL_ bit order. Caps lock
         * needs no toggling: the keyboard keeps that state itself and sends
         * "down" when the light goes on, "up" when it goes off. */
        unsigned short bit = (unsigned short)(1U << (code - KEY_FIRST_QUALIFIER));

        if (code <= 0x67) {
            if (down) qual |= bit;
            else      qual &= (unsigned short)~bit;
        }
        post(code, down ? KEY_DOWN : KEY_UP, 0);
        return;
    }

    if (down) {
        unsigned short ch = translate(code);

        post(code, KEY_DOWN, ch);
        /* The last key down is the one that repeats - unless it is a dead
         * key waiting, which has nothing to repeat. */
        repeat_code  = code;
        repeat_ticks = dead ? 0 : REPEAT_DELAY;
    } else {
        if (repeat_ticks && code == repeat_code)
            repeat_ticks = 0;
        post(code, KEY_UP, 0);
    }
}

void input_tick(void)
{
    int is_dead;

    if (!repeat_ticks || --repeat_ticks)
        return;
    repeat_ticks = REPEAT_PERIOD;
    /* Looked up afresh: shift pressed mid-repeat changes what repeats. */
    post(repeat_code, KEY_REPEAT, lookup(repeat_code, &is_dead));
}

/* ----------------------------------------------------------------- setup --- */

int input_set_keymap(const char *name)
{
    unsigned int i;

    for (i = 0; i < sizeof keymaps / sizeof keymaps[0]; i++)
        if (str_eq(keymaps[i]->name, name)) {
            CRITICAL_ENTER();
            keymap = keymaps[i];
            dead = 0;
            CRITICAL_EXIT();
            return 0;
        }
    return -1;
}

const char *input_keymap_name(void)
{
    return keymap->name;
}

void input_init(void)
{
    q_head = q_tail = 0;
    readers.head = readers.tail = 0;
    input_dropped = 0;
    qual = 0;
    dead = 0;
    repeat_ticks = 0;
    keymap = &keymap_us;
}
