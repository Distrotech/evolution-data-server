/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Live view client object
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Ross Burton <ross@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>

#include <string.h>
#include "e-cal-client.h"
#include "e-cal-client-view.h"

#include "e-gdbus-cal-view.h"

#define E_CAL_CLIENT_VIEW_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_CLIENT_VIEW, ECalClientViewPrivate))

typedef struct _SignalClosure SignalClosure;

struct _ECalClientViewPrivate {
	GWeakRef client;
	GDBusProxy *dbus_proxy;
	GDBusConnection *connection;
	gchar *object_path;
	gboolean running;

	GMainContext *main_context;
	GMutex main_context_lock;

	gulong objects_added_handler_id;
	gulong objects_modified_handler_id;
	gulong objects_removed_handler_id;
	gulong progress_handler_id;
	gulong complete_handler_id;
};

struct _SignalClosure {
	GWeakRef client_view;
	GSList *component_list;
	GSList *component_id_list;
	gchar *message;
	guint percent;
	GError *error;
};

enum {
	PROP_0,
	PROP_CLIENT,
	PROP_CONNECTION,
	PROP_OBJECT_PATH
};

enum {
	OBJECTS_ADDED,
	OBJECTS_MODIFIED,
	OBJECTS_REMOVED,
	PROGRESS,
	COMPLETE,
	LAST_SIGNAL
};

/* Forward Declarations */
static void	e_cal_client_view_initable_init	(GInitableIface *interface);

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE (
	ECalClientView,
	e_cal_client_view,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_cal_client_view_initable_init))

static void
signal_closure_free (SignalClosure *signal_closure)
{
	g_weak_ref_set (&signal_closure->client_view, NULL);

	g_slist_free_full (
		signal_closure->component_list,
		(GDestroyNotify) icalcomponent_free);

	g_slist_free_full (
		signal_closure->component_id_list,
		(GDestroyNotify) e_cal_component_free_id);

	g_free (signal_closure->message);

	if (signal_closure->error != NULL)
		g_error_free (signal_closure->error);

	g_slice_free (SignalClosure, signal_closure);
}

static GMainContext *
cal_client_view_ref_main_context (ECalClientView *client_view)
{
	GMainContext *main_context;

	/* Intentionally not checking for NULL so we get a console
	 * warning if we try to reference a NULL main context, but
	 * that should never happen. */

	g_mutex_lock (&client_view->priv->main_context_lock);

	main_context = g_main_context_ref (client_view->priv->main_context);

	g_mutex_unlock (&client_view->priv->main_context_lock);

	return main_context;
}

static void
cal_client_view_set_main_context (ECalClientView *client_view,
                                  GMainContext *main_context)
{
	g_mutex_lock (&client_view->priv->main_context_lock);

	if (client_view->priv->main_context != NULL)
		g_main_context_unref (client_view->priv->main_context);

	client_view->priv->main_context = g_main_context_ref (main_context);

	g_mutex_unlock (&client_view->priv->main_context_lock);
}

static GSList *
build_object_list (const gchar * const *seq)
{
	GSList *list;
	gint i;

	list = NULL;
	for (i = 0; seq[i]; i++) {
		icalcomponent *comp;

		comp = icalcomponent_new_from_string ((gchar *) seq[i]);
		if (!comp)
			continue;

		list = g_slist_prepend (list, comp);
	}

	return g_slist_reverse (list);
}

static GSList *
build_id_list (const gchar * const *seq)
{
	GSList *list;
	const gchar *eol;
	gint i;

	list = NULL;
	for (i = 0; seq[i]; i++) {
		ECalComponentId *id;
		id = g_new (ECalComponentId, 1);

		/* match encoding as in notify_remove()
		 * in e-data-cal-view.c: <uid>[\n<rid>] */
		eol = strchr (seq[i], '\n');
		if (eol) {
			id->uid = g_strndup (seq[i], eol - seq[i]);
			id->rid = g_strdup (eol + 1);
		} else {
			id->uid = g_strdup (seq[i]);
			id->rid = NULL;
		}

		list = g_slist_prepend (list, id);
	}

	return g_slist_reverse (list);
}

static gboolean
cal_client_view_emit_objects_added_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	ECalClientView *client_view;

	client_view = g_weak_ref_get (&signal_closure->client_view);

	if (client_view != NULL) {
		g_signal_emit (
			client_view,
			signals[OBJECTS_ADDED], 0,
			signal_closure->component_list);
		g_object_unref (client_view);
	}

	return FALSE;
}

static gboolean
cal_client_view_emit_objects_modified_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	ECalClientView *client_view;

	client_view = g_weak_ref_get (&signal_closure->client_view);

	if (client_view != NULL) {
		g_signal_emit (
			client_view,
			signals[OBJECTS_MODIFIED], 0,
			signal_closure->component_list);
		g_object_unref (client_view);
	}

	return FALSE;
}

static gboolean
cal_client_view_emit_objects_removed_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	ECalClientView *client_view;

	client_view = g_weak_ref_get (&signal_closure->client_view);

	if (client_view != NULL) {
		g_signal_emit (
			client_view,
			signals[OBJECTS_REMOVED], 0,
			signal_closure->component_id_list);
		g_object_unref (client_view);
	}

	return FALSE;
}

static gboolean
cal_client_view_emit_progress_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	ECalClientView *client_view;

	client_view = g_weak_ref_get (&signal_closure->client_view);

	if (client_view != NULL) {
		g_signal_emit (
			client_view,
			signals[PROGRESS], 0,
			signal_closure->percent,
			signal_closure->message);
		g_object_unref (client_view);
	}

	return FALSE;
}

static gboolean
cal_client_view_emit_complete_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	ECalClientView *client_view;

	client_view = g_weak_ref_get (&signal_closure->client_view);

	if (client_view != NULL) {
		g_signal_emit (
			client_view,
			signals[COMPLETE], 0,
			signal_closure->error);
		g_object_unref (client_view);
	}

	return FALSE;
}

static void
cal_client_view_objects_added_cb (EGdbusCalView *dbus_proxy,
                                  const gchar * const *objects,
                                  GWeakRef *client_view_weak_ref)
{
	ECalClientView *client_view;

	client_view = g_weak_ref_get (client_view_weak_ref);

	if (client_view != NULL) {
		GSource *idle_source;
		GMainContext *main_context;
		SignalClosure *signal_closure;

		if (!client_view->priv->running) {
			g_object_unref (client_view);
			return;
		}

		signal_closure = g_slice_new0 (SignalClosure);
		g_weak_ref_set (&signal_closure->client_view, client_view);
		signal_closure->component_list = build_object_list (objects);

		main_context = cal_client_view_ref_main_context (client_view);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			cal_client_view_emit_objects_added_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, main_context);
		g_source_unref (idle_source);

		g_main_context_unref (main_context);

		g_object_unref (client_view);
	}
}

static void
cal_client_view_objects_modified_cb (EGdbusCalView *dbus_proxy,
                                     const gchar * const *objects,
                                     GWeakRef *client_view_weak_ref)
{
	ECalClientView *client_view;

	client_view = g_weak_ref_get (client_view_weak_ref);

	if (client_view != NULL) {
		GSource *idle_source;
		GMainContext *main_context;
		SignalClosure *signal_closure;

		if (!client_view->priv->running) {
			g_object_unref (client_view);
			return;
		}

		signal_closure = g_slice_new0 (SignalClosure);
		g_weak_ref_set (&signal_closure->client_view, client_view);
		signal_closure->component_list = build_object_list (objects);

		main_context = cal_client_view_ref_main_context (client_view);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			cal_client_view_emit_objects_modified_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, main_context);
		g_source_unref (idle_source);

		g_main_context_unref (main_context);

		g_object_unref (client_view);
	}
}

static void
cal_client_view_objects_removed_cb (EGdbusCalView *dbus_proxy,
                                    const gchar * const *uids,
                                    GWeakRef *client_view_weak_ref)
{
	ECalClientView *client_view;

	client_view = g_weak_ref_get (client_view_weak_ref);

	if (client_view != NULL) {
		GSource *idle_source;
		GMainContext *main_context;
		SignalClosure *signal_closure;

		if (!client_view->priv->running) {
			g_object_unref (client_view);
			return;
		}

		signal_closure = g_slice_new0 (SignalClosure);
		g_weak_ref_set (&signal_closure->client_view, client_view);
		signal_closure->component_id_list = build_id_list (uids);

		main_context = cal_client_view_ref_main_context (client_view);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			cal_client_view_emit_objects_removed_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, main_context);
		g_source_unref (idle_source);

		g_main_context_unref (main_context);

		g_object_unref (client_view);
	}
}

static void
cal_client_view_progress_cb (EGdbusCalView *dbus_proxy,
                             guint percent,
                             const gchar *message,
                             GWeakRef *client_view_weak_ref)
{
	ECalClientView *client_view;

	client_view = g_weak_ref_get (client_view_weak_ref);

	if (client_view != NULL) {
		GSource *idle_source;
		GMainContext *main_context;
		SignalClosure *signal_closure;

		if (!client_view->priv->running) {
			g_object_unref (client_view);
			return;
		}

		signal_closure = g_slice_new0 (SignalClosure);
		g_weak_ref_set (&signal_closure->client_view, client_view);
		signal_closure->message = g_strdup (message);
		signal_closure->percent = percent;

		main_context = cal_client_view_ref_main_context (client_view);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			cal_client_view_emit_progress_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, main_context);
		g_source_unref (idle_source);

		g_main_context_unref (main_context);

		g_object_unref (client_view);
	}
}

static void
cal_client_view_complete_cb (EGdbusCalView *dbus_proxy,
                             const gchar * const *arg_error,
                             GWeakRef *client_view_weak_ref)
{
	ECalClientView *client_view;

	client_view = g_weak_ref_get (client_view_weak_ref);

	if (client_view != NULL) {
		GSource *idle_source;
		GMainContext *main_context;
		SignalClosure *signal_closure;

		if (!client_view->priv->running) {
			g_object_unref (client_view);
			return;
		}

		signal_closure = g_slice_new0 (SignalClosure);
		g_weak_ref_set (&signal_closure->client_view, client_view);
		e_gdbus_templates_decode_error (
			arg_error, &signal_closure->error);

		main_context = cal_client_view_ref_main_context (client_view);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			cal_client_view_emit_complete_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, main_context);
		g_source_unref (idle_source);

		g_main_context_unref (main_context);

		g_object_unref (client_view);
	}
}

static void
cal_client_view_dispose_cb (GObject *source_object,
                            GAsyncResult *result,
                            gpointer user_data)
{
	GError *local_error = NULL;

	e_gdbus_cal_view_call_dispose_finish (
		G_DBUS_PROXY (source_object), result, &local_error);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_warning ("%s: %s", G_STRFUNC, local_error->message);
		g_error_free (local_error);
	}
}

static void
cal_client_view_set_client (ECalClientView *client_view,
                            ECalClient *client)
{
	g_return_if_fail (E_IS_CAL_CLIENT (client));

	g_weak_ref_set (&client_view->priv->client, client);
}

static void
cal_client_view_set_connection (ECalClientView *client_view,
                                GDBusConnection *connection)
{
	g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
	g_return_if_fail (client_view->priv->connection == NULL);

	client_view->priv->connection = g_object_ref (connection);
}

static void
cal_client_view_set_object_path (ECalClientView *client_view,
                                 const gchar *object_path)
{
	g_return_if_fail (object_path != NULL);
	g_return_if_fail (client_view->priv->object_path == NULL);

	client_view->priv->object_path = g_strdup (object_path);
}

static void
cal_client_view_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CLIENT:
			cal_client_view_set_client (
				E_CAL_CLIENT_VIEW (object),
				g_value_get_object (value));
			return;

		case PROP_CONNECTION:
			cal_client_view_set_connection (
				E_CAL_CLIENT_VIEW (object),
				g_value_get_object (value));
			return;

		case PROP_OBJECT_PATH:
			cal_client_view_set_object_path (
				E_CAL_CLIENT_VIEW (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_client_view_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CLIENT:
			g_value_take_object (
				value,
				e_cal_client_view_ref_client (
				E_CAL_CLIENT_VIEW (object)));
			return;

		case PROP_CONNECTION:
			g_value_set_object (
				value,
				e_cal_client_view_get_connection (
				E_CAL_CLIENT_VIEW (object)));
			return;

		case PROP_OBJECT_PATH:
			g_value_set_string (
				value,
				e_cal_client_view_get_object_path (
				E_CAL_CLIENT_VIEW (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_client_view_dispose (GObject *object)
{
	ECalClientViewPrivate *priv;

	priv = E_CAL_CLIENT_VIEW_GET_PRIVATE (object);

	g_weak_ref_set (&priv->client, NULL);

	if (priv->connection != NULL) {
		g_object_unref (priv->connection);
		priv->connection = NULL;
	}

	if (priv->main_context != NULL) {
		g_main_context_unref (priv->main_context);
		priv->main_context = NULL;
	}

	if (priv->dbus_proxy != NULL) {
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->objects_added_handler_id);
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->objects_modified_handler_id);
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->objects_removed_handler_id);
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->progress_handler_id);
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->complete_handler_id);

		/* Call D-Bus dispose() asynchronously
		 * so we don't block this dispose(). */
		e_gdbus_cal_view_call_dispose (
			priv->dbus_proxy, NULL,
			cal_client_view_dispose_cb, NULL);
		g_object_unref (priv->dbus_proxy);
		priv->dbus_proxy = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_cal_client_view_parent_class)->dispose (object);
}

static void
cal_client_view_finalize (GObject *object)
{
	ECalClientViewPrivate *priv;

	priv = E_CAL_CLIENT_VIEW_GET_PRIVATE (object);

	g_free (priv->object_path);

	g_mutex_clear (&priv->main_context_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_client_view_parent_class)->finalize (object);
}

static gboolean
cal_client_view_initable_init (GInitable *initable,
                               GCancellable *cancellable,
                               GError **error)
{
	ECalClientViewPrivate *priv;
	EGdbusCalView *gdbus_calview;
	gulong handler_id;

	priv = E_CAL_CLIENT_VIEW_GET_PRIVATE (initable);

	gdbus_calview = e_gdbus_cal_view_proxy_new_sync (
		priv->connection,
		G_DBUS_PROXY_FLAGS_NONE,
		CALENDAR_DBUS_SERVICE_NAME,
		priv->object_path,
		cancellable, error);

	if (gdbus_calview == NULL)
		return FALSE;

	priv->dbus_proxy = G_DBUS_PROXY (gdbus_calview);

	handler_id = g_signal_connect_data (
		priv->dbus_proxy, "objects-added",
		G_CALLBACK (cal_client_view_objects_added_cb),
		e_weak_ref_new (initable),
		(GClosureNotify) e_weak_ref_free, 0);
	priv->objects_added_handler_id = handler_id;

	handler_id = g_signal_connect_data (
		priv->dbus_proxy, "objects-modified",
		G_CALLBACK (cal_client_view_objects_modified_cb),
		e_weak_ref_new (initable),
		(GClosureNotify) e_weak_ref_free, 0);
	priv->objects_modified_handler_id = handler_id;

	handler_id = g_signal_connect_data (
		priv->dbus_proxy, "objects-removed",
		G_CALLBACK (cal_client_view_objects_removed_cb),
		e_weak_ref_new (initable),
		(GClosureNotify) e_weak_ref_free, 0);
	priv->objects_removed_handler_id = handler_id;

	handler_id = g_signal_connect_data (
		priv->dbus_proxy, "progress",
		G_CALLBACK (cal_client_view_progress_cb),
		e_weak_ref_new (initable),
		(GClosureNotify) e_weak_ref_free, 0);
	priv->progress_handler_id = handler_id;

	handler_id = g_signal_connect_data (
		priv->dbus_proxy, "complete",
		G_CALLBACK (cal_client_view_complete_cb),
		e_weak_ref_new (initable),
		(GClosureNotify) e_weak_ref_free, 0);
	priv->complete_handler_id = handler_id;

	return TRUE;
}

static void
e_cal_client_view_class_init (ECalClientViewClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ECalClientViewPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = cal_client_view_set_property;
	object_class->get_property = cal_client_view_get_property;
	object_class->dispose = cal_client_view_dispose;
	object_class->finalize = cal_client_view_finalize;

	/**
	 * ECalClientView:client:
	 *
	 * The ECalClient for the view
	 */
	g_object_class_install_property (
		object_class,
		PROP_CLIENT,
		g_param_spec_object (
			"client",
			"Client",
			"The ECalClient for the view",
			E_TYPE_CAL_CLIENT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ECalClientView:connection:
	 *
	 * The GDBusConnection used to create the D-Bus proxy
	 */
	g_object_class_install_property (
		object_class,
		PROP_CONNECTION,
		g_param_spec_object (
			"connection",
			"Connection",
			"The GDBusConnection used "
			"to create the D-Bus proxy",
			G_TYPE_DBUS_CONNECTION,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ECalClientView:object-path:
	 *
	 * The object path used to create the D-Bus proxy
	 */
	g_object_class_install_property (
		object_class,
		PROP_OBJECT_PATH,
		g_param_spec_string (
			"object-path",
			"Object Path",
			"The object path used "
			"to create the D-Bus proxy",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ECalClientView::objects-added:
	 * @client_view: the #ECalClientView which emitted the signal
	 * @objects: (type GSList) (transfer none) (element-type long):
	 */
	signals[OBJECTS_ADDED] = g_signal_new (
		"objects-added",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClientViewClass, objects_added),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	/**
	 * ECalClientView::objects-modified:
	 * @client_view: the #ECalClientView which emitted the signal
	 * @objects: (type GSList) (transfer none) (element-type long):
	 */
	signals[OBJECTS_MODIFIED] = g_signal_new (
		"objects-modified",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClientViewClass, objects_modified),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	/**
	 * ECalClientView::objects-removed:
	 * @client_view: the #ECalClientView which emitted the signal
	 * @objects: (type GSList) (transfer none) (element-type ECalComponentId):
	 */
	signals[OBJECTS_REMOVED] = g_signal_new (
		"objects-removed",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClientViewClass, objects_removed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[PROGRESS] = g_signal_new (
		"progress",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClientViewClass, progress),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		G_TYPE_UINT,
		G_TYPE_STRING);

	signals[COMPLETE] = g_signal_new (
		"complete",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClientViewClass, complete),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_ERROR);
}

static void
e_cal_client_view_initable_init (GInitableIface *interface)
{
	interface->init = cal_client_view_initable_init;
}

static void
e_cal_client_view_init (ECalClientView *client_view)
{
	client_view->priv = E_CAL_CLIENT_VIEW_GET_PRIVATE (client_view);

	g_mutex_init (&client_view->priv->main_context_lock);
}

/**
 * e_cal_client_view_ref_client:
 * @client_view: an #ECalClientView
 *
 * Returns the #ECalClientView:client associated with @client_view.
 *
 * The returned #ECalClient is referenced for thread-safety.  Unreference
 * the #ECalClient with g_object_unref() when finished with it.
 *
 * Returns: an #ECalClient
 *
 * Since: 3.10
 **/
ECalClient *
e_cal_client_view_ref_client (ECalClientView *client_view)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT_VIEW (client_view), NULL);

	return g_weak_ref_get (&client_view->priv->client);
}

/**
 * e_cal_client_view_get_client:
 * @client_view: an #ECalClientView
 *
 * Returns the #ECalClientView:client associated with @client_view.
 *
 * Returns: (transfer none): an #ECalClient
 *
 * Deprecated: 3.10: Use e_cal_client_view_ref_client() instead.
 *
 * Since: 3.2
 **/
ECalClient *
e_cal_client_view_get_client (ECalClientView *client_view)
{
	ECalClient *client;

	g_return_val_if_fail (E_IS_CAL_CLIENT_VIEW (client_view), NULL);

	client = e_cal_client_view_ref_client (client_view);

	/* XXX Drop the ECalClient reference for backward-compatibility.
	 *     This is risky.  Without a reference, the ECalClient could
	 *     be finalized while the caller is still using it. */
	if (client != NULL)
		g_object_unref (client);

	return client;
}

/**
 * e_cal_client_view_get_connection:
 * @client_view: an #ECalClientView
 *
 * Returns the #GDBusConnection used to create the D-Bus proxy.
 *
 * Returns: (transfer none): the #GDBusConnection
 *
 * Since: 3.8
 **/
GDBusConnection *
e_cal_client_view_get_connection (ECalClientView *client_view)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT_VIEW (client_view), NULL);

	return client_view->priv->connection;
}

/**
 * e_cal_client_view_get_object_path:
 * @client_view: an #ECalClientView
 *
 * Returns the object path used to create the D-Bus proxy.
 *
 * Returns: the object path
 *
 * Since: 3.8
 **/
const gchar *
e_cal_client_view_get_object_path (ECalClientView *client_view)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT_VIEW (client_view), NULL);

	return client_view->priv->object_path;
}

/**
 * e_cal_client_view_is_running:
 * @client_view: an #ECalClientView
 *
 * Retunrs: Whether view is running. Not running views are ignoring
 * all events sent from the server.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_view_is_running (ECalClientView *client_view)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT_VIEW (client_view), FALSE);

	return client_view->priv->running;
}

/**
 * e_cal_client_view_start:
 * @client_view: an #ECalClientView
 * @error: return location for a #GError, or %NULL
 *
 * Tells @client_view to start processing events.
 *
 * Since: 3.2
 **/
void
e_cal_client_view_start (ECalClientView *client_view,
                         GError **error)
{
	GMainContext *main_context;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (client_view));

	/* Emit signals from the current thread-default main context. */
	main_context = g_main_context_ref_thread_default ();
	cal_client_view_set_main_context (client_view, main_context);
	g_main_context_unref (main_context);

	client_view->priv->running = TRUE;

	e_gdbus_cal_view_call_start_sync (
		client_view->priv->dbus_proxy, NULL, &local_error);

	if (local_error != NULL) {
		client_view->priv->running = FALSE;
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
	}
}

/**
 * e_cal_client_view_stop:
 * @client_view: an #ECalClientView
 * @error: return location for a #GError, or %NULL
 *
 * Tells @client_view to stop processing events.
 *
 * Since: 3.2
 */
void
e_cal_client_view_stop (ECalClientView *client_view,
                        GError **error)
{
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (client_view));

	client_view->priv->running = FALSE;

	e_gdbus_cal_view_call_stop_sync (
		client_view->priv->dbus_proxy, NULL, &local_error);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
	}
}

/**
 * e_cal_client_view_set_fields_of_interest:
 * @client_view: an #ECalClientView
 * @fields_of_interest: (element-type utf8) (allow-none): List of field names
 *                      in which the client is interested, or %NULL to reset
 *                      the fields of interest
 * @error: return location for a #GError, or %NULL
 *
 * Client can instruct server to which fields it is interested in only, thus
 * the server can return less data over the wire. The server can still return
 * complete objects, this is just a hint to it that the listed fields will
 * be used only. The UID/RID fields are returned always. Initial views has no
 * fields of interest and using %NULL for @fields_of_interest will unset any
 * previous changes.
 *
 * Some backends can use summary information of its cache to create artifical
 * objects, which will omit stored object parsing. If this cannot be done then
 * it will simply return object as is stored in the cache.
 **/
void
e_cal_client_view_set_fields_of_interest (ECalClientView *client_view,
                                          const GSList *fields_of_interest,
                                          GError **error)
{
	gchar **strv;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (client_view));

	strv = e_client_util_slist_to_strv (fields_of_interest);
	e_gdbus_cal_view_call_set_fields_of_interest_sync (
		client_view->priv->dbus_proxy,
		(const gchar * const *) strv,
		NULL, &local_error);
	g_strfreev (strv);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
	}
}

/**
 * e_cal_client_view_set_flags:
 * @client_view: an #ECalClientView
 * @flags: the #ECalClientViewFlags for @client_view
 * @error: return location for a #GError, or %NULL
 *
 * Sets the @flags which control the behaviour of @client_view.
 *
 * Since: 3.6
 */
void
e_cal_client_view_set_flags (ECalClientView *client_view,
                             ECalClientViewFlags flags,
                             GError **error)
{
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (client_view));

	e_gdbus_cal_view_call_set_flags_sync (
		client_view->priv->dbus_proxy, flags, NULL, &local_error);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
	}
}
