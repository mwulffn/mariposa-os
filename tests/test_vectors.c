/*
 * test_vectors.c - ROM image sanity, the exception vector table, and the
 * panic handler's stack frame decoding.
 *
 * These are regression tests for two bugs fixed in 2210e79:
 *   - install_exception_vectors filled the table with the generic handler
 *     AFTER installing the specific ones, wiping spurious, the autovectors
 *     and every TRAP.
 *   - panic decoded SR and PC at a fixed offset, which is wrong for bus and
 *     address error: those push an 8-byte prologue ahead of the pair.
 */
#include "protocol.h"

/* --- ROM image ---------------------------------------------------------- */

static void t_rom_header(void)
{
    CHECK_U32(H_ROM_BASE, h_sym("rom_start"));
    CHECK_U32(0x414D4147u, h_peek32(H_ROM_BASE + 8));    /* 'AMAG' */
    /* Last longword of the ROM points back at its start. */
    CHECK_U32(H_ROM_BASE, h_peek32(H_ROM_BASE + H_ROM_SIZE - 4));
}

/* --- exception vector table --------------------------------------------- */

static void t_vectors_specific_handlers(void)
{
    h_result r;

    h_begin_call();
    r = h_call(h_sym("install_exception_vectors"));
    CHECK_CALL(r);

    CHECK_U32(h_sym("bus_error_handler"),     h_peek32(0x008));
    CHECK_U32(h_sym("address_error_handler"), h_peek32(0x00C));
    CHECK_U32(h_sym("illegal_handler"),       h_peek32(0x010));
    CHECK_U32(h_sym("zero_divide_handler"),   h_peek32(0x014));
    CHECK_U32(h_sym("chk_handler"),           h_peek32(0x018));
    CHECK_U32(h_sym("trapv_handler"),         h_peek32(0x01C));
    CHECK_U32(h_sym("privilege_handler"),     h_peek32(0x020));
    CHECK_U32(h_sym("trace_handler"),         h_peek32(0x024));
    CHECK_U32(h_sym("line_a_handler"),        h_peek32(0x028));
    CHECK_U32(h_sym("line_f_handler"),        h_peek32(0x02C));
}

static void t_vectors_survive_generic_fill(void)
{
    /* The bug: the generic fill covers $30-$3FF, which contains spurious,
     * all seven autovectors and all sixteen TRAPs. If the fill runs last it
     * silently eats them and every interrupt reports "UNKNOWN EXCEPTION". */
    uint32_t autovec = h_sym("auto_vec_handler");
    uint32_t trap    = h_sym("trap_handler");
    h_result r;
    uint32_t a;

    h_begin_call();
    r = h_call(h_sym("install_exception_vectors"));
    CHECK_CALL(r);

    CHECK_U32(h_sym("spurious_handler"), h_peek32(0x060));

    for (a = 0x064; a <= 0x07C; a += 4)
        CHECK_U32(autovec, h_peek32(a));

    for (a = 0x080; a <= 0x0BC; a += 4)
        CHECK_U32(trap, h_peek32(a));
}

static void t_vectors_unassigned_are_generic(void)
{
    uint32_t generic = h_sym("generic_handler");
    h_result r;

    h_begin_call();
    r = h_call(h_sym("install_exception_vectors"));
    CHECK_CALL(r);

    CHECK_U32(generic, h_peek32(0x030));   /* first vector in the filled range */
    CHECK_U32(generic, h_peek32(0x0C0));   /* just past the TRAPs */
    CHECK_U32(generic, h_peek32(0x3FC));   /* last vector */
}

static void t_vectors_leave_reset_alone(void)
{
    /* $0 and $4 are the reset SSP and PC. Clobbering them would be harmless
     * at runtime but is a sign the fill range slipped. */
    h_result r;

    h_poke32(0, 0x11111111u);
    h_poke32(4, 0x22222222u);

    h_begin_call();
    r = h_call(h_sym("install_exception_vectors"));
    CHECK_CALL(r);

    CHECK_U32(0x11111111u, h_peek32(0));
    CHECK_U32(0x22222222u, h_peek32(4));
}

/* --- panic stack frame decoding ----------------------------------------- */

#define FAKE_PC 0x0012345Au
#define FAKE_SR 0x2704u

/* panic never returns - it falls into the debugger's command loop, which
 * blocks on serial. Run it with a budget and read the result out of the
 * register dump area and the captured serial. */
static void run_panic(const char *entry, int group0)
{
    uint32_t sp = H_STACK_TOP - 64;

    if (group0) {
        h_poke16(sp + 0, 0x0015u);        /* SSW                */
        h_poke32(sp + 2, 0x00BADBADu);    /* access address     */
        h_poke16(sp + 6, 0x4E71u);        /* instruction register */
        h_poke16(sp + 8, (uint16_t)FAKE_SR);
        h_poke32(sp + 10, FAKE_PC);
    } else {
        h_poke16(sp + 0, (uint16_t)FAKE_SR);
        h_poke32(sp + 2, FAKE_PC);
    }

    h_set_sp(sp);
    h_set_a(0, h_str("TEST FAULT"));
    h_set_cycle_budget(4000000);
    h_run(h_sym(entry));
}

static void t_panic_group1_frame(void)
{
    run_panic("panic_with_msg", 0);
    CHECK_U32(FAKE_PC, h_peek32(h_sym("saved_pc")));
    CHECK_U32(FAKE_SR, h_peek16(h_sym("saved_sr")));
}

static void t_panic_group0_frame(void)
{
    /* Bus and address error push SSW, access address and IR ahead of the
     * SR/PC pair. Decoding them at offset 0 yields a garbage PC. */
    run_panic("panic_with_msg_group0", 1);
    CHECK_U32(FAKE_PC, h_peek32(h_sym("saved_pc")));
    CHECK_U32(FAKE_SR, h_peek16(h_sym("saved_sr")));
}

static void t_panic_reports_pc_on_serial(void)
{
    run_panic("panic_with_msg", 0);
    CHECK_CONTAINS("PC:$0012345A", h_serial());
    CHECK_CONTAINS("TEST FAULT", h_serial());
}

static void t_panic_group0_reports_pc_on_serial(void)
{
    run_panic("panic_with_msg_group0", 1);
    CHECK_CONTAINS("PC:$0012345A", h_serial());
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "rom_header",                 t_rom_header,                 NULL },
    { "specific_handlers",          t_vectors_specific_handlers,  NULL },
    { "survive_generic_fill",       t_vectors_survive_generic_fill, NULL },
    { "unassigned_are_generic",     t_vectors_unassigned_are_generic, NULL },
    { "leave_reset_alone",          t_vectors_leave_reset_alone,  NULL },
    { "panic_group1_frame",         t_panic_group1_frame,         NULL },
    { "panic_group0_frame",         t_panic_group0_frame,         NULL },
    { "panic_reports_pc",           t_panic_reports_pc_on_serial, NULL },
    { "panic_group0_reports_pc",    t_panic_group0_reports_pc_on_serial, NULL },
};

const test_suite vector_suite = { "rom", tests, sizeof tests / sizeof tests[0] };
