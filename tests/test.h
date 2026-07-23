/*
 * Minimal test harness shared by the validation tests.
 *
 * Each test program uses CHECK()/CHECK_EQ() to record results and
 * finishes by returning test_exit(name) from main().  A failing check
 * prints the location and expression but does not abort, so one run
 * reports all failures.
 */

#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int	test_checks;
static int	test_fails;

#define CHECK(cond)							\
	do {								\
		++test_checks;						\
		if (!(cond)) {						\
			++test_fails;					\
			printf("FAIL %s:%d: %s\n",			\
				__FILE__, __LINE__, #cond);		\
		}							\
	} while (0)

#define CHECK_EQ(a, b)							\
	do {								\
		long long _ta = (long long)(a);				\
		long long _tb = (long long)(b);				\
		++test_checks;						\
		if (_ta != _tb) {					\
			++test_fails;					\
			printf("FAIL %s:%d: %s == %s (%lld != %lld)\n",	\
				__FILE__, __LINE__, #a, #b, _ta, _tb);	\
		}							\
	} while (0)

#define CHECK_NEAR(a, b, tol)						\
	do {								\
		double _ta = (a);					\
		double _tb = (b);					\
		double _td = _ta > _tb ? _ta - _tb : _tb - _ta;		\
		++test_checks;						\
		if (!(_td <= (tol))) {					\
			++test_fails;					\
			printf("FAIL %s:%d: %s ~= %s (%g != %g)\n",	\
				__FILE__, __LINE__, #a, #b, _ta, _tb);	\
		}							\
	} while (0)

static inline int
test_exit(const char *name)
{
	printf("%s: %d check%s, %d failure%s\n",
		name,
		test_checks, test_checks == 1 ? "" : "s",
		test_fails, test_fails == 1 ? "" : "s");

	return test_fails ? EXIT_FAILURE : EXIT_SUCCESS;
}

#endif
