/*
 * protocol.h - test registry and the ### result protocol
 *
 * Every line the runner prints on stdout that begins with ### is machine
 * readable and one line long:
 *
 *   ###BOOT ok
 *   ###TEST <name> PASS
 *   ###TEST <name> FAIL <detail>
 *   ###TEST <name> XFAIL <detail>        known bug, does not fail the run
 *   ###TEST <name> XPASS <reason>        known bug looks fixed - drop the xfail
 *   ###PANIC <what>                      guest took an exception
 *   ###NOTE  <text>                      advisory, never affects exit code
 *   ###DONE pass=N fail=N xfail=N xpass=N
 *
 * Exit codes:
 *   0  everything passed (xfail and xpass are not failures)
 *   1  at least one test failed
 *   2  harness error - could not load the ROM, symbols, or Musashi state
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "harness.h"

typedef struct {
    const char *name;
    void      (*fn)(void);
    /* NULL when the test is expected to pass. Otherwise a one-line
     * description of the known bug that makes it fail; the run stays green
     * and the failure is reported as XFAIL. */
    const char *xfail;
} test_case;

typedef struct {
    const char      *name;
    const test_case *tests;
    size_t           count;
} test_suite;

/* Record a failure against the test currently running. Safe to call more
 * than once; the first failure is what gets reported. */
void t_fail(const char *fmt, ...);

void t_check_str (const char *file, int line, const char *expect, const char *actual);
void t_check_u32 (const char *file, int line, const char *what,
                  uint32_t expect, uint32_t actual);
void t_check_call(const char *file, int line, const h_result *r);
void t_check_contains(const char *file, int line, const char *needle, const char *haystack);

#define CHECK(cond, ...)      do { if (!(cond)) t_fail(__VA_ARGS__); } while (0)
#define CHECK_STR(e, a)       t_check_str(__FILE__, __LINE__, (e), (a))
#define CHECK_U32(e, a)       t_check_u32(__FILE__, __LINE__, #a, (uint32_t)(e), (uint32_t)(a))
#define CHECK_CALL(r)         t_check_call(__FILE__, __LINE__, &(r))
#define CHECK_CONTAINS(n, h)  t_check_contains(__FILE__, __LINE__, (n), (h))

/* Run every suite, print the protocol, return the process exit code.
 * `filter`, if non-NULL, limits the run to tests whose "suite.name"
 * contains it as a substring. */
int run_suites(const test_suite *suites, size_t nsuites, const char *filter);

#endif /* PROTOCOL_H */
