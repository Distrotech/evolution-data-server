/*
 * e-gdbus-book-view.c
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
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
 *
 */

#include <stdio.h>
#include <gio/gio.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-gdbus-marshallers.h>

#include "e-gdbus-book-view.h"

#define GDBUS_BOOK_VIEW_INTERFACE_NAME "org.gnome.evolution.dataserver.AddressBookView"

typedef EGdbusBookViewIface EGdbusBookViewInterface;
G_DEFINE_INTERFACE (EGdbusBookView, e_gdbus_book_view, G_TYPE_OBJECT);

enum
{
	_0_SIGNAL,
	__OBJECTS_ADDED_SIGNAL,
	__OBJECTS_MODIFIED_SIGNAL,
	__OBJECTS_REMOVED_SIGNAL,
	__PROGRESS_SIGNAL,
	__COMPLETE_SIGNAL,
	__START_METHOD,
	__STOP_METHOD,
	__DISPOSE_METHOD,
	__SET_FIELDS_OF_INTEREST_METHOD,
	__LAST_SIGNAL
};

static guint signals[__LAST_SIGNAL] = {0};

/* ------------------------------------------------------------------------- */

/* Various lookup tables */

static GHashTable *_method_name_to_id = NULL;
static GHashTable *_method_name_to_type = NULL;
static GHashTable *_signal_name_to_id = NULL;
static GHashTable *_signal_name_to_type = NULL;

static guint
lookup_method_id_from_method_name (const gchar *method_name)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (_method_name_to_id, method_name));
}

static guint
lookup_method_type_from_method_name (const gchar *method_name)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (_method_name_to_type, method_name));
}

static guint
lookup_signal_id_from_signal_name (const gchar *signal_name)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (_signal_name_to_id, signal_name));
}

static guint
lookup_signal_type_from_signal_name (const gchar *signal_name)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (_signal_name_to_type, signal_name));
}

/* ------------------------------------------------------------------------- */

E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV	 (GDBUS_BOOK_VIEW_INTERFACE_NAME, objects_added)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV	 (GDBUS_BOOK_VIEW_INTERFACE_NAME, objects_modified)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV	 (GDBUS_BOOK_VIEW_INTERFACE_NAME, objects_removed)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_UINT_STRING (GDBUS_BOOK_VIEW_INTERFACE_NAME, progress)
E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV	 (GDBUS_BOOK_VIEW_INTERFACE_NAME, complete)

static void
e_gdbus_book_view_default_init (EGdbusBookViewIface *iface)
{
	/* Build lookup structures */
	_method_name_to_id = g_hash_table_new (g_str_hash, g_str_equal);
	_method_name_to_type = g_hash_table_new (g_str_hash, g_str_equal);
	_signal_name_to_id = g_hash_table_new (g_str_hash, g_str_equal);
	_signal_name_to_type = g_hash_table_new (g_str_hash, g_str_equal);

	
	/* GObject signals definitions for D-Bus signals: */
	E_INIT_GDBUS_SIGNAL_STRV	(EGdbusBookViewIface, "ObjectsAdded",	objects_added, __OBJECTS_ADDED_SIGNAL)
	E_INIT_GDBUS_SIGNAL_STRV	(EGdbusBookViewIface, "ObjectsModified",objects_modified, __OBJECTS_MODIFIED_SIGNAL)
	E_INIT_GDBUS_SIGNAL_STRV	(EGdbusBookViewIface, "ObjectsRemoved",	objects_removed, __OBJECTS_REMOVED_SIGNAL)
	E_INIT_GDBUS_SIGNAL_UINT_STRING	(EGdbusBookViewIface, "Progress",	progress, __PROGRESS_SIGNAL)
	E_INIT_GDBUS_SIGNAL_STRV	(EGdbusBookViewIface, "Complete",	complete, __COMPLETE_SIGNAL)

	/* GObject signals definitions for D-Bus methods: */
	E_INIT_GDBUS_METHOD_VOID	(EGdbusBookViewIface, "start",			start, __START_METHOD)
	E_INIT_GDBUS_METHOD_VOID	(EGdbusBookViewIface, "stop",			stop, __STOP_METHOD)
	E_INIT_GDBUS_METHOD_VOID	(EGdbusBookViewIface, "dispose",		dispose, __DISPOSE_METHOD)
	E_INIT_GDBUS_METHOD_STRV	(EGdbusBookViewIface, "setFieldsOfInterest",	set_fields_of_interest, __SET_FIELDS_OF_INTEREST_METHOD)
}

void
e_gdbus_book_view_call_start (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_method_call_void ("start", proxy, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_view_call_start_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_book_view_call_start_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_method_call_sync_void__void ("start", proxy, cancellable, error);
}

void
e_gdbus_book_view_call_stop (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_method_call_void ("stop", proxy, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_view_call_stop_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_book_view_call_stop_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_method_call_sync_void__void ("stop", proxy, cancellable, error);
}

void
e_gdbus_book_view_call_dispose (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_method_call_void ("dispose", proxy, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_view_call_dispose_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_book_view_call_dispose_sync (GDBusProxy *proxy, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_method_call_sync_void__void ("dispose", proxy, cancellable, error);
}

void
e_gdbus_book_view_call_set_fields_of_interest (GDBusProxy *proxy, const gchar * const *in_only_fields, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	e_gdbus_proxy_method_call_strv ("setFieldsOfInterest", proxy, in_only_fields, cancellable, callback, user_data);
}

gboolean
e_gdbus_book_view_call_set_fields_of_interest_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
	return e_gdbus_proxy_method_call_finish_void (proxy, result, error);
}

gboolean
e_gdbus_book_view_call_set_fields_of_interest_sync (GDBusProxy *proxy, const gchar * const *in_only_fields, GCancellable *cancellable, GError **error)
{
	return e_gdbus_proxy_method_call_sync_strv__void ("setFieldsOfInterest", proxy, in_only_fields, cancellable, error);
}

void
e_gdbus_book_view_emit_objects_added (EGdbusBookView *object, const gchar * const *arg_objects)
{
	g_signal_emit (object, signals[__OBJECTS_ADDED_SIGNAL], 0, arg_objects);
}

void
e_gdbus_book_view_emit_objects_modified (EGdbusBookView *object, const gchar * const *arg_objects)
{
	g_signal_emit (object, signals[__OBJECTS_MODIFIED_SIGNAL], 0, arg_objects);
}

void
e_gdbus_book_view_emit_objects_removed (EGdbusBookView *object, const gchar * const *arg_uids)
{
	g_signal_emit (object, signals[__OBJECTS_REMOVED_SIGNAL], 0, arg_uids);
}

void
e_gdbus_book_view_emit_progress (EGdbusBookView *object, guint arg_percent, const gchar *arg_message)
{
	g_signal_emit (object, signals[__PROGRESS_SIGNAL], 0, arg_percent, arg_message);
}

void
e_gdbus_book_view_emit_complete (EGdbusBookView *object, const gchar * const *arg_error)
{
	g_signal_emit (object, signals[__COMPLETE_SIGNAL], 0, arg_error);
}

E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (book_view, ObjectsAdded, objects, "as")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (book_view, ObjectsModified, objects, "as")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (book_view, ObjectsRemoved, uids, "as")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_2 (book_view, Progress, percent, "u", message, "s")
E_DECLARE_GDBUS_NOTIFY_SIGNAL_1 (book_view, Complete, error, "as")

E_DECLARE_GDBUS_SYNC_METHOD_0	(book_view, start)
E_DECLARE_GDBUS_SYNC_METHOD_0	(book_view, stop)
E_DECLARE_GDBUS_SYNC_METHOD_0	(book_view, dispose)
E_DECLARE_GDBUS_SYNC_METHOD_1	(book_view, setFieldsOfInterest, fields_of_interest, "as")

static const GDBusMethodInfo * const e_gdbus_book_view_method_info_pointers[] =
{
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book_view, start),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book_view, stop),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book_view, dispose),
	&E_DECLARED_GDBUS_METHOD_INFO_NAME (book_view, setFieldsOfInterest),
	NULL
};

static const GDBusSignalInfo * const e_gdbus_book_view_signal_info_pointers[] =
{
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book_view, ObjectsAdded),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book_view, ObjectsModified),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book_view, ObjectsRemoved),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book_view, Progress),
	&E_DECLARED_GDBUS_SIGNAL_INFO_NAME (book_view, Complete),
	NULL
};

static const GDBusInterfaceInfo _e_gdbus_book_view_interface_info =
{
	-1,
	(gchar *) GDBUS_BOOK_VIEW_INTERFACE_NAME,
	(GDBusMethodInfo **) &e_gdbus_book_view_method_info_pointers,
	(GDBusSignalInfo **) &e_gdbus_book_view_signal_info_pointers,
	(GDBusPropertyInfo **) NULL
};

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
	guint method_id, method_type;

	method_id = lookup_method_id_from_method_name (method_name);
	method_type = lookup_method_type_from_method_name (method_name);

	g_return_if_fail (method_id != 0);
	g_return_if_fail (method_type != 0);

	e_gdbus_stub_handle_method_call (user_data, invocation, parameters, method_name, signals[method_id], method_type);
}

static GVariant *
get_property (GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *property_name, GError **error, gpointer user_data)
{
	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "This implementation does not support property `%s'", property_name);
	return NULL;
}

static gboolean
set_property (GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *property_name, GVariant *value, GError **error, gpointer user_data)
{
	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "This implementation does not support property `%s'", property_name);
	return FALSE;
}

static const GDBusInterfaceVTable e_gdbus_book_view_interface_vtable =
{
  handle_method_call,
  get_property,
  set_property
};

static gboolean
emit_notifications_in_idle (gpointer user_data)
{
	GObject *object = G_OBJECT (user_data);
	GDBusConnection *connection;
	const gchar *path;
	GHashTable *notification_queue;
	GHashTableIter iter;
	const gchar *property_name;
	GVariant *value;
	GVariantBuilder *builder;
	GVariantBuilder *invalidated_builder;
	GHashTable *pvc;
	gboolean has_changes;

	notification_queue = g_object_get_data (object, "gdbus-codegen-notification-queue");
	path = g_object_get_data (object, "gdbus-codegen-path");
	connection = g_object_get_data (object, "gdbus-codegen-connection");
	pvc = g_object_get_data (object, "gdbus-codegen-pvc");
	g_assert (notification_queue != NULL && path != NULL && connection != NULL && pvc != NULL);

	builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
	invalidated_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
	g_hash_table_iter_init (&iter, notification_queue);
	has_changes = FALSE;
	while (g_hash_table_iter_next (&iter, (gpointer) &property_name, (gpointer) &value)) {
		GVariant *cached_value;
		cached_value = g_hash_table_lookup (pvc, property_name);
		if (cached_value == NULL || !g_variant_equal (cached_value, value)) {
			g_hash_table_insert (pvc, (gpointer) property_name, (gpointer) g_variant_ref (value));
			g_variant_builder_add (builder, "{sv}", property_name, value);
			has_changes = TRUE;
		}
	}

	if (has_changes) {
		g_dbus_connection_emit_signal (connection,
					NULL,
					path,
					"org.freedesktop.DBus.Properties",
					"PropertiesChanged",
					g_variant_new ("(sa{sv}as)",
							GDBUS_BOOK_VIEW_INTERFACE_NAME,
							builder,
							invalidated_builder),
					NULL);
	} else {
		g_variant_builder_unref (builder);
		g_variant_builder_unref (invalidated_builder);
	}

	g_hash_table_remove_all (notification_queue);
	g_object_set_data (object, "gdbus-codegen-notification-idle-id", GUINT_TO_POINTER (0));
	return FALSE;
}

/**
 * e_gdbus_book_view_drain_notify:
 * @object: A #EGdbusBookView that is exported.
 *
 * If @object has queued notifications, empty the queue forcing
 * the <literal>PropertiesChanged</literal> signal to be emitted.
 * See <xref linkend="EGdbusBookView.description"/> for more background information.
 */
void
e_gdbus_book_view_drain_notify (EGdbusBookView *object)
{
	gint idle_id;
	idle_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (object), "gdbus-codegen-notification-idle-id"));
	if (idle_id > 0) {
		emit_notifications_in_idle (object);
		g_source_remove (idle_id);
	}
}

static void
on_object_unregistered (GObject *object)
{
	gint idle_id;
	idle_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (object), "gdbus-codegen-notification-idle-id"));
	if (idle_id > 0) {
		g_source_remove (idle_id);
	}
	g_object_set_data (G_OBJECT (object), "gdbus-codegen-path", NULL);
	g_object_set_data (G_OBJECT (object), "gdbus-codegen-connection", NULL);
}

/**
 * e_gdbus_book_view_register_object:
 * @object: An instance of a #GObject<!-- -->-derived type implementing the #EGdbusBookView interface.
 * @connection: A #GDBusConnection.
 * @object_path: The object to register the object at.
 * @error: Return location for error or %NULL.
 *
 * Registers @object at @object_path on @connection.
 *
 * See <xref linkend="EGdbusBookView.description"/>
 * for how properties, methods and signals are handled.
 *
 * Returns: 0 if @error is set, otherwise a registration id (never 0) that can be used with g_dbus_connection_unregister_object().
 */
guint
e_gdbus_book_view_register_object (EGdbusBookView *object, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	GHashTable *pvc;

	pvc = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_variant_unref);

	g_object_set_data_full (G_OBJECT (object), "gdbus-codegen-path", (gpointer) g_strdup (object_path), g_free);
	g_object_set_data (G_OBJECT (object), "gdbus-codegen-connection", (gpointer) connection);
	g_object_set_data_full (G_OBJECT (object), "gdbus-codegen-pvc", (gpointer) pvc, (GDestroyNotify) g_hash_table_unref);
	return g_dbus_connection_register_object (connection,
			object_path,
			(GDBusInterfaceInfo *) &_e_gdbus_book_view_interface_info,
			&e_gdbus_book_view_interface_vtable,
			object,
			(GDestroyNotify) on_object_unregistered,
			error);
}

/**
 * e_gdbus_book_view_interface_info:
 *
 * Gets interface description for the <literal>org.gnome.evolution.dataserver.AddressBookView</literal> D-Bus interface.
 *
 * Returns: A #GDBusInterfaceInfo. Do not free, the object is statically allocated.
 */
const GDBusInterfaceInfo *
e_gdbus_book_view_interface_info (void)
{
	return &_e_gdbus_book_view_interface_info;
}

/* ---------------------------------------------------------------------- */

static void proxy_iface_init (EGdbusBookViewIface *iface);

G_DEFINE_TYPE_WITH_CODE (EGdbusBookViewProxy, e_gdbus_book_view_proxy, G_TYPE_DBUS_PROXY,
                         G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_BOOK_VIEW, proxy_iface_init));

static void
e_gdbus_book_view_proxy_init (EGdbusBookViewProxy *proxy)
{
	g_dbus_proxy_set_interface_info (G_DBUS_PROXY (proxy), (GDBusInterfaceInfo *) &_e_gdbus_book_view_interface_info);
}

static void
g_signal (GDBusProxy *proxy, const gchar *sender_name, const gchar *signal_name, GVariant *parameters)
{
	guint signal_id, signal_type;

	signal_id = lookup_signal_id_from_signal_name (signal_name);
	signal_type = lookup_signal_type_from_signal_name (signal_name);

	g_return_if_fail (signal_id != 0);
	g_return_if_fail (signal_type != 0);

	e_gdbus_proxy_emit_signal (proxy, parameters, signals[signal_id], signal_type);
}

static void
e_gdbus_book_view_proxy_class_init (EGdbusBookViewProxyClass *klass)
{
	GDBusProxyClass *proxy_class;

	proxy_class = G_DBUS_PROXY_CLASS (klass);
	proxy_class->g_signal = g_signal;
}

static void
proxy_iface_init (EGdbusBookViewIface *iface)
{
}

/**
 * e_gdbus_book_view_proxy_new:
 * @connection: A #GDBusConnection.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Like g_dbus_proxy_new() but returns a #EGdbusBookViewProxy.
 *
 * This is a failable asynchronous constructor - when the proxy is ready, callback will be invoked and you can use e_gdbus_book_view_proxy_new_finish() to get the result.
 */
void
e_gdbus_book_view_proxy_new (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	g_async_initable_new_async (E_TYPE_GDBUS_BOOK_VIEW_PROXY,
				G_PRIORITY_DEFAULT,
				cancellable,
				callback,
				user_data,
				"g-flags", flags,
				"g-name", name,
				"g-connection", connection,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_BOOK_VIEW_INTERFACE_NAME,
				NULL);
}

/**
 * e_gdbus_book_view_proxy_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to e_gdbus_book_view_proxy_new().
 * @error: Return location for error or %NULL.
 *
 * Finishes creating a #EGdbusBookViewProxy.
 *
 * Returns: A #EGdbusBookViewProxy or %NULL if @error is set. Free with g_object_unref().
 */
EGdbusBookView *
e_gdbus_book_view_proxy_new_finish (GAsyncResult  *result, GError **error)
{
	GObject *object;
	GObject *source_object;
	source_object = g_async_result_get_source_object (result);
	g_assert (source_object != NULL);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, error);
	g_object_unref (source_object);
	if (object != NULL)
		return E_GDBUS_BOOK_VIEW (object);
	else
		return NULL;
}

/**
 * e_gdbus_book_view_proxy_new_sync:
 * @connection: A #GDBusConnection.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like g_dbus_proxy_new_sync() but returns a #EGdbusBookViewProxy.
 *
 * This is a synchronous failable constructor. See e_gdbus_book_view_proxy_new() and e_gdbus_book_view_proxy_new_finish() for the asynchronous version.
 *
 * Returns: A #EGdbusBookViewProxy or %NULL if error is set. Free with g_object_unref().
 */
EGdbusBookView *
e_gdbus_book_view_proxy_new_sync (GDBusConnection *connection, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error)
{
	GInitable *initable;
	initable = g_initable_new (E_TYPE_GDBUS_BOOK_VIEW_PROXY,
				cancellable,
				error,
				"g-flags", flags,
				"g-name", name,
				"g-connection", connection,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_BOOK_VIEW_INTERFACE_NAME,
				NULL);
	if (initable != NULL)
		return E_GDBUS_BOOK_VIEW (initable);
	else
		return NULL;
}

/**
 * e_gdbus_book_view_proxy_new_for_bus:
 * @bus_type: A #GBusType.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Like g_dbus_proxy_new_for_bus() but returns a #EGdbusBookViewProxy.
 *
 * This is a failable asynchronous constructor - when the proxy is ready, callback will be invoked and you can use e_gdbus_book_view_proxy_new_for_bus_finish() to get the result.
 */
void
e_gdbus_book_view_proxy_new_for_bus (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	g_async_initable_new_async (E_TYPE_GDBUS_BOOK_VIEW_PROXY,
				G_PRIORITY_DEFAULT,
				cancellable,
				callback,
				user_data,
				"g-flags", flags,
				"g-name", name,
				"g-bus-type", bus_type,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_BOOK_VIEW_INTERFACE_NAME,
				NULL);
}

/**
 * e_gdbus_book_view_proxy_new_for_bus_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to e_gdbus_book_view_proxy_new_for_bus().
 * @error: Return location for error or %NULL.
 *
 * Finishes creating a #EGdbusBookViewProxy.
 *
 * Returns: A #EGdbusBookViewProxy or %NULL if @error is set. Free with g_object_unref().
 */
EGdbusBookView *
e_gdbus_book_view_proxy_new_for_bus_finish (GAsyncResult *result, GError **error)
{
	GObject *object;
	GObject *source_object;
	source_object = g_async_result_get_source_object (result);
	g_assert (source_object != NULL);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, error);
	g_object_unref (source_object);
	if (object != NULL)
		return E_GDBUS_BOOK_VIEW (object);
	else
		return NULL;
}

/**
 * e_gdbus_book_view_proxy_new_for_bus_sync:
 * @bus_type: A #GBusType.
 * @flags: Flags used when constructing the proxy.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like g_dbus_proxy_new_for_bus_sync() but returns a #EGdbusBookViewProxy.
 *
 * This is a synchronous failable constructor. See e_gdbus_book_view_proxy_new_for_bus() and e_gdbus_book_view_proxy_new_for_bus_finish() for the asynchronous version.
 *
 * Returns: A #EGdbusBookViewProxy or %NULL if error is set. Free with g_object_unref().
 */
EGdbusBookView *
e_gdbus_book_view_proxy_new_for_bus_sync (GBusType bus_type, GDBusProxyFlags flags, const gchar *name, const gchar *object_path, GCancellable *cancellable, GError **error)
{
	GInitable *initable;
	initable = g_initable_new (E_TYPE_GDBUS_BOOK_VIEW_PROXY,
				cancellable,
				error,
				"g-flags", flags,
				"g-name", name,
				"g-bus-type", bus_type,
				"g-object-path", object_path,
				"g-interface-name", GDBUS_BOOK_VIEW_INTERFACE_NAME,
				NULL);
	if (initable != NULL)
		return E_GDBUS_BOOK_VIEW (initable);
	else
		return NULL;
}

/* ---------------------------------------------------------------------- */

struct _EGdbusBookViewStubPrivate
{
	gint foo;
};

static void stub_iface_init (EGdbusBookViewIface *iface);

G_DEFINE_TYPE_WITH_CODE (EGdbusBookViewStub, e_gdbus_book_view_stub, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (E_TYPE_GDBUS_BOOK_VIEW, stub_iface_init));

static void
e_gdbus_book_view_stub_init (EGdbusBookViewStub *stub)
{
	stub->priv = G_TYPE_INSTANCE_GET_PRIVATE (stub, E_TYPE_GDBUS_BOOK_VIEW_STUB, EGdbusBookViewStubPrivate);
}

static void
e_gdbus_book_view_stub_class_init (EGdbusBookViewStubClass *klass)
{
	g_type_class_add_private (klass, sizeof (EGdbusBookViewStubPrivate));
}

static void
stub_iface_init (EGdbusBookViewIface *iface)
{
}

/**
 * e_gdbus_book_view_stub_new:
 *
 * Creates a new stub object that can be exported via e_gdbus_book_view_register_object().
 *
 * Returns: A #EGdbusBookViewStub instance. Free with g_object_unref().
 */
EGdbusBookView *
e_gdbus_book_view_stub_new (void)
{
	return E_GDBUS_BOOK_VIEW (g_object_new (E_TYPE_GDBUS_BOOK_VIEW_STUB, NULL));
}
