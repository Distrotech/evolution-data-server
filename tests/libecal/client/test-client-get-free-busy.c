/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>
#include <libecal/e-cal-time-util.h>
#include <libical/ical.h>

#include "client-test-utils.h"

static gboolean
test_sync (void)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icaltimezone *utc;
	GSList *users = NULL, *free_busy_ecalcomps = NULL;
	time_t start, end;

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENT, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	/* XXX: create dummy list, which the file backend will ignore */
	users = g_slist_append (users, (gpointer) "user@example.com");

	if (!e_cal_client_get_free_busy_sync (cal_client, start, end, users, &free_busy_ecalcomps, NULL, &error)) {
		report_error ("get free busy sync", &error);
		g_object_unref (cal_client);
		g_slist_free (users);
		return FALSE;
	}

	g_slist_free (users);
	e_cal_client_free_ecalcomp_slist (free_busy_ecalcomps);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	g_object_unref (cal_client);

	return TRUE;
}

/* asynchronous get_free_busy callback with a main-loop running */
static void
async_get_free_busy_result_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *free_busy_ecalcomps = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_free_busy_finish (cal_client, result, &free_busy_ecalcomps, &error)) {
		report_error ("create object finish", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return;
	}

	e_cal_client_free_ecalcomp_slist (free_busy_ecalcomps);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return;
	}

	g_object_unref (cal_client);

	stop_main_loop (0);
}

/* synchronously in idle with main-loop running */
static gboolean
test_sync_in_idle (gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icaltimezone *utc;
	GSList *users = NULL;
	time_t start, end;

	if (!test_sync ()) {
		stop_main_loop (1);
		return FALSE;
	}

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENT, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		stop_main_loop (1);
		return FALSE;
	}

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);
	/* XXX: create dummy list, which the file backend will ignore */
	users = g_slist_append (users, (gpointer) "user@example.com");

	if (!e_cal_client_get_free_busy (cal_client, start, end, users, NULL, async_get_free_busy_result_ready, NULL)) {
		report_error ("get free busy", NULL);
		g_object_unref (cal_client);
		g_slist_free (users);
		stop_main_loop (1);
		return FALSE;
	}

	g_slist_free (users);

	return FALSE;
}

/* synchronously in a dedicated thread with main-loop running */
static gpointer
test_sync_in_thread (gpointer user_data)
{
	if (!test_sync ()) {
		stop_main_loop (1);
		return NULL;
	}

	g_idle_add (test_sync_in_idle, NULL);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	main_initialize ();

	/* synchronously without main-loop */
	if (!test_sync ())
		return 1;

	start_in_thread_with_main_loop (test_sync_in_thread, NULL);

	if (get_main_loop_stop_result () == 0)
		g_print ("Test finished successfully.\n");

	return get_main_loop_stop_result ();
}
