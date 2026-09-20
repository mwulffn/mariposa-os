/*
 * main.c - test runner
 *
 * usage: run-tests [--rom PATH] [--sym PATH] [--filter SUBSTRING] [--builddir DIR]
 *
 * --filter narrows the run to tests whose "suite.name" contains SUBSTRING,
 * which is the fast path when iterating on one failure.
 */
#include "protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern const test_suite format_suite;
extern const test_suite vector_suite;
extern const test_suite irq_suite;
extern const test_suite memory_suite;
extern const test_suite zorro_suite;
extern const test_suite disk_suite;
extern const test_suite libsup_suite;
extern const test_suite cpu_suite;
extern const test_suite kserial_suite;
extern const test_suite boot_suite;

/*
 * Position-independent kernel assembly, assembled to origin zero and loaded
 * here. Each needs its own base: the symbols are merged into one table with
 * a per-module bias, so overlapping them would give two modules the same
 * addresses.
 */
static const struct { const char *name; uint32_t base; } modules[] = {
    { "libsup", 0x00100000u },
    { "cpu",    0x00110000u },
    { "isr",    0x00120000u },
};

/*
 * The real kernel image, linked at $200000, with its symbols under a
 * "kernel:" prefix. This is how the kernel's C gets tested: not a copy built
 * for the tests, the SYSTEM.BIN that boots. .bss lies past the end of the
 * file and is simply RAM, which h_reset zeroes - the job crt0 does.
 */
#define KERNEL_BASE 0x00200000u

/* Where mkdisk.py put the generated disk images. */
void t_set_disk_dir(const char *dir);

int main(int argc, char **argv)
{
    const char *rom    = "../src/rom/build/kick.rom";
    const char *sym    = "../src/rom/build/kick.sym";
    const char *filter = NULL;
    const char *builddir = "build";
    const char *kernel   = "../src/kernel/build/SYSTEM.BIN";
    const char *ksym     = "../src/kernel/build/kernel.sym";
    int i, rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc)         rom = argv[++i];
        else if (strcmp(argv[i], "--sym") == 0 && i + 1 < argc)    sym = argv[++i];
        else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
        else if (strcmp(argv[i], "--builddir") == 0 && i + 1 < argc) builddir = argv[++i];
        else if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc) kernel = argv[++i];
        else if (strcmp(argv[i], "--ksym") == 0 && i + 1 < argc) ksym = argv[++i];
        else {
            fprintf(stderr,
                "usage: %s [--rom PATH] [--sym PATH] [--filter SUBSTRING]"
                " [--builddir DIR] [--kernel PATH] [--ksym PATH]\n",
                argv[0]);
            return 2;
        }
    }

    if (h_init(rom, sym) != 0)
        return 2;

    t_set_disk_dir(builddir);

    for (i = 0; i < (int)(sizeof modules / sizeof modules[0]); i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.bin", builddir, modules[i].name);
        if (h_load_module(path, modules[i].base) != 0)
            return 2;
        snprintf(path, sizeof path, "%s/%s.sym", builddir, modules[i].name);
        if (h_add_symbols(path, modules[i].base) != 0)
            return 2;
    }

    if (h_load_module(kernel, KERNEL_BASE) != 0)
        return 2;
    if (h_add_symbols_prefixed(ksym, 0, "kernel:") != 0)
        return 2;

    {
        const test_suite suites[] = { libsup_suite, format_suite,
                                      vector_suite, irq_suite, cpu_suite,
                                      memory_suite, zorro_suite,
                                      disk_suite, kserial_suite,
                                      boot_suite };
        rc = run_suites(suites, sizeof suites / sizeof suites[0], filter);
    }

    h_shutdown();
    return rc;
}
