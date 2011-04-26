/*
 * e-client.h
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

#ifndef E_CLIENT_H
#define E_CLIENT_H

#include <glib.h>
#include <gio/gio.h>

#include <libedataserver/e-credentials.h>
#include <libedataserver/e-source.h>
#include <libedataserver/e-source-list.h>

#define E_TYPE_CLIENT		(e_client_get_type ())
#define E_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_CLIENT, EClient))
#define E_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_CLIENT, EClientClass))
#define E_IS_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_CLIENT))
#define E_IS_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_CLIENT))
#define E_CLIENT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CLIENT, EClientClass))

typedef struct _EClient        EClient;
typedef struct _EClientClass   EClientClass;
typedef struct _EClientPrivate EClientPrivate;

struct _EClient {
	GObject parent;

	/*< private >*/
	EClientPrivate *priv;
};

struct _EClientClass {
	GObjectClass parent;

	/* virtual methods */
	GDBusProxy *	(* get_dbus_proxy) (EClient *client);
	void		(* unwrap_dbus_error) (EClient *client, GError *dbus_error, GError **out_error);

	guint32		(* open) (EClient *client, gboolean only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
	gboolean	(* open_finish) (EClient *client, GAsyncResult *result, GError **error);
	gboolean	(* open_sync) (EClient *client, gboolean only_if_exists, GCancellable *cancellable, GError **error);

	guint32		(* remove) (EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
	gboolean	(* remove_finish) (EClient *client, GAsyncResult *result, GError **error);
	gboolean	(* remove_sync) (EClient *client, GCancellable *cancellable, GError **error);

	void		(* handle_authentication) (EClient *client, const ECredentials *credentials);
	gchar *		(* retrieve_capabilities) (EClient *client);

	/* signals */
	gboolean	(* authenticate) (EClient *client, ECredentials *credentials);
	void		(* backend_error) (EClient *client, const gchar *error_msg);
	void		(* backend_died) (EClient *client);
};

GType		e_client_get_type		(void);

ESource *	e_client_get_source		(EClient *client);
const gchar *	e_client_get_uri		(EClient *client);
const GSList *	e_client_get_capabilities	(EClient *client);
gboolean	e_client_check_capability	(EClient *client, const gchar *capability);
gboolean	e_client_is_readonly		(EClient *client);
gboolean	e_client_is_online		(EClient *client);
gboolean	e_client_is_opened		(EClient *client);

void		e_client_cancel_op		(EClient *client, guint32 opid);
void		e_client_cancel_all		(EClient *client);

guint32		e_client_open			(EClient *client, gboolean only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_client_open_finish		(EClient *client, GAsyncResult *result, GError **error);
gboolean	e_client_open_sync		(EClient *client, gboolean only_if_exists, GCancellable *cancellable, GError **error);

guint32		e_client_remove			(EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_client_remove_finish		(EClient *client, GAsyncResult *result, GError **error);
gboolean	e_client_remove_sync		(EClient *client, GCancellable *cancellable, GError **error);

/* utility functions */
gchar **	e_client_util_slist_to_strv	(const GSList *strings);
GSList *	e_client_util_strv_to_slist	(const gchar * const *strv);
GSList *	e_client_util_copy_string_slist	(GSList *copy_to, const GSList *strings);
GSList *	e_client_util_copy_object_slist	(GSList *copy_to, const GSList *objects);
void		e_client_util_free_string_slist	(GSList *strings);
void		e_client_util_free_object_slist	(GSList *objects);
GSList *	e_client_util_parse_capabilities(const gchar *capabilities);

/* protected functions */
void		e_client_set_capabilities	(EClient *client, const gchar *capabilities);
void		e_client_set_readonly		(EClient *client, gboolean readonly);
void		e_client_set_online		(EClient *client, gboolean is_online);
guint32		e_client_register_op		(EClient *client, GCancellable *cancellable);
void		e_client_unregister_op		(EClient *client, guint32 opid);
void		e_client_process_authentication	(EClient *client, const ECredentials *credentials);

gboolean	e_client_emit_authenticate	(EClient *client, ECredentials *credentials);
void		e_client_emit_backend_error	(EClient *client, const gchar *error_msg);
void		e_client_emit_backend_died	(EClient *client);

ESource *	e_client_util_get_system_source	(ESourceList *source_list);
gboolean	e_client_util_set_default	(ESourceList *source_list, ESource *source);
ESource *	e_client_util_get_source_for_uri(ESourceList *source_list, const gchar *uri);

/* protected functions simplifying sync/async calls */
GDBusProxy *	e_client_get_dbus_proxy		(EClient *client);
void		e_client_unwrap_dbus_error	(EClient *client, GError *dbus_error, GError **out_error);

typedef gboolean (* EClientProxyFinishVoidFunc)		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
typedef gboolean (* EClientProxyFinishBooleanFunc)	(GDBusProxy *proxy, GAsyncResult *result, gboolean *out_boolean, GError **error);
typedef gboolean (* EClientProxyFinishStringFunc)	(GDBusProxy *proxy, GAsyncResult *result, gchar **out_string, GError **error);
typedef gboolean (* EClientProxyFinishStrvFunc)		(GDBusProxy *proxy, GAsyncResult *result, gchar ***out_strv, GError **error);
typedef gboolean (* EClientProxyFinishUintFunc)		(GDBusProxy *proxy, GAsyncResult *result, guint *out_uint, GError **error);

guint32		e_client_proxy_call_void	(EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);
guint32		e_client_proxy_call_boolean	(EClient *client, gboolean in_boolean, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);
guint32		e_client_proxy_call_string	(EClient *client, const gchar *in_string, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, const gchar * in_string, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);
guint32		e_client_proxy_call_strv	(EClient *client, const gchar * const *in_strv, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, const gchar * const * in_strv, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);
guint32		e_client_proxy_call_uint	(EClient *client, guint in_uint, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);

gboolean	e_client_proxy_call_finish_void		(EClient *client, GAsyncResult *result, GError **error, gpointer source_tag);
gboolean	e_client_proxy_call_finish_boolean	(EClient *client, GAsyncResult *result, gboolean *out_boolean, GError **error, gpointer source_tag);
gboolean	e_client_proxy_call_finish_string	(EClient *client, GAsyncResult *result, gchar **out_string, GError **error, gpointer source_tag);
gboolean	e_client_proxy_call_finish_strv		(EClient *client, GAsyncResult *result, gchar ***out_strv, GError **error, gpointer source_tag);
gboolean	e_client_proxy_call_finish_uint		(EClient *client, GAsyncResult *result, guint *out_uint, GError **error, gpointer source_tag);

gboolean	e_client_proxy_call_sync_void__void		(EClient *client, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_void__boolean		(EClient *client, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_void__string		(EClient *client, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_void__strv		(EClient *client, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_void__uint		(EClient *client, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint *out_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__void		(EClient *client, gboolean in_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__boolean	(EClient *client, gboolean in_boolean, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__string	(EClient *client, gboolean in_boolean, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__strv		(EClient *client, gboolean in_boolean, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__uint		(EClient *client, gboolean in_boolean, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, guint *out_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__void		(EClient *client, const gchar *in_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__boolean	(EClient *client, const gchar *in_string, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__string		(EClient *client, const gchar *in_string, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__strv		(EClient *client, const gchar *in_string, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__uint		(EClient *client, const gchar *in_string, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, guint *out_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__void		(EClient *client, const gchar * const *in_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__boolean		(EClient *client, const gchar * const *in_strv, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__string		(EClient *client, const gchar * const *in_strv, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__strv		(EClient *client, const gchar * const *in_strv, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__uint		(EClient *client, const gchar * const *in_strv, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, guint *out_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__void		(EClient *client, guint in_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__boolean		(EClient *client, guint in_uint, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__string		(EClient *client, guint in_uint, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__strv		(EClient *client, guint in_uint, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__uint		(EClient *client, guint in_uint, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, guint *out_uint, GCancellable *cancellable, GError **error));

G_END_DECLS

#endif /* E_CLIENT_H */
