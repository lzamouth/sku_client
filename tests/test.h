/* Minimal test harness: CHECK(cond), CHECK_EQ_STR(a, b), CHECK_EQ_INT(a, b). */
#ifndef SKU_TEST_H
#define SKU_TEST_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int test_failures, test_checks;

#define CHECK(cond) do { test_checks++; if (!(cond)) { test_failures++; \
	fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ_STR(a, b) do { const char *_a = (a), *_b = (b); test_checks++; \
	if (!_a || !_b || strcmp(_a, _b) != 0) { test_failures++; \
	fprintf(stderr, "FAIL %s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, _a ? _a : "(null)", _b ? _b : "(null)"); } } while (0)

#define CHECK_EQ_INT(a, b) do { long long _a = (long long)(a), _b = (long long)(b); test_checks++; \
	if (_a != _b) { test_failures++; \
	fprintf(stderr, "FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

#define TEST_MAIN_END() do { printf("%s: %d checks, %d failures\n", __FILE__, test_checks, test_failures); \
	return test_failures ? 1 : 0; } while (0)

#endif
