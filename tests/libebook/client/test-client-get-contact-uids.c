/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure_sync         = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async        = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };
static ETestServerClosure book_closure_direct_sync  = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_direct_async = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

static void
test_get_contact_uids_sync (ETestServerFixture *fixture,
                            gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;
	EBookQuery *query;
	gchar *sexp;
	GSList *contacts = NULL;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact)) {
		g_error ("Failed to add contact");
	}
	g_object_unref (contact);

	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "Foo Bar");
	sexp = e_book_query_to_string (query);

	if (!e_book_client_get_contacts_uids_sync (book_client, sexp, &contacts, NULL, &error)) {
		g_error ("get contacts uids: %s", error->message);
	}

	g_assert_cmpint (g_slist_length (contacts), ==, 1);
	e_util_free_string_slist (contacts);

	e_book_query_unref (query);
	g_free (sexp);

}

static void
contacts_ready_cb (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	GError *error = NULL;
	GSList *contacts = NULL;

	if (!e_book_client_get_contacts_uids_finish (E_BOOK_CLIENT (source_object), result, &contacts, &error)) {
		g_error ("get contact finish: %s", error->message);
	} else {

		g_assert_cmpint (g_slist_length (contacts), ==, 1);
		e_util_free_string_slist (contacts);
	}

	g_main_loop_quit (loop);
}

static void
test_get_contact_uids_async (ETestServerFixture *fixture,
                             gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;
	EBookQuery *query;
	gchar *sexp;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact)) {
		g_error ("Failed to add contact");
	}
	g_object_unref (contact);

	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "Foo Bar");
	sexp = e_book_query_to_string (query);

	e_book_client_get_contacts_uids (book_client, sexp, NULL, contacts_ready_cb, fixture->loop);

	e_book_query_unref (query);
	g_free (sexp);

	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/EBookClient/GetContactUids/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_get_contact_uids_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/GetContactUids/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_get_contact_uids_async,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/GetContactUids/Sync",
		ETestServerFixture,
		&book_closure_direct_sync,
		e_test_server_utils_setup,
		test_get_contact_uids_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/GetContactUids/Async",
		ETestServerFixture,
		&book_closure_direct_async,
		e_test_server_utils_setup,
		test_get_contact_uids_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
