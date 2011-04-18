/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book-client.h>

#include "client-test-utils.h"

#define NUM_VIEWS 200

static void
contacts_added (EBookView *book_view, const GList *contacts)
{
	GList *l;

	for (l = (GList*)contacts; l; l = l->next) {
		print_email (l->data);
	}
}

static void
contacts_removed (EBookView *book_view, const GList *ids)
{
	GList *l;

	for (l = (GList*)ids; l; l = l->next) {
		printf ("   Removed contact: %s\n", (gchar *)l->data);
	}
}

static void
view_complete (EBookView *book_view, EBookViewStatus status, const gchar *error_msg)
{
	printf ("view_complete (status == %d, error_msg == %s%s%s)\n", status, error_msg ? "'" : "", error_msg ? error_msg : "NULL", error_msg ? "'" : "");
}

static gint
stress_book_views (EBookClient *book_client, gboolean in_thread)
{
	EBookQuery *query;
	EBookView *view = NULL;
	EBookView *new_view;
	gint i;

	g_return_val_if_fail (book_client != NULL, -1);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (book_client), -1);

	query = e_book_query_any_field_contains ("");

	for (i = 0; i < NUM_VIEWS; i++) {
		GError *error = NULL;

		if (!e_book_client_get_view_sync (book_client, query, &new_view, NULL, &error)) {
			report_error ("get book view sync", &error);
			g_object_unref (view);
			e_book_query_unref (query);
			return 1;
		}

		g_signal_connect (new_view, "contacts_added", G_CALLBACK (contacts_added), NULL);
		g_signal_connect (new_view, "contacts_removed", G_CALLBACK (contacts_removed), NULL);
		g_signal_connect (new_view, "view_complete", G_CALLBACK (view_complete), NULL);

		e_book_view_start (new_view);

		if (view) {
			/* wait 100 ms when in a thread */
			if (in_thread)
				g_usleep (100000);

			e_book_view_stop (view);
			g_object_unref (view);
		}

		view = new_view;
	}

	e_book_view_stop (view);
	g_object_unref (view);

	e_book_query_unref (query);

	return 0;
}

static gpointer
stress_book_views_thread (gpointer user_data)
{
	stop_main_loop (stress_book_views (user_data, TRUE));

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	GError *error = NULL;

	main_initialize ();

	printf ("loading addressbook\n");

	book_client = e_book_client_new_system_addressbook (&error);
	if (!book_client) {
		report_error ("create local addressbook", &error);
		return 1;
	}

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		g_object_unref (book_client);
		report_error ("open client sync", &error);
		return 1;
	}

	/* test from main thread */
	stress_book_views (book_client, FALSE);

	/* test from dedicated thread */
	start_in_thread_with_main_loop (stress_book_views_thread, book_client);

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}
