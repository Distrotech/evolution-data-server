/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"

static void
_remove_object_cb (ECal         *cal,
		   const GError *error,
		   gpointer      userdata)
{
	GMainLoop *loop = (GMainLoop *)userdata;

	if (error)
	{
		g_warning ("failed to remove icalcomponent object; %s\n", error->message);
		exit(1);
	}

	test_print ("successfully removed the icalcomponent object\n");
	g_main_loop_quit (loop);
}

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	gchar *uri = NULL;
	icalcomponent *component;
	icalcomponent *component_final;
	gchar *uid;
	GMainLoop *loop;

	g_type_init ();

	loop = g_main_loop_new (NULL, FALSE);

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	component = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	uid = ecal_test_utils_cal_create_object (cal, component);

	component_final = ecal_test_utils_cal_get_object (cal, uid);
	ecal_test_utils_cal_assert_objects_equal_shallow (component, component_final);
	ecal_test_utils_cal_remove_object_async (cal, uid, _remove_object_cb, loop);

	g_main_loop_run (loop);
	
	ecal_test_utils_cal_remove (cal);

	g_free (uid);
	icalcomponent_free (component);
	icalcomponent_free (component_final);

	return 0;
}
