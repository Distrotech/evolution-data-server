/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

#define TEST_CONTACT_UID "old-mac-donald-had-a-farm"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

static void
test_preserve_uid (ETestServerFixture *fixture,
                   gconstpointer user_data)
{
	EBookClient *book_client;
	EContact    *contact;
	gchar       *vcard;
	gchar       *uid = NULL;
	GError      *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	vcard = new_vcard_from_test_case ("simple-1");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	e_contact_set (contact, E_CONTACT_UID, TEST_CONTACT_UID);

	if (!e_book_client_add_contact_sync (book_client, contact, &uid, NULL, &error))
		g_error ("Failed to add contact: %s", error->message);

	g_assert_cmpstr (uid, ==, TEST_CONTACT_UID);
	g_object_unref (contact);
	g_free (uid);
}

static void
test_uid_conflict (ETestServerFixture *fixture,
                   gconstpointer user_data)
{
	EBookClient *book_client;
	EContact    *contact;
	gchar       *vcard;
	GError      *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Hijack the first test case, ensure we already have the contact added */
	test_preserve_uid (fixture, user_data);

	vcard = new_vcard_from_test_case ("simple-2");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	e_contact_set (contact, E_CONTACT_UID, TEST_CONTACT_UID);

	if (!e_book_client_add_contact_sync (book_client, contact, NULL, NULL, &error)) {
		g_assert (g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS));
		g_error_free (error);
	} else
		g_error ("Succeeded in adding two contacts with the same UID !");

	g_object_unref (contact);
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
		"/EBookClient/AddContact/PreserveUid",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_preserve_uid,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/AddContact/UidConflict",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_uid_conflict,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
