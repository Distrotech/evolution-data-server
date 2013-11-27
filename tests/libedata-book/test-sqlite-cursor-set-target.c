/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

static EbSqlCursorClosure book_closure = { { FALSE, e_sqlite_cursor_fixture_setup_book }, NULL, E_BOOK_CURSOR_SORT_ASCENDING };

/*****************************************************
 *          Expect the same results twice            *
 *****************************************************/
static void
test_cursor_set_target_reset_cursor (EbSqlCursorFixture *fixture,
				     gconstpointer  user_data)
{
	GSList *results = NULL;
	GError *error = NULL;

	/* First batch */
	if (e_book_sqlite_cursor_step (((EbSqlFixture *) fixture)->ebsql,
				       fixture->cursor,
				       EBSQL_CURSOR_STEP_MOVE | EBSQL_CURSOR_STEP_FETCH,
				       EBSQL_CURSOR_ORIGIN_BEGIN,
				       5, &results, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* Assert the first 5 contacts in en_US order */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-11",
			       "sorted-1",
			       "sorted-2",
			       "sorted-5",
			       "sorted-6",
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_sqlite_search_data_free, NULL);
	g_slist_free (results);
	results = NULL;

	/* Second batch reset (same results) */
	if (e_book_sqlite_cursor_step (((EbSqlFixture *) fixture)->ebsql,
				       fixture->cursor,
				       EBSQL_CURSOR_STEP_MOVE | EBSQL_CURSOR_STEP_FETCH,
				       EBSQL_CURSOR_ORIGIN_BEGIN,
				       5, &results, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* Assert the first 5 contacts in en_US order again */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-11",
			       "sorted-1",
			       "sorted-2",
			       "sorted-5",
			       "sorted-6",
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_sqlite_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 * Expect results with family name starting with 'C' *
 *****************************************************/
static void
test_cursor_set_target_c_next_results (EbSqlCursorFixture *fixture,
				       gconstpointer  user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
	ECollator *collator;
	gint n_labels;
	const gchar *const *labels;

	/* First verify our test... in en_US locale the label 'C' should exist with the index 3 */
	collator = e_book_sqlite_ref_collator (((EbSqlFixture *) fixture)->ebsql);
	labels = e_collator_get_index_labels (collator, &n_labels, NULL, NULL, NULL);
	g_assert_cmpstr (labels[3], ==, "C");
	e_collator_unref (collator);

	/* Set the cursor at the start of family names beginning with 'C' */
	e_book_sqlite_cursor_set_target_alphabetic_index (((EbSqlFixture *) fixture)->ebsql,
							  fixture->cursor, 3);

	if (e_book_sqlite_cursor_step (((EbSqlFixture *) fixture)->ebsql,
				       fixture->cursor,
				       EBSQL_CURSOR_STEP_MOVE | EBSQL_CURSOR_STEP_FETCH,
				       EBSQL_CURSOR_ORIGIN_CURRENT,
				       5, &results, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* Assert that we got the results starting at C */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-10",
			       "sorted-14",
			       "sorted-12",
			       "sorted-13",
			       "sorted-9",
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_sqlite_search_data_free, NULL);
	g_slist_free (results);
}

/*****************************************************
 *       Expect results before the letter 'C'        *
 *****************************************************/
static void
test_cursor_set_target_c_prev_results (EbSqlCursorFixture *fixture,
				       gconstpointer  user_data)
{
	GSList *results = NULL;
	GError *error = NULL;
	ECollator *collator;
	gint n_labels;
	const gchar *const *labels;

	/* First verify our test... in en_US locale the label 'C' should exist with the index 3 */
	collator = e_book_sqlite_ref_collator (((EbSqlFixture *) fixture)->ebsql);
	labels = e_collator_get_index_labels (collator, &n_labels, NULL, NULL, NULL);
	g_assert_cmpstr (labels[3], ==, "C");
	e_collator_unref (collator);

	/* Set the cursor at the start of family names beginning with 'C' */
	e_book_sqlite_cursor_set_target_alphabetic_index (((EbSqlFixture *) fixture)->ebsql,
							  fixture->cursor, 3);

	if (e_book_sqlite_cursor_step (((EbSqlFixture *) fixture)->ebsql,
				       fixture->cursor, 
				       EBSQL_CURSOR_STEP_MOVE | EBSQL_CURSOR_STEP_FETCH,
				       EBSQL_CURSOR_ORIGIN_CURRENT,
				       -5, &results, &error) < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);

	/* Assert that we got the results before C */
	g_assert_cmpint (g_slist_length (results), ==, 5);
	assert_contacts_order (results,
			       "sorted-18",
			       "sorted-16",
			       "sorted-17",
			       "sorted-15",
			       "sorted-8",
			       NULL);

	g_slist_foreach (results, (GFunc)e_book_sqlite_search_data_free, NULL);
	g_slist_free (results);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/EbSqlCursor/SetTarget/ResetCursor", EbSqlCursorFixture, &book_closure,
		    e_sqlite_cursor_fixture_setup,
		    test_cursor_set_target_reset_cursor,
		    e_sqlite_cursor_fixture_teardown);
	g_test_add ("/EbSqlCursor/SetTarget/Alphabetic/C/NextResults", EbSqlCursorFixture, &book_closure,
		    e_sqlite_cursor_fixture_setup,
		    test_cursor_set_target_c_next_results,
		    e_sqlite_cursor_fixture_teardown);
	g_test_add ("/EbSqlCursor/SetTarget/Alphabetic/C/PreviousResults", EbSqlCursorFixture, &book_closure,
		    e_sqlite_cursor_fixture_setup,
		    test_cursor_set_target_c_prev_results,
		    e_sqlite_cursor_fixture_teardown);

	return g_test_run ();
}
