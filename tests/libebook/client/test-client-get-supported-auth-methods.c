/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>

#include "client-test-utils.h"

static void
print_values (const GSList *values)
{
	printf ("\tsupported auth methods: ");
	if (!values) {
		printf ("NULL");
	} else {
		while (values) {
			printf ("'%s'", (const gchar *) values->data);
			values = values->next;

			if (values)
				printf (", ");
		}
	}
	printf ("\n");
}

static void
identify_source (ESource *source)
{
	const gchar *name, *uri;

	g_return_if_fail (source != NULL);

	name = e_source_peek_name (source);
	if (!name)
		name = "Unknown name";

	uri = e_source_peek_absolute_uri (source);
	if (!uri)
		uri = e_source_peek_relative_uri (source);
	if (!uri)
		uri = "Unknown uri";

	printf ("\n   Checking source '%s' (%s)\n", name, uri);
}

static void client_opened_async (GObject *source_object, GAsyncResult *result, gpointer async_data);

static void
continue_next_source (gpointer async_data)
{
	ESource *source = NULL;
	EBookClient *book_client;
	GError *error = NULL;

	g_return_if_fail (async_data != NULL);

	while (async_data && foreach_configured_source_async_next (&async_data, &source)) {
		identify_source (source);

		book_client = e_book_client_new (source, &error);
		if (!book_client) {
			report_error ("book client new", &error);
			continue;
		}

		e_client_open (E_CLIENT (book_client), TRUE, NULL, client_opened_async, async_data);
		break;
	}

	if (!async_data)
		stop_main_loop (0);
}

static void
client_got_values_async (GObject *source_object, GAsyncResult *result, gpointer async_data)
{
	GSList *values = NULL;
	GError *error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT (source_object));
	g_return_if_fail (async_data != NULL);

	if (!e_book_client_get_supported_auth_methods_finish (E_BOOK_CLIENT (source_object), result, &values, &error)) {
		report_error ("get supported auth methods finish", &error);
	} else {
		print_values (values);

		e_client_util_free_string_slist (values);
	}

	g_object_unref (source_object);

	continue_next_source (async_data);
}

static void
client_opened_async (GObject *source_object, GAsyncResult *result, gpointer async_data)
{
	GError *error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT (source_object));
	g_return_if_fail (async_data != NULL);

	if (!e_client_open_finish (E_CLIENT (source_object), result, &error)) {
		report_error ("client open finish", &error);
		g_object_unref (source_object);
		continue_next_source (async_data);
		return;
	}

	e_book_client_get_supported_auth_methods (E_BOOK_CLIENT (source_object), NULL, client_got_values_async, async_data);
}

static void
check_source_sync (ESource *source)
{
	EBookClient *book_client;
	GError *error = NULL;
	GSList *values = NULL;

	g_return_if_fail (source != NULL);

	identify_source (source);

	book_client = e_book_client_new (source, &error);
	if (!book_client) {
		report_error ("book client new", &error);
		return;
	}

	if (!e_client_open_sync (E_CLIENT (book_client), TRUE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return;
	}

	if (!e_book_client_get_supported_auth_methods_sync (book_client, &values, NULL, &error)) {
		report_error ("get supported auth methods sync", &error);
		g_object_unref (book_client);
		return;
	}

	print_values (values);

	g_slist_foreach (values, (GFunc) g_free, NULL);
	g_slist_free (values);

	g_object_unref (book_client);
}

static gboolean
in_main_thread_idle_cb (gpointer unused)
{
	gpointer async_data;
	ESource *source = NULL;
	EBookClient *book_client;
	GError *error = NULL;

	printf ("* run in main thread with mainloop running\n");
	foreach_configured_source (check_source_sync);
	printf ("---------------------------------------------------------\n\n");

	async_data = foreach_configured_source_async_start (&source);
	if (!async_data) {
		stop_main_loop (1);
		return FALSE;
	}

	printf ("* run in main thread async\n");

	identify_source (source);

	while (book_client = e_book_client_new (source, &error), !book_client) {
		report_error ("book client new", &error);

		if (!foreach_configured_source_async_next (&async_data, &source)) {
			stop_main_loop (0);
			return FALSE;
		}

		identify_source (source);
	}

	e_client_open (E_CLIENT (book_client), TRUE, NULL, client_opened_async, async_data);

	return FALSE;
}

static gpointer
worker_thread (gpointer unused)
{
	printf ("* run in dedicated thread with mainloop running\n");
	foreach_configured_source (check_source_sync);
	printf ("---------------------------------------------------------\n\n");

	g_idle_add (in_main_thread_idle_cb, NULL);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	main_initialize ();

	printf ("* run in main thread without mainloop\n");
	foreach_configured_source (check_source_sync);
	printf ("---------------------------------------------------------\n\n");

	start_in_thread_with_main_loop (worker_thread, NULL);

	return get_main_loop_stop_result ();
}
