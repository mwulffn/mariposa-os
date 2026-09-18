/*
 * main.c - test runner
 *
 * usage: run-tests [--rom PATH] [--sym PATH] [--filter SUBSTRING]
 *
 * --filter narrows the run to tests whose "suite.name" contains SUBSTRING,
 * which is the fast path when iterating on one failure.
 */
#include "protocol.h"

#include <stdio.h>
#include <string.h>

extern const test_suite format_suite;
extern const test_suite vector_suite;

int main(int argc, char **argv)
{
    const char *rom    = "../src/rom/build/kick.rom";
    const char *sym    = "../src/rom/build/kick.sym";
    const char *filter = NULL;
    int i, rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc)         rom = argv[++i];
        else if (strcmp(argv[i], "--sym") == 0 && i + 1 < argc)    sym = argv[++i];
        else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
        else {
            fprintf(stderr,
                "usage: %s [--rom PATH] [--sym PATH] [--filter SUBSTRING]\n",
                argv[0]);
            return 2;
        }
    }

    if (h_init(rom, sym) != 0)
        return 2;

    {
        const test_suite suites[] = { format_suite, vector_suite };
        rc = run_suites(suites, sizeof suites / sizeof suites[0], filter);
    }

    h_shutdown();
    return rc;
}
