/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

static EbSqlClosure closure = { FALSE, NULL };

static void
test_get_contact (EbSqlFixture   *fixture,
		  gconstpointer   user_data)
{
	EContact *contact = NULL;
	EContact *other = NULL;
	GError *error = NULL;

	add_contact_from_test_case (fixture, "simple-1", &contact);

	if (!e_book_sqlite_get_contact (fixture->ebsql,
					(const gchar *)e_contact_get_const (contact, E_CONTACT_UID),
					FALSE,	
					&other,
					&error))
		g_error ("Failed to get contact with uid '%s': %s",
			 (const gchar *)e_contact_get_const (contact, E_CONTACT_UID),
			 error->message);

	g_object_unref (contact);
	g_object_unref (other);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Ensure that the client and server get the same locale */
	g_assert (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/EBookSqlite/GetContact", EbSqlFixture, &closure,
		    e_sqlite_fixture_setup, test_get_contact, e_sqlite_fixture_teardown);

	return g_test_run ();
}
