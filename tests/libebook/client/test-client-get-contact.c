/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };
static ETestServerClosure direct_book_closure = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0 };

static void
test_get_contact_sync (ETestServerFixture *fixture,
                       gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact)) {
		g_error ("Failed to get contact");
	}

	g_assert (contact != NULL);
	g_object_unref (contact);
}

static void
contact_ready_cb (GObject *source_object,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	EContact *contact;
	GError *error = NULL;

	if (!e_book_client_get_contact_finish (E_BOOK_CLIENT (source_object), result, &contact, &error)) {
		g_error ("get contact finish: %s", error->message);
	}

	g_object_unref (contact);
	g_main_loop_quit (loop);
}

static void
test_get_contact_async (ETestServerFixture *fixture,
                        gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact)) {
		g_error ("Failed to get contact");
	}

	e_book_client_get_contact (
		book_client,
		e_contact_get_const (contact, E_CONTACT_UID),
		NULL, contact_ready_cb, fixture->loop);
	g_object_unref (contact);
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
		"/EBookClient/GetContact/Sync",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_get_contact_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/GetContact/Async",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_get_contact_async,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/GetContact/Sync",
		ETestServerFixture,
		&direct_book_closure,
		e_test_server_utils_setup,
		test_get_contact_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/GetContact/Async",
		ETestServerFixture,
		&direct_book_closure,
		e_test_server_utils_setup,
		test_get_contact_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
