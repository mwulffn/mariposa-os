#include "protocol.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int  g_failed;                  /* current test has failed */
static char g_detail[512];             /* first failure detail */
static char g_panic[256];              /* set when the guest took an exception */

/* Collapse a string to one printable line so it cannot break the protocol. */
static void escape(char *dst, size_t dstsz, const char *src)
{
    size_t o = 0;
    for (; *src && o + 5 < dstsz; src++) {
        unsigned char c = (unsigned char)*src;
        if      (c == '\n') { dst[o++] = '\\'; dst[o++] = 'n'; }
        else if (c == '\r') { dst[o++] = '\\'; dst[o++] = 'r'; }
        else if (c == '\t') { dst[o++] = '\\'; dst[o++] = 't'; }
        else if (c < 0x20 || c > 0x7E) o += (size_t)snprintf(dst + o, dstsz - o, "\\x%02X", c);
        else dst[o++] = (char)c;
    }
    dst[o] = '\0';
}

void t_fail(const char *fmt, ...)
{
    va_list ap;
    if (!g_failed) {
        va_start(ap, fmt);
        vsnprintf(g_detail, sizeof g_detail, fmt, ap);
        va_end(ap);
    }
    g_failed = 1;
}

int t_failed(void) { return g_failed; }

void t_check_str(const char *file, int line, const char *expect, const char *actual)
{
    char e[256], a[256];
    if (strcmp(expect, actual) == 0) return;
    escape(e, sizeof e, expect);
    escape(a, sizeof a, actual);
    t_fail("%s:%d expected \"%s\" got \"%s\"", file, line, e, a);
}

void t_check_contains(const char *file, int line, const char *needle, const char *haystack)
{
    char n[256], h[256];
    if (strstr(haystack, needle) != NULL) return;
    escape(n, sizeof n, needle);
    escape(h, sizeof h, haystack);
    t_fail("%s:%d expected output to contain \"%s\", got \"%s\"", file, line, n, h);
}

void t_check_u32(const char *file, int line, const char *what,
                 uint32_t expect, uint32_t actual)
{
    if (expect == actual) return;
    t_fail("%s:%d %s: expected $%08X got $%08X", file, line, what, expect, actual);
}

void t_check_call(const char *file, int line, const h_result *r)
{
    switch (r->status) {
        case H_OK:
            return;
        case H_EXCEPTION:
            snprintf(g_panic, sizeof g_panic, "%s", r->detail);
            t_fail("%s:%d guest exception: %s", file, line, r->detail);
            return;
        case H_TIMEOUT:
            t_fail("%s:%d routine did not return: %s", file, line, r->detail);
            return;
        case H_FAULT:
            t_fail("%s:%d %s", file, line, r->detail);
            return;
    }
    t_fail("%s:%d unknown call status %d", file, line, (int)r->status);
}

int run_suites(const test_suite *suites, size_t nsuites, const char *filter)
{
    size_t s, i;
    int pass = 0, fail = 0, xfail = 0, xpass = 0;
    char detail[512];

    printf("###BOOT ok\n");
    fflush(stdout);

    for (s = 0; s < nsuites; s++) {
        for (i = 0; i < suites[s].count; i++) {
            const test_case *tc = &suites[s].tests[i];
            char full[192];

            snprintf(full, sizeof full, "%s.%s", suites[s].name, tc->name);

            if (filter && strstr(full, filter) == NULL)
                continue;

            g_failed = 0;
            g_detail[0] = '\0';
            g_panic[0]  = '\0';

            h_reset();
            tc->fn();

            if (g_panic[0])
                printf("###PANIC %s\n", g_panic);

            escape(detail, sizeof detail, g_detail);

            if (g_failed && tc->xfail) {
                printf("###TEST %s XFAIL %s\n", full, tc->xfail);
                xfail++;
            } else if (g_failed) {
                printf("###TEST %s FAIL %s\n", full, detail);
                fail++;
            } else if (tc->xfail) {
                printf("###TEST %s XPASS %s\n", full, tc->xfail);
                xpass++;
            } else {
                printf("###TEST %s PASS\n", full);
                pass++;
            }
            fflush(stdout);
        }
    }

    if (filter && pass + fail + xfail + xpass == 0)
        printf("###NOTE filter \"%s\" matched no tests\n", filter);

    if (xpass)
        printf("###NOTE %d test(s) marked xfail now pass; "
               "remove the xfail annotation in tests/\n", xpass);

    printf("###DONE pass=%d fail=%d xfail=%d xpass=%d\n", pass, fail, xfail, xpass);
    fflush(stdout);

    return fail ? 1 : 0;
}
