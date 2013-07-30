/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-test-server-utils.c - Test scaffolding to run tests with in-tree data server.
 *
 * Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "e-test-server-utils.h"

#include <sys/wait.h>

#define ADDRESS_BOOK_SOURCE_UID "test-address-book"
#define CALENDAR_SOURCE_UID     "test-calendar"

/* FIXME, currently we are unable to achieve server activation
 * twice in a single test case, so we're using one D-Bus server
 * throughout an entire test suite.
 *
 * When this is fixed we can migrate the D-Bus initialization
 * and teardown from e_test_server_utils_run() to
 * e_test_server_utils_setup() and e_test_server_utils_teardown()
 * and this will transparantly change the way tests run using
 * this test framework.
 */
#define GLOBAL_DBUS_DAEMON 1

#if GLOBAL_DBUS_DAEMON
static GTestDBus *global_test_dbus = NULL;
#endif

/* The ESource identifier numerical component, this should
 * not be needed (and should probably be removed) once we
 * can get rid of the GLOBAL_DBUS_DAEMON hack.
 */
static gint global_test_source_id = 0;

typedef struct {
	ETestServerFixture *fixture;
	ETestServerClosure *closure;
} FixturePair;

static gboolean
test_installed_services (void)
{
	static gint use_installed_services = -1;

	if (use_installed_services < 0) {
		if (g_getenv ("TEST_INSTALLED_SERVICES") != NULL)
			use_installed_services = 1;
		else
			use_installed_services = 0;
	}
	return use_installed_services;
}

static void
setup_environment (void)
{
	g_assert (g_setenv ("XDG_DATA_HOME", EDS_TEST_WORK_DIR, TRUE));
	g_assert (g_setenv ("XDG_CACHE_HOME", EDS_TEST_WORK_DIR, TRUE));
	g_assert (g_setenv ("XDG_CONFIG_HOME", EDS_TEST_WORK_DIR, TRUE));
	g_assert (g_setenv ("GSETTINGS_SCHEMA_DIR", EDS_TEST_SCHEMA_DIR, TRUE));
	g_assert (g_setenv ("EDS_CALENDAR_MODULES", EDS_TEST_CALENDAR_DIR, TRUE));
	g_assert (g_setenv ("EDS_ADDRESS_BOOK_MODULES", EDS_TEST_ADDRESS_BOOK_DIR, TRUE));
	g_assert (g_setenv ("EDS_REGISTRY_MODULES", EDS_TEST_REGISTRY_DIR, TRUE));
	g_assert (g_setenv ("EDS_CAMEL_PROVIDER_DIR", EDS_TEST_CAMEL_DIR, TRUE));
	g_assert (g_setenv ("GIO_USE_VFS", "local", TRUE));
	g_assert (g_setenv ("EDS_TESTING", "1", TRUE));
	g_assert (g_setenv ("GSETTINGS_BACKEND", "memory", TRUE));

	g_unsetenv ("DISPLAY");
}

static void
delete_work_directory (void)
{
	/* XXX Instead of complex error checking here, we should ideally use
	 * a recursive GDir / g_unlink() function.
	 *
	 * We cannot use GFile and the recursive delete function without
	 * corrupting our contained D-Bus environment with service files
	 * from the OS.
	 */
	const gchar *argv[] = { "/bin/rm", "-rf", EDS_TEST_WORK_DIR, NULL };
	gboolean spawn_succeeded;
	gint exit_status;

	spawn_succeeded = g_spawn_sync (
		NULL, (gchar **) argv, NULL, 0, NULL, NULL,
					NULL, NULL, &exit_status, NULL);

	g_assert (spawn_succeeded);
	g_assert (WIFEXITED (exit_status));
	g_assert_cmpint (WEXITSTATUS (exit_status), ==, 0);
}

static gboolean
e_test_server_utils_bootstrap_timeout (FixturePair *pair)
{
	g_error ("Timed out while waiting for ESource creation from the registry");

	pair->fixture->timeout_source_id = 0;
	return FALSE;
}

static void
registry_weak_notify (gpointer data,
                      GObject *where_the_object_was)
{
	ETestServerFixture *fixture = (ETestServerFixture *) data;

	fixture->registry_finalized = TRUE;
}

static void
client_weak_notify (gpointer data,
                    GObject *where_the_object_was)
{
	ETestServerFixture *fixture = (ETestServerFixture *) data;

	fixture->client_finalized = TRUE;
}

static void
e_test_server_utils_client_ready (GObject *source_object,
				  GAsyncResult *res,
				  gpointer user_data)
{
	FixturePair *pair = (FixturePair *)user_data;
	GError *error = NULL;

	switch (pair->closure->type) {
	case E_TEST_SERVER_ADDRESS_BOOK:
		pair->fixture->service.book_client = (EBookClient *)
			e_book_client_connect_finish (res, &error);

		if (!pair->fixture->service.book_client)
			g_error ("Unable to create the test book: %s", error->message);

		g_object_weak_ref (G_OBJECT (pair->fixture->service.book_client),
				   client_weak_notify, pair->fixture);
		break;
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:
		pair->fixture->service.book_client = (EBookClient *)
			e_book_client_connect_direct_finish (res, &error);

		if (!pair->fixture->service.book_client)
			g_error ("Unable to create the test book: %s", error->message);

		g_object_weak_ref (G_OBJECT (pair->fixture->service.book_client),
				   client_weak_notify, pair->fixture);
		break;
	case E_TEST_SERVER_CALENDAR:
		pair->fixture->service.calendar_client = (ECalClient *)
			e_cal_client_connect_finish (res, &error);

		if (!pair->fixture->service.calendar_client)
			g_error ("Unable to create the test calendar: %s", error->message);

		g_object_weak_ref (G_OBJECT (pair->fixture->service.calendar_client),
				   client_weak_notify, pair->fixture);
		break;
	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:
	case E_TEST_SERVER_DEPRECATED_CALENDAR:
	case E_TEST_SERVER_NONE:
		g_assert_not_reached ();
	}

	g_main_loop_quit (pair->fixture->loop);
}

static void
e_test_server_utils_source_added (ESourceRegistry *registry,
                                  ESource *source,
                                  FixturePair *pair)
{
	GError  *error = NULL;

	if (g_strcmp0 (e_source_get_uid (source), pair->fixture->source_name) != 0)
		return;

	switch (pair->closure->type) {
	case E_TEST_SERVER_ADDRESS_BOOK:
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:

		if (pair->closure->type == E_TEST_SERVER_DIRECT_ADDRESS_BOOK) {
			if (pair->closure->use_async_connect)
				e_book_client_connect_direct (source, NULL, e_test_server_utils_client_ready, pair);
			else
				pair->fixture->service.book_client = (EBookClient *)
					e_book_client_connect_direct_sync (pair->fixture->registry,
									   source, NULL, &error);
		} else {

			if (pair->closure->use_async_connect)
				e_book_client_connect (source, NULL, e_test_server_utils_client_ready, pair);
			else
				pair->fixture->service.book_client = (EBookClient *)
					e_book_client_connect_sync (source, NULL, &error);
		}

		if (!pair->closure->use_async_connect) {
			if (!pair->fixture->service.book_client)
				g_error ("Unable to create the test book: %s", error->message);

			g_object_weak_ref (G_OBJECT (pair->fixture->service.book_client),
					   client_weak_notify, pair->fixture);
		}

		break;

	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:

		/* Dont bother testing the Async apis for deprecated APIs */
		pair->fixture->service.book = e_book_new (source, &error);
		if (!pair->fixture->service.book)
			g_error ("Unable to create the test book: %s", error->message);

		if (!e_book_open (pair->fixture->service.book, FALSE, &error))
			g_error ("Unable to open book: %s", error->message);

		g_object_weak_ref (
			G_OBJECT (pair->fixture->service.book),
			client_weak_notify, pair->fixture);

		break;

	case E_TEST_SERVER_CALENDAR:

		if (pair->closure->use_async_connect) {
			e_cal_client_connect (source, pair->closure->calendar_source_type,
					      NULL, e_test_server_utils_client_ready, pair);

		} else {

			pair->fixture->service.calendar_client = (ECalClient *)
				e_cal_client_connect_sync (
				        source,
					pair->closure->calendar_source_type, NULL, &error);
			if (!pair->fixture->service.calendar_client)
				g_error ("Unable to create the test calendar: %s", error->message);

			g_object_weak_ref (G_OBJECT (pair->fixture->service.calendar_client),
					   client_weak_notify, pair->fixture);
		}

		break;

	case E_TEST_SERVER_DEPRECATED_CALENDAR:

		/* Dont bother testing the Async apis for deprecated APIs */
		pair->fixture->service.calendar = e_cal_new (source, pair->closure->calendar_source_type);
		if (!pair->fixture->service.calendar)
			g_error ("Unable to create the test calendar");

		if (!e_cal_open (pair->fixture->service.calendar, FALSE, &error))
			g_error ("Unable to open calendar: %s", error->message);

		g_object_weak_ref (
			G_OBJECT (pair->fixture->service.calendar),
			client_weak_notify, pair->fixture);

		break;

	case E_TEST_SERVER_NONE:
		return;
	}

	if (!pair->closure->use_async_connect)
		g_main_loop_quit (pair->fixture->loop);
}

static gboolean
e_test_server_utils_bootstrap_idle (FixturePair *pair)
{
	ESourceBackend *backend = NULL;
	ESource *scratch = NULL;
	GError  *error = NULL;

	pair->fixture->registry = e_source_registry_new_sync (NULL, &error);

	if (!pair->fixture->registry)
		g_error ("Unable to create the test registry: %s", error->message);

	g_object_weak_ref (
		G_OBJECT (pair->fixture->registry),
		registry_weak_notify, pair->fixture);

	g_signal_connect (
		pair->fixture->registry, "source-added",
		G_CALLBACK (e_test_server_utils_source_added), pair);

	/* Create an address book */
	switch (pair->closure->type) {
	case E_TEST_SERVER_ADDRESS_BOOK:
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:
	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:

		if (!pair->fixture->source_name)
			pair->fixture->source_name = g_strdup_printf ("%s-%d", ADDRESS_BOOK_SOURCE_UID, global_test_source_id++);

		scratch = e_source_new_with_uid (pair->fixture->source_name, NULL, &error);
		if (!scratch)
			g_error ("Failed to create scratch source for an addressbook: %s", error->message);

		/* Ensure Book type */
		backend = e_source_get_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
		e_source_backend_set_backend_name (backend, "local");

		break;
	case E_TEST_SERVER_CALENDAR:
	case E_TEST_SERVER_DEPRECATED_CALENDAR:

		if (!pair->fixture->source_name)
			pair->fixture->source_name = g_strdup_printf ("%s-%d", CALENDAR_SOURCE_UID, global_test_source_id++);

		scratch = e_source_new_with_uid (pair->fixture->source_name, NULL, &error);
		if (!scratch)
			g_error ("Failed to create scratch source for a calendar: %s", error->message);

		/* Ensure Calendar type source (how to specify the backend here ?? */
		backend = e_source_get_extension (scratch, E_SOURCE_EXTENSION_CALENDAR);
		e_source_backend_set_backend_name (backend, "local");

		break;

	case E_TEST_SERVER_NONE:
		break;
	}

	if (scratch) {
		if (pair->closure->customize)
			pair->closure->customize (scratch, pair->closure);

		if (!e_source_registry_commit_source_sync (pair->fixture->registry, scratch, NULL, &error)) {
			/* Allow sources to carry from one test to the next, if the keep_work_directory
			 * semantics are used then that's what we want (to reuse a source from the
			 * previous test case).
			 */
			if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				ESource *source = e_source_registry_ref_source (
					pair->fixture->registry,
					pair->fixture->source_name);

				g_clear_error (&error);

				g_assert (E_IS_SOURCE (source));

				e_test_server_utils_source_added (pair->fixture->registry, source, pair);
				g_object_unref (source);
			} else
				g_error ("Unable to add new addressbook source to the registry: %s", error->message);
		}

		g_object_unref (scratch);
	}

	if (pair->closure->type != E_TEST_SERVER_NONE)
		pair->fixture->timeout_source_id =
			g_timeout_add (20 * 1000, (GSourceFunc) e_test_server_utils_bootstrap_timeout, pair);
	else
		g_main_loop_quit (pair->fixture->loop);

	return FALSE;
}

/**
 * e_test_server_utils_setup:
 * @fixture: A #ETestServerFixture
 * @user_data: A #ETestServerClosure or derived structure provided by the test.
 *
 * A setup function for the #ETestServerFixture fixture
 */
void
e_test_server_utils_setup (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	ETestServerClosure *closure = (ETestServerClosure *) user_data;
	FixturePair         pair    = { fixture, closure };

	/* Create work directory */
	g_assert (g_mkdir_with_parents (EDS_TEST_WORK_DIR, 0755) == 0);

	fixture->loop = g_main_loop_new (NULL, FALSE);

	if (!test_installed_services ()) {
#if !GLOBAL_DBUS_DAEMON
		/* Create the global dbus-daemon for this test suite */
		fixture->dbus = g_test_dbus_new (G_TEST_DBUS_NONE);

		/* Add the private directory with our in-tree service files */
		g_test_dbus_add_service_dir (fixture->dbus, EDS_TEST_DBUS_SERVICE_DIR);

		/* Start the private D-Bus daemon */
		g_test_dbus_up (fixture->dbus);
#else
		fixture->dbus = global_test_dbus;
#endif
	}

	g_idle_add ((GSourceFunc) e_test_server_utils_bootstrap_idle, &pair);
	g_main_loop_run (fixture->loop);

	/* This needs to be explicitly removed, otherwise the timeout source
	 * stays in the default GMainContext and after running tests for 20 seconds
	 * in the same test suite... the tests bail out.
	 */
	if (fixture->timeout_source_id) {
		g_source_remove (fixture->timeout_source_id);
		fixture->timeout_source_id = 0;
	}

	g_signal_handlers_disconnect_by_func (fixture->registry, e_test_server_utils_source_added, &pair);
}

/**
 * e_test_server_utils_teardown:
 * @fixture: A #ETestServerFixture
 * @user_data: A #ETestServerClosure or derived structure provided by the test.
 *
 * A teardown function for the #ETestServerFixture fixture
 */
void
e_test_server_utils_teardown (ETestServerFixture *fixture,
                              gconstpointer user_data)
{
	ETestServerClosure *closure = (ETestServerClosure *) user_data;
	GError             *error = NULL;

	switch (closure->type) {
	case E_TEST_SERVER_ADDRESS_BOOK:
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:
		if (!closure->keep_work_directory && !e_client_remove_sync (E_CLIENT (fixture->service.book_client), NULL, &error)) {
			g_message ("Failed to remove test book: %s (ignoring)", error->message);
			g_clear_error (&error);
		}
		g_object_unref (fixture->service.book_client);
		fixture->service.book_client = NULL;
		break;

	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:
		if (!closure->keep_work_directory && !e_book_remove (fixture->service.book, &error)) {
			g_message ("Failed to remove test book: %s (ignoring)", error->message);
			g_clear_error (&error);
		}
		g_object_unref (fixture->service.book);
		fixture->service.book = NULL;
		break;

	case E_TEST_SERVER_CALENDAR:
		if (!closure->keep_work_directory && !e_client_remove_sync (E_CLIENT (fixture->service.calendar_client), NULL, &error)) {
			g_message ("Failed to remove test calendar: %s (ignoring)", error->message);
			g_clear_error (&error);
		}
		g_object_unref (fixture->service.calendar_client);
		fixture->service.calendar_client = NULL;
		break;

	case E_TEST_SERVER_DEPRECATED_CALENDAR:
		if (!closure->keep_work_directory && !e_cal_remove (fixture->service.calendar, &error)) {
			g_message ("Failed to remove test calendar: %s (ignoring)", error->message);
			g_clear_error (&error);
		}
		g_object_unref (fixture->service.calendar);
		fixture->service.calendar = NULL;

	case E_TEST_SERVER_NONE:
		break;
	}

	if (closure->type != E_TEST_SERVER_NONE &&
	    fixture->client_finalized == FALSE)
		g_error ("Failed to destroy client while tearing down test case; reference count imbalance");

	g_free (fixture->source_name);
	g_object_run_dispose (G_OBJECT (fixture->registry));
	g_object_unref (fixture->registry);
	fixture->registry = NULL;

	if (fixture->registry_finalized == FALSE)
		g_error ("Failed to destroy registry while tearing down test case; reference count imbalance");

	g_main_loop_unref (fixture->loop);
	fixture->loop = NULL;

	if (!test_installed_services ()) {
#if !GLOBAL_DBUS_DAEMON
		/* Teardown the D-Bus Daemon
		 *
		 * Note that we intentionally leak the TestDBus daemon
		 * in this case, presumably this is due to some leaked
		 * GDBusConnection reference counting
		 */
		g_test_dbus_down (fixture->dbus);
		g_object_unref (fixture->dbus);
		fixture->dbus = NULL;
#else
		fixture->dbus = NULL;
#endif
	}

	/* Cleanup work directory
	 *
	 * XXX This is avoided for now since we are currently using
	 * a separate ESource UID for each test, removing the work directory
	 * would cause the cache-reaper module to spew error messages when
	 * attempting to move missing removed ESources to the trash.
	 *
	 * This should probably be all completely removed once the
	 * GLOBAL_DBUS_DAEMON clauses can be removed.
	 */
	/* if (!closure->keep_work_directory && !test_installed_services ()) */
	/* 	delete_work_directory (); */

	/* Destroy dynamically allocated closure */
	if (closure->destroy_closure_func)
		closure->destroy_closure_func (closure);
}

gint
e_test_server_utils_run (void)
{
	gint tests_ret;

	/* Cleanup work directory */
	delete_work_directory ();

	setup_environment ();

#if GLOBAL_DBUS_DAEMON
	if (!test_installed_services ()) {
		/* Create the global dbus-daemon for this test suite */
		global_test_dbus = g_test_dbus_new (G_TEST_DBUS_NONE);

		/* Add the private directory with our in-tree service files */
		g_test_dbus_add_service_dir (global_test_dbus, EDS_TEST_DBUS_SERVICE_DIR);

		/* Start the private D-Bus daemon */
		g_test_dbus_up (global_test_dbus);
	}
#endif

	/* Run the GTest suite */
	tests_ret = g_test_run ();

#if GLOBAL_DBUS_DAEMON
	if (!test_installed_services ()) {
		/* Teardown the D-Bus Daemon
		 *
		 * Note that we intentionally leak the TestDBus daemon
		 * in this case, presumably this is due to some leaked
		 * GDBusConnection reference counting
		 */
		g_test_dbus_stop (global_test_dbus);
		/* g_object_unref (global_test_dbus); */
		global_test_dbus = NULL;
	}
#endif

	return tests_ret;
}
