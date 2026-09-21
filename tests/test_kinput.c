/*
 * test_kinput.c - the keyboard: cia.c, kbd.c, input.c and the keymaps
 *
 * Three layers, tested through the top one. The driver speaks raw key codes
 * and nothing else - position codes, which is what the hardware sends and
 * is what makes layouts possible at all: $10 is "the key where Q is on a US
 * board", whatever is printed on it. input.c tracks qualifiers, repeats
 * keys and translates through the active keymap, and every event carries
 * both the raw code and the character, because a window manager binds by
 * position and a text field wants the letter.
 *
 * Characters are Unicode code points. Latin-1 is the first 256 of them.
 */
#include "protocol.h"

#include <stdint.h>
#include <string.h>

/* raw key codes */
#define K_1      0x01
#define K_2      0x02
#define K_ACUTE  0x0C       /* DK: dead acute, shifted dead grave */
#define K_Q      0x10
#define K_E      0x12
#define K_U      0x16
#define K_DIAER  0x1B       /* DK: dead diaeresis */
#define K_A      0x20
#define K_K      0x27
#define K_SEMI   0x29       /* US ;  DK ae */
#define K_C      0x33
#define K_SPACE  0x40
#define K_F1     0x50
#define K_LSHIFT 0x60
#define K_CAPS   0x62
#define K_CTRL   0x63
#define K_LALT   0x64
#define UP       0x80

#define Q_LSHIFT 0x01
#define Q_CAPS   0x04
#define Q_CTRL   0x08

#define V_UP     0
#define V_DOWN   1
#define V_REPEAT 2

typedef struct { unsigned type, code, value, qual, ch; } event;

static uint32_t kcall(const char *name, int nargs, const uint32_t *args)
{
    h_result r;
    int i;

    h_begin_call();
    for (i = nargs - 1; i >= 0; i--)
        h_push32(args[i]);
    r = h_call(h_sym(name));
    CHECK_CALL(r);
    return h_get_d(0);
}

static void setup_on(int model, uint32_t cpu)
{
    if (model != 68000)
        h_set_cpu(model);
    kcall("kernel:_ser_init", 0, NULL);
    kcall("kernel:_sched_init", 1, &cpu);
    kcall("kernel:_irq_init", 0, NULL);
    kcall("kernel:_input_init", 0, NULL);
    kcall("kernel:_cia_init", 0, NULL);
    kcall("kernel:_kbd_init", 0, NULL);
}

static void setup(void) { setup_on(68000, 0); }

/* Unmasked foreground, long enough for the keyboard to say its piece. */
static void let_it_run(int laps)
{
    uint8_t code[] = {
        0x72, 0x00, 0x30, 0x3C, 0xFF, 0xFF, 0x51, 0xC8, 0xFF, 0xFE,
        0x51, 0xC9, 0xFF, 0xF6, 0x4E, 0x75
    };
    h_result r;

    code[1] = (uint8_t)(laps - 1);
    h_begin_call();
    h_set_sr(0x2000);
    h_set_cycle_budget(40000000);
    r = h_call(h_alloc(code, sizeof code));
    CHECK_CALL(r);
}

static void type_keys(const uint8_t *codes, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        h_key(codes[i]);
    let_it_run(1);
    CHECK(h_kbd_idle(), "keyboard still waiting to be handshaken");
}

static uint32_t pending(void) { return kcall("kernel:_input_pending", 0, NULL); }

/* input_read only blocks on an empty queue, so with events waiting it can
 * be called from out here. */
static event next_event(void)
{
    uint8_t zero[8] = {0};
    uint32_t buf = h_alloc(zero, sizeof zero);
    event e;

    memset(&e, 0, sizeof e);
    if (pending() == 0) {
        t_fail("no event waiting");
        return e;
    }
    kcall("kernel:_input_read", 1, &buf);
    e.type  = h_peek8(buf);
    e.code  = h_peek8(buf + 1);
    e.value = h_peek16(buf + 2);
    e.qual  = h_peek16(buf + 4);
    e.ch    = h_peek16(buf + 6);
    return e;
}

/* The next character typed: skips key-ups, qualifier keys, and keys that
 * produce no character - a dead key waiting for its letter is one. */
static unsigned next_char(void)
{
    while (pending()) {
        event e = next_event();
        if (e.value == V_DOWN && e.code < K_LSHIFT && e.ch)
            return e.ch;
    }
    t_fail("no key-down event waiting");
    return 0;
}

static void keymap(const char *name)
{
    uint32_t a = h_str(name);
    CHECK_U32(0, kcall("kernel:_input_set_keymap", 1, &a));
}

/* --- the driver ----------------------------------------------------------- */

static void t_key_arrives_as_an_event(void)
{
    static const uint8_t keys[] = { K_A, K_A | UP };
    event e;

    setup();
    type_keys(keys, 2);

    CHECK_U32(2, pending());
    e = next_event();
    CHECK_U32(K_A, e.code);  CHECK_U32(V_DOWN, e.value);  CHECK_U32('a', e.ch);
    e = next_event();
    CHECK_U32(K_A, e.code);  CHECK_U32(V_UP, e.value);    CHECK_U32(0, e.ch);
}

/*
 * The handshake: KDAT held low for at least 85 microseconds per key, or the
 * keyboard decides the byte was lost. Timed by CIA timer A, so it is the
 * same 85 microseconds on any CPU - a delay loop tuned on a 7MHz 68000 is
 * several times too short on a 68020 and hopeless on a 68060.
 */
static void handshake_on(int model, uint32_t cpu)
{
    static const uint8_t keys[] = { K_Q, K_Q | UP, K_E, K_E | UP, K_A, K_A | UP };

    h_reset();
    setup_on(model, cpu);
    type_keys(keys, 6);

    CHECK_U32(6, h_kbd_handshakes());
    CHECK_U32(0, h_kbd_short_handshakes());
    CHECK_U32(6, pending());
}

static void t_handshake_68000(void) { handshake_on(68000, 0); }
static void t_handshake_68020(void) { handshake_on(68020, 2); }
static void t_handshake_68040(void) { handshake_on(68040, 4); }

static void t_keys_arrive_in_order(void)
{
    static const uint8_t keys[] = { 0x25, 0x25|UP, 0x12, 0x12|UP, 0x28, 0x28|UP,
                                    0x28, 0x28|UP, 0x18, 0x18|UP };   /* hello */
    char got[8];
    int n = 0;

    setup();
    type_keys(keys, 10);
    while (pending() && n < 7) {
        event e = next_event();
        if (e.value == V_DOWN) got[n++] = (char)e.ch;
    }
    got[n] = 0;
    CHECK_STR("hello", got);
}

/* $F9-$FE are the keyboard talking about itself: resend, buffer overflow,
 * self test. They are handshaken like any byte, and they are not keys. */
static void t_protocol_codes_are_not_keys(void)
{
    static const uint8_t keys[] = { 0xF9, 0xFA, K_A, 0xFD, 0xFE };

    setup();
    type_keys(keys, 5);

    CHECK_U32(5, h_kbd_handshakes());
    CHECK_U32(1, pending());
    CHECK_U32(4, h_peek32(h_sym("kernel:_kbd_protocol_codes")));
}

/* Nobody reading must never stall the keyboard: the handshake happens
 * whether or not the event has anywhere to go. */
static void t_full_queue_drops_but_still_handshakes(void)
{
    uint8_t keys[120];
    int i;

    for (i = 0; i < 120; i++)
        keys[i] = (uint8_t)(K_A | (i & 1 ? UP : 0));
    setup();
    for (i = 0; i < 120; i++) h_key(keys[i]);
    let_it_run(2);

    CHECK_U32(120, h_kbd_handshakes());
    CHECK(pending() < 120, "queue holds %u", pending());
    CHECK_U32(120, pending() + h_peek32(h_sym("kernel:_input_dropped")));
}

/* --- qualifiers ------------------------------------------------------------ */

static void t_shift(void)
{
    static const uint8_t keys[] = { K_LSHIFT, K_A, K_A|UP, K_1, K_1|UP,
                                    K_LSHIFT|UP, K_A, K_A|UP };
    event e;

    setup();
    type_keys(keys, 8);

    e = next_event();                               /* shift itself */
    CHECK_U32(K_LSHIFT, e.code);  CHECK_U32(0, e.ch);
    e = next_event();
    CHECK_U32('A', e.ch);  CHECK_U32(Q_LSHIFT, e.qual);
    next_event();
    CHECK_U32('!', next_char());
    CHECK_U32('a', next_char());
}

/* Caps lock shifts letters and leaves the rest alone. The Amiga keyboard
 * keeps the state itself: it sends "down" when the light goes on and "up"
 * when it goes off, so there is nothing to toggle here. */
static void t_caps_lock(void)
{
    static const uint8_t keys[] = { K_CAPS, K_A, K_A|UP, K_1, K_1|UP,
                                    K_CAPS|UP, K_A, K_A|UP };
    setup();
    type_keys(keys, 8);
    CHECK_U32('A', next_char());
    CHECK_U32('1', next_char());
    CHECK_U32('a', next_char());
}

static void t_ctrl_makes_control_codes(void)
{
    static const uint8_t keys[] = { K_CTRL, K_C, K_C|UP, K_CTRL|UP };
    setup();
    type_keys(keys, 4);
    CHECK_U32(3, next_char());
}

/* A key with no character still produces an event: that is what the raw
 * code is for. */
static void t_function_keys_have_codes_not_chars(void)
{
    static const uint8_t keys[] = { K_F1 };
    event e;

    setup();
    type_keys(keys, 1);
    e = next_event();
    CHECK_U32(K_F1, e.code);  CHECK_U32(V_DOWN, e.value);  CHECK_U32(0, e.ch);
}

/* --- layouts --------------------------------------------------------------- */

static void t_same_key_different_layout(void)
{
    static const uint8_t keys[] = { K_SEMI, K_SEMI|UP };

    setup();
    type_keys(keys, 2);
    CHECK_U32(';', next_char());

    while (pending()) next_event();
    keymap("dk");
    h_reset(); setup(); keymap("dk");
    type_keys(keys, 2);
    CHECK_U32(0xE6, next_char());                   /* ae */
}

static void t_danish_shift_caps_and_alt(void)
{
    static const uint8_t keys[] = { K_LSHIFT, K_SEMI, K_SEMI|UP, K_LSHIFT|UP,
                                    K_CAPS, K_SEMI, K_SEMI|UP, K_CAPS|UP,
                                    K_LALT, K_2, K_2|UP, K_LALT|UP };
    setup();
    keymap("dk");
    type_keys(keys, 12);
    CHECK_U32(0xC6, next_char());                   /* AE by shift */
    CHECK_U32(0xC6, next_char());                   /* AE by caps lock: a letter */
    CHECK_U32('@', next_char());                    /* alt-2 */
}

/* A dead key produces nothing until the next key, and then one character. */
static void t_dead_keys_compose(void)
{
    static const uint8_t keys[] = {
        K_ACUTE, K_ACUTE|UP, K_E, K_E|UP,                       /* e acute */
        K_LSHIFT, K_ACUTE, K_ACUTE|UP, K_LSHIFT|UP, K_A, K_A|UP, /* a grave */
        K_DIAER, K_DIAER|UP, K_U, K_U|UP,                        /* u diaeresis */
        K_ACUTE, K_ACUTE|UP, K_SPACE, K_SPACE|UP,                /* the accent itself */
        K_ACUTE, K_ACUTE|UP, K_K, K_K|UP,                        /* no such letter */
    };
    event e;

    setup();
    keymap("dk");
    type_keys(keys, sizeof keys);

    e = next_event();
    CHECK_U32(K_ACUTE, e.code);  CHECK_U32(0, e.ch);        /* dead: silent */
    CHECK_U32(0xE9, next_char());
    CHECK_U32(0xE0, next_char());
    CHECK_U32(0xFC, next_char());
    CHECK_U32(0xB4, next_char());
    CHECK_U32('k', next_char());
}

static void t_unknown_keymap_is_refused(void)
{
    static const uint8_t keys[] = { K_SEMI };
    uint32_t a;

    setup();
    keymap("dk");
    a = h_str("klingon");
    CHECK_U32(0xFFFFFFFFu, kcall("kernel:_input_set_keymap", 1, &a));
    type_keys(keys, 1);
    CHECK_U32(0xE6, next_char());                   /* still Danish */
}

/* --- repeat ---------------------------------------------------------------- */

/* The Amiga keyboard does not repeat; software does, off the tick. */
static void t_held_key_repeats(void)
{
    static const uint8_t up[] = { K_A | UP };
    int downs = 0, repeats = 0;
    uint32_t n;

    setup();
    h_vbl_every(20011);
    h_key(K_A);
    let_it_run(3);                                  /* ~100 ticks held */

    while (pending()) {
        event e = next_event();
        if (e.value == V_DOWN) downs++;
        if (e.value == V_REPEAT) { repeats++; CHECK_U32('a', e.ch); }
    }
    CHECK_U32(1, downs);
    CHECK(repeats >= 5, "held for ~100 ticks, %d repeats", repeats);

    type_keys(up, 1);
    while (pending()) next_event();
    let_it_run(2);
    n = pending();
    CHECK(n == 0, "%u events after the key was released", n);
}

/* Shift is held for seconds at a time and must not machine-gun. */
static void t_qualifiers_do_not_repeat(void)
{
    setup();
    h_vbl_every(20011);
    h_key(K_LSHIFT);
    let_it_run(3);
    CHECK_U32(1, pending());
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "key_is_an_event",        t_key_arrives_as_an_event,          NULL },
    { "handshake.68000",        t_handshake_68000,                  NULL },
    { "handshake.68020",        t_handshake_68020,                  NULL },
    { "handshake.68040",        t_handshake_68040,                  NULL },
    { "keys_in_order",          t_keys_arrive_in_order,             NULL },
    { "protocol_codes",         t_protocol_codes_are_not_keys,      NULL },
    { "full_queue_handshakes",  t_full_queue_drops_but_still_handshakes, NULL },
    { "shift",                  t_shift,                            NULL },
    { "caps_lock",              t_caps_lock,                        NULL },
    { "ctrl",                   t_ctrl_makes_control_codes,         NULL },
    { "function_keys",          t_function_keys_have_codes_not_chars, NULL },
    { "layout_us_vs_dk",        t_same_key_different_layout,        NULL },
    { "dk_shift_caps_alt",      t_danish_shift_caps_and_alt,        NULL },
    { "dk_dead_keys",           t_dead_keys_compose,                NULL },
    { "unknown_keymap",         t_unknown_keymap_is_refused,        NULL },
    { "held_key_repeats",       t_held_key_repeats,                 NULL },
    { "qualifiers_dont_repeat", t_qualifiers_do_not_repeat,         NULL },
};

const test_suite kinput_suite = { "kbd", tests, sizeof tests / sizeof tests[0] };
