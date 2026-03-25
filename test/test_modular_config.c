/* Copyright (c) Hardy Simpson
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * test_modular_config.c — Test for the modular configuration API
 *
 * Tests:
 *   zlog_add_format()
 *   zlog_add_rule()
 *   zlog_remove_rules()
 *   zlog_has_format()
 *   zlog_has_category_rules()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "zlog.h"

#define ASSERT_OK(expr) do { \
	int _rc = (expr); \
	if (_rc) { \
		fprintf(stderr, "FAIL: %s returned %d at line %d\n", #expr, _rc, __LINE__); \
		zlog_fini(); \
		return EXIT_FAILURE; \
	} \
} while(0)

#define ASSERT_FAIL(expr) do { \
	int _rc = (expr); \
	if (_rc >= 0) { \
		fprintf(stderr, "FAIL: %s should have failed but returned %d at line %d\n", #expr, _rc, __LINE__); \
		zlog_fini(); \
		return EXIT_FAILURE; \
	} \
} while(0)

#define ASSERT_EQ(expr, expected) do { \
	int _rc = (expr); \
	if (_rc != (expected)) { \
		fprintf(stderr, "FAIL: %s returned %d, expected %d at line %d\n", #expr, _rc, (expected), __LINE__); \
		zlog_fini(); \
		return EXIT_FAILURE; \
	} \
} while(0)

int main(void)
{
	int rc;
	zlog_category_t *cat;

	/* Initialize with a minimal config */
	const char *config =
		"[global]\n"
		"strict init = false\n"
		"\n"
		"[formats]\n"
		"simple = \"%d(%H:%M:%S) [%c] %m%n\"\n"
		"\n"
		"[rules]\n"
		"*.INFO >stdout; simple\n";

	printf("=== Test 1: Initialize ===\n");
	rc = zlog_init_from_string(config);
	if (rc) {
		fprintf(stderr, "zlog_init_from_string failed\n");
		return EXIT_FAILURE;
	}

	/* Test has_format: "simple" should exist, "detailed" should not */
	printf("=== Test 2: has_format ===\n");
	ASSERT_EQ(zlog_has_format("simple"), 1);
	ASSERT_EQ(zlog_has_format("detailed"), 0);
	ASSERT_EQ(zlog_has_format("nonexistent"), 0);

	/* Test has_category_rules: "*" rule exists, specific ones don't */
	printf("=== Test 3: has_category_rules ===\n");
	ASSERT_EQ(zlog_has_category_rules("*"), 1);
	ASSERT_EQ(zlog_has_category_rules("module_a"), 0);

	/* Test add_format: add a new format */
	printf("=== Test 4: add_format (new) ===\n");
	ASSERT_OK(zlog_add_format("detailed", "%d(%Y-%m-%d %H:%M:%S) [%-5V] [%c] %m%n"));
	ASSERT_EQ(zlog_has_format("detailed"), 1);

	/* Test add_format: override existing format (no error) */
	printf("=== Test 5: add_format (override) ===\n");
	ASSERT_OK(zlog_add_format("simple", "%d [%c] %m%n"));
	ASSERT_EQ(zlog_has_format("simple"), 1);

	/* Test add_rule: add rules for module_a */
	printf("=== Test 6: add_rule ===\n");
	ASSERT_OK(zlog_add_rule("module_a.DEBUG >stdout; simple"));
	ASSERT_EQ(zlog_has_category_rules("module_a"), 1);

	/* Verify module_a can log */
	cat = zlog_get_category("module_a");
	if (!cat) {
		fprintf(stderr, "FAIL: zlog_get_category(module_a) returned NULL\n");
		zlog_fini();
		return EXIT_FAILURE;
	}
	zlog_info(cat, "module_a test log after add_rule");
	zlog_debug(cat, "module_a debug log after add_rule");

	/* Test add_rule: add another rule for module_a (multiple rules per category) */
	printf("=== Test 7: add_rule (multiple for same category) ===\n");
	ASSERT_OK(zlog_add_rule("module_a.ERROR >stderr; detailed"));
	zlog_error(cat, "module_a error log (should appear on stderr too)");

	/* Test add_rule for module_b */
	printf("=== Test 8: add_rule for module_b ===\n");
	ASSERT_OK(zlog_add_rule("module_b.INFO >stdout; detailed"));
	ASSERT_EQ(zlog_has_category_rules("module_b"), 1);
	{
		zlog_category_t *cat_b = zlog_get_category("module_b");
		if (!cat_b) {
			fprintf(stderr, "FAIL: zlog_get_category(module_b) returned NULL\n");
			zlog_fini();
			return EXIT_FAILURE;
		}
		zlog_info(cat_b, "module_b test log");
	}

	/* Test remove_rules: remove module_a rules */
	printf("=== Test 9: remove_rules ===\n");
	rc = zlog_remove_rules("module_a");
	if (rc < 0) {
		fprintf(stderr, "FAIL: zlog_remove_rules returned %d\n", rc);
		zlog_fini();
		return EXIT_FAILURE;
	}
	printf("Removed %d rules for module_a\n", rc);
	ASSERT_EQ(rc, 2); /* we added 2 rules for module_a */
	ASSERT_EQ(zlog_has_category_rules("module_a"), 0);

	/* module_b should still work */
	ASSERT_EQ(zlog_has_category_rules("module_b"), 1);

	/* Test remove_rules for non-existent category: should return 0 */
	printf("=== Test 10: remove_rules (non-existent) ===\n");
	ASSERT_EQ(zlog_remove_rules("nonexistent"), 0);

	/* Test duplicate module loading: add rules, then remove and re-add (simulating reload) */
	printf("=== Test 11: simulate module reload (remove + add) ===\n");
	ASSERT_OK(zlog_add_rule("module_c.DEBUG >stdout; simple"));
	ASSERT_OK(zlog_add_rule("module_c.WARN >stderr; simple"));
	ASSERT_EQ(zlog_has_category_rules("module_c"), 1);

	/* "Reload" module_c with new rules */
	rc = zlog_remove_rules("module_c");
	ASSERT_EQ(rc, 2);
	ASSERT_OK(zlog_add_rule("module_c.ERROR >stdout; detailed"));
	ASSERT_EQ(zlog_has_category_rules("module_c"), 1);

	{
		zlog_category_t *cat_c = zlog_get_category("module_c");
		if (!cat_c) {
			fprintf(stderr, "FAIL: zlog_get_category(module_c) returned NULL\n");
			zlog_fini();
			return EXIT_FAILURE;
		}
		zlog_error(cat_c, "module_c error after reload");
	}

	/* Clean up */
	printf("=== Test 12: cleanup ===\n");
	zlog_fini();

	printf("\nAll modular config tests passed!\n");
	return EXIT_SUCCESS;
}
