/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <string.h>
#include <libecal/e-cal-client.h>
#include <libedataserver/e-source-group.h>

#include "client-test-utils.h"

static gint running_async = 0;

typedef struct _ExtraValues {
	gpointer async_data;

	const gchar *cache_dir;
	gchar *cal_address;
	gchar *alarm_email_address;
	icalcomponent *default_object;
} ExtraValues;

static void
extra_values_free (ExtraValues *evals)
{
	if (!evals)
		return;

	if (evals->default_object)
		icalcomponent_free (evals->default_object);
	g_free (evals->cal_address);
	g_free (evals->alarm_email_address);
	g_free (evals);
}

static void
print_values (const GSList *values, const ExtraValues *evals, EClient *client)
{
	g_return_if_fail (evals != NULL);

	g_print ("\treadonly:%s online:%s\n", e_client_is_readonly (client) ? "yes" : "no", e_client_is_online (client) ? "yes" : "no");
	g_print ("\tcal address: %s%s%s\n", evals->cal_address ? "'" : "", evals->cal_address ? evals->cal_address : "none", evals->cal_address ? "'" : "");
	g_print ("\talarm email address: %s%s%s\n", evals->alarm_email_address ? "'" : "", evals->alarm_email_address ? evals->alarm_email_address : "none", evals->alarm_email_address ? "'" : "");
	g_print ("\tcache dir: %s%s%s\n", evals->cache_dir ? "'" : "", evals->cache_dir ? evals->cache_dir : "none", evals->cache_dir ? "'" : "");
	g_print ("\tcapabilities: ");
	if (!values) {
		g_print ("NULL");
		if (e_client_get_capabilities (client))
			g_print (", but client has %d capabilities", g_slist_length ((GSList *) e_client_get_capabilities (client)));
	} else {
		gint client_caps = g_slist_length ((GSList *) e_client_get_capabilities (client)), my_caps = g_slist_length ((GSList *) values);

		while (values) {
			const gchar *cap = values->data;

			g_print ("'%s'", cap);
			if (!e_client_check_capability (client, cap))
				g_print (" (not found in EClient)");

			values = values->next;

			if (values)
				g_print (", ");
		}

		if (client_caps != my_caps) {
			g_print ("\n\t * has different count of capabilities in EClient (%d) and returned (%d)", client_caps, my_caps);
		}

	}
	g_print ("\n");
	g_print ("\tdefault object: %s\n", evals->default_object ? "" : "none");
	if (evals->default_object) {
		gchar *comp_str = icalcomponent_as_ical_string_r (evals->default_object);
		const gchar *p = comp_str, *n;
		while (n = strchr (p, '\n'), p) {
			if (!n) {
				g_print ("\t   %s\n", p);
				break;
			} else {
				g_print ("\t   %.*s\n", (gint) (n - p), p);
				n++;
			}

			p = n;
		}

		g_free (comp_str);
	}
}

static void
identify_source (ESource *source, ECalClientSourceType source_type)
{
	const gchar *name, *uri, *type;
	gchar *abs_uri = NULL;

	g_return_if_fail (source != NULL);

	switch (source_type) {
	case E_CAL_CLIENT_SOURCE_TYPE_EVENT:
		type = "events";
		break;
	case E_CAL_CLIENT_SOURCE_TYPE_TODO:
		type = "tasks";
		break;
	case E_CAL_CLIENT_SOURCE_TYPE_JOURNAL:
		type = "memos";
		break;
	default:
		type = "unknown-type";
		break;
	}

	name = e_source_peek_name (source);
	if (!name)
		name = "Unknown name";

	uri = e_source_peek_absolute_uri (source);
	if (!uri) {
		abs_uri = e_source_build_absolute_uri (source);
		uri = abs_uri;
	}
	if (!uri)
		uri = e_source_peek_relative_uri (source);
	if (!uri)
		uri = "Unknown uri";

	g_print ("\n   Checking %s source '%s' (%s)\n", type, name, uri);

	g_free (abs_uri);
}

static void
identify_cal_client (ECalClient *cal_client)
{
	g_return_if_fail (cal_client != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (cal_client));

	identify_source (e_client_get_source (E_CLIENT (cal_client)), e_cal_client_get_source_type (cal_client));
}

static void client_opened_async (GObject *source_object, GAsyncResult *result, gpointer async_data);

static void
continue_next_source (gpointer async_data)
{
	ESource *source = NULL;
	ECalClient *cal_client;
	GError *error = NULL;

	g_return_if_fail (async_data != NULL);

	while (async_data && foreach_configured_source_async_next (&async_data, &source)) {
		ECalClientSourceType source_type = foreach_configured_source_async_get_source_type (async_data);

		cal_client = e_cal_client_new (source, source_type, &error);
		if (!cal_client) {
			identify_source (source, source_type);
			report_error ("cal client new", &error);
			continue;
		}

		e_client_open (E_CLIENT (cal_client), TRUE, NULL, client_opened_async, async_data);
		break;
	}

	if (!async_data) {
		running_async--;
		if (!running_async)
			stop_main_loop (0);
	}
}

static void
client_got_values_async (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ExtraValues *evals = user_data;
	GSList *values = NULL;
	GError *error = NULL;
	ECalClient *cal_client;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (source_object));
	g_return_if_fail (evals != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_capabilities_finish (cal_client, result, &values, &error)) {
		identify_cal_client (cal_client);
		report_error ("get capabilities finish", &error);
	} else {
		/* to cache them, as it can be fetched with idle as well */
		e_client_get_capabilities (E_CLIENT (source_object));

		evals->cache_dir = e_cal_client_get_local_attachment_store (cal_client);

		identify_cal_client (cal_client);
		print_values (values, evals, E_CLIENT (source_object));

		e_client_util_free_string_slist (values);
	}

	g_object_unref (source_object);

	continue_next_source (evals->async_data);
	extra_values_free (evals);
}

static void
client_got_alarm_email_address_async (GObject *source_object, GAsyncResult *result, gpointer evals_data)
{
	ExtraValues *evals = evals_data;
	ECalClient *cal_client;
	GError *error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (source_object));
	g_return_if_fail (evals != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_alarm_email_address_finish (cal_client, result, &evals->alarm_email_address, &error)) {
		identify_cal_client (cal_client);
		report_error ("get alarm email address finish", &error);
		g_object_unref (source_object);
		continue_next_source (evals->async_data);
		extra_values_free (evals);
		return;
	}

	e_cal_client_get_capabilities (cal_client, NULL, client_got_values_async, evals);
}

static void
client_got_cal_address_async (GObject *source_object, GAsyncResult *result, gpointer evals_data)
{
	ExtraValues *evals = evals_data;
	ECalClient *cal_client;
	GError *error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (source_object));
	g_return_if_fail (evals != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_cal_email_address_finish (cal_client, result, &evals->cal_address, &error)) {
		identify_cal_client (cal_client);
		report_error ("get cal address finish", &error);
		g_object_unref (source_object);
		continue_next_source (evals->async_data);
		extra_values_free (evals);
		return;
	}

	e_cal_client_get_alarm_email_address (cal_client, NULL, client_got_alarm_email_address_async, evals);
}

static void
client_got_default_object_async (GObject *source_object, GAsyncResult *result, gpointer evals_data)
{
	ExtraValues *evals = evals_data;
	ECalClient *cal_client;
	GError *error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (source_object));
	g_return_if_fail (evals != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_default_object_finish (cal_client, result, &evals->default_object, &error)) {
		identify_cal_client (cal_client);
		report_error ("get default object finish", &error);
	}

	e_cal_client_get_cal_email_address (cal_client, NULL, client_got_cal_address_async, evals);
}

static void
client_opened_async (GObject *source_object, GAsyncResult *result, gpointer async_data)
{
	ExtraValues *evals;
	GError *error = NULL;
	ECalClient *cal_client;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (source_object));
	g_return_if_fail (async_data != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_client_open_finish (E_CLIENT (source_object), result, &error)) {
		identify_cal_client (cal_client);
		report_error ("client open finish", &error);
		g_object_unref (source_object);
		continue_next_source (async_data);
		return;
	}

	evals = g_new0 (ExtraValues, 1);
	evals->async_data = async_data;
	
	e_cal_client_get_default_object (cal_client, NULL, client_got_default_object_async, evals);
}

static void
check_source_sync (ESource *source, ECalClientSourceType source_type)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *values = NULL;
	ExtraValues evals = { 0 };

	g_return_if_fail (source != NULL);

	identify_source (source, source_type);

	cal_client = e_cal_client_new (source, source_type, &error);
	if (!cal_client) {
		report_error ("cal client new", &error);
		return;
	}

	if (!e_client_open_sync (E_CLIENT (cal_client), TRUE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return;
	}

	if (!e_cal_client_get_capabilities_sync (cal_client, &values, NULL, &error)) {
		report_error ("get capabilities sync", &error);
		g_object_unref (cal_client);
		return;
	}

	if (!e_cal_client_get_cal_email_address_sync (cal_client, &evals.cal_address, NULL, &error)) {
		report_error ("get cal address sync", &error);
	}

	if (!e_cal_client_get_alarm_email_address_sync (cal_client, &evals.alarm_email_address, NULL, &error)) {
		report_error ("get alarm email address sync", &error);
	}

	if (!e_cal_client_get_default_object_sync (cal_client, &evals.default_object, NULL, &error)) {
		report_error ("get default object sync", &error);
	}

	evals.cache_dir = e_cal_client_get_local_attachment_store (cal_client);

	print_values (values, &evals, E_CLIENT (cal_client));

	g_free (evals.cal_address);
	g_free (evals.alarm_email_address);
	icalcomponent_free (evals.default_object);
	e_client_util_free_string_slist (values);
	g_object_unref (cal_client);
}

static gboolean
foreach_async (ECalClientSourceType source_type)
{
	gpointer async_data;
	ESource *source = NULL;
	ECalClient *cal_client;
	GError *error = NULL;

	async_data = foreach_configured_source_async_start (source_type, &source);
	if (!async_data) {
		stop_main_loop (1);
		return FALSE;
	}

	running_async++;

	while (cal_client = e_cal_client_new (source, source_type, &error), !cal_client) {
		identify_source (source, source_type);
		report_error ("cal client new", &error);

		if (!foreach_configured_source_async_next (&async_data, &source)) {
			running_async--;
			if (!running_async)
				stop_main_loop (0);
			return FALSE;
		}

		identify_source (source, source_type);
	}

	e_client_open (E_CLIENT (cal_client), TRUE, NULL, client_opened_async, async_data);

	return TRUE;
}

static gboolean
in_main_thread_idle_cb (gpointer unused)
{
	g_print ("* run in main thread with mainloop running\n");
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_EVENT, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_TODO, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_JOURNAL, check_source_sync);
	g_print ("---------------------------------------------------------\n\n");

	g_print ("* run in main thread async\n");

	if (!foreach_async (E_CAL_CLIENT_SOURCE_TYPE_EVENT))
		return FALSE;

	if (!foreach_async (E_CAL_CLIENT_SOURCE_TYPE_TODO))
		return FALSE;

	if (!foreach_async (E_CAL_CLIENT_SOURCE_TYPE_JOURNAL))
		return FALSE;

	return FALSE;
}

static gpointer
worker_thread (gpointer unused)
{
	g_print ("* run in dedicated thread with mainloop running\n");
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_EVENT, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_TODO, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_JOURNAL, check_source_sync);
	g_print ("---------------------------------------------------------\n\n");

	g_idle_add (in_main_thread_idle_cb, NULL);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	main_initialize ();

	g_print ("* run in main thread without mainloop\n");
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_EVENT, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_TODO, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_JOURNAL, check_source_sync);
	g_print ("---------------------------------------------------------\n\n");

	start_in_thread_with_main_loop (worker_thread, NULL);

	return get_main_loop_stop_result ();
}
