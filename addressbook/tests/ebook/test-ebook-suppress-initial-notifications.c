/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/e-book.h>

#include "ebook-test-utils.h"

#define NOTIFICATION_WAIT 2000

static GMainLoop *loop;

static gboolean loading_view;

static void
add_contact (EBook *book)
{
	EContact *contact = e_contact_new ();
	EContact *final;
	const gchar *uid;

	e_contact_set (contact, E_CONTACT_FULL_NAME, "Micheal Jackson");

	uid   = ebook_test_utils_book_add_contact (book, contact);
	final = ebook_test_utils_book_get_contact (book, uid);

	/* verify the contact was added "successfully" (not thorough) */
	g_assert (ebook_test_utils_contacts_are_equal_shallow (contact, final));

	test_print ("Added contact\n");
}

static void
setup_book (EBook     **book_out)
{
	EBook *book;

	book = ebook_test_utils_book_new_temp (NULL);
	ebook_test_utils_book_open (book, FALSE);

	ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", NULL);
	ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-2", NULL);
	ebook_test_utils_book_add_contact_from_test_case_verify (book, "name-only", NULL);

	*book_out = book;
}

static void
print_contact (EContact *contact)
{
	GList *emails, *e;

	test_print ("Contact: %s\n", (gchar *)e_contact_get_const (contact, E_CONTACT_FULL_NAME));
	test_print ("UID: %s\n", (gchar *)e_contact_get_const (contact, E_CONTACT_UID));
	test_print ("Email addresses:\n");

	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		test_print ("\t%s\n",  (gchar *)e->data);
	}
	g_list_foreach (emails, (GFunc)g_free, NULL);
	g_list_free (emails);

	test_print ("\n");
}

static void
finish_test (EBookView *book_view)
{
	e_book_view_stop (book_view);
	g_object_unref (book_view);
	g_main_loop_quit (loop);
}

static void
contacts_added (EBookView *book_view, const GList *contacts)
{
	GList *l;

	if (loading_view)
		g_error ("Expected no notifications while loading the view");
	else {
		/* We quit the mainloop and the test succeeds if we get the notification
		 * for the contact we add after loading the view completes */
		for (l = (GList*)contacts; l; l = l->next) {
			print_contact (l->data);
		}

		finish_test (book_view);
	}
}

static void
contacts_removed (EBookView *book_view, const GList *ids)
{
	GList *l;

	if (loading_view)
		g_error ("Expected no notifications while loading the view");

	for (l = (GList*)ids; l; l = l->next) {
		test_print ("Removed contact: %s\n", (gchar *)l->data);
	}
}

static gboolean
add_contact_timeout (EBookView *book_view)
{
	if (book_view)
		g_error ("Timed out waiting for notification of added contact");

	return FALSE;
}

static void
view_complete (EBookView *book_view, EBookViewStatus status, const gchar *error_msg)
{
	test_print ("Loading view complete\n");

	/* Now add a contact and assert that we received notification */
	loading_view = FALSE;
	add_contact (e_book_view_get_book (book_view));

	g_timeout_add (NOTIFICATION_WAIT, (GSourceFunc)add_contact_timeout, book_view);
}

static void
setup_and_start_view (EBookView *view)
{
	g_signal_connect (view, "contacts_added", G_CALLBACK (contacts_added), NULL);
	g_signal_connect (view, "contacts_removed", G_CALLBACK (contacts_removed), NULL);
	g_signal_connect (view, "view_complete", G_CALLBACK (view_complete), NULL);

	loading_view = TRUE;

	/* Set flags to 0, i.e. unflag E_BOOK_VIEW_NOTIFY_INITIAL */
	e_book_view_set_flags (view, 0);
	e_book_view_start (view);
}

static void
get_book_view_cb (EBookTestClosure *closure)
{
	g_assert (closure->view);

	setup_and_start_view (closure->view);
}

gint
main (gint argc, gchar **argv)
{
	EBook *book;
	EBookQuery *query;
	EBookView *view;

	g_type_init ();

	/*
	 * Sync version
	 */
	setup_book (&book);
	query = e_book_query_any_field_contains ("");
	ebook_test_utils_book_get_book_view (book, query, NULL, &view);
	setup_and_start_view (view);

	loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (loop);

        e_book_query_unref (query);
	ebook_test_utils_book_remove (book);

	/*
	 * Async version
	 */
	setup_book (&book);
	query = e_book_query_any_field_contains ("");

	loop = g_main_loop_new (NULL, TRUE);
	ebook_test_utils_book_async_get_book_view (book, query, NULL,
			(GSourceFunc) get_book_view_cb, loop);

	g_main_loop_run (loop);

        e_book_query_unref (query);
	ebook_test_utils_book_remove (book);

	return 0;
}
