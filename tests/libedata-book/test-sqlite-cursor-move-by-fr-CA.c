/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

gint
main (gint argc,
      gchar **argv)
{
	StepData *data;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	data = step_test_new ("/EbSdbCursor/fr_CA/Move/Forward", "fr_CA.UTF-8");
	step_test_add_assertion (data, 5, 11, 1, 2, 5, 6);
	step_test_add_assertion (data, 6, 4, 3, 7, 8, 15, 17);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/fr_CA/Move/ForwardOnNameless", "fr_CA.UTF-8");
	step_test_add_assertion (data, 1, 11);
	step_test_add_assertion (data, 3, 1, 2, 5);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/fr_CA/Move/Backwards", "fr_CA.UTF-8");
	step_test_add_assertion (data, -5, 20, 19, 9, 12, 13);
	step_test_add_assertion (data, -8, 14, 10, 18, 16, 17, 15, 8, 7);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/fr_CA/Filtered/Move/Forward", "fr_CA.UTF-8");
	step_test_add_assertion (data, 5, 11, 1, 2, 5, 3);
	step_test_add_assertion (data, 8, 8, 17, 16, 18, 10, 14, 12, 9);
	step_test_add (data, TRUE);

	data = step_test_new ("/EbSdbCursor/fr_CA/Filtered/Move/Backwards", "fr_CA.UTF-8");
	step_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
	step_test_add_assertion (data, -8, 16, 17, 8, 3, 5, 2, 1, 11);
	step_test_add (data, TRUE);

	return e_test_server_utils_run ();
}
