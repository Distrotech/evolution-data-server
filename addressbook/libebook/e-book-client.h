/*
 * e-book-client.h
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

#ifndef E_BOOK_CLIENT_H
#define E_BOOK_CLIENT_H

#include <glib.h>
#include <gio/gio.h>

#include <libedataserver/e-client.h>
#include <libedataserver/e-source-list.h>
#include <libebook/e-book-query.h>
#include <libebook/e-book-view.h>

#define E_TYPE_BOOK_CLIENT		(e_book_client_get_type ())
#define E_BOOK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_CLIENT, EBookClient))
#define E_BOOK_CLIENT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_BOOK_CLIENT, EBookClientClass))
#define E_IS_BOOK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_CLIENT))
#define E_IS_BOOK_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_CLIENT))
#define E_BOOK_CLIENT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_CLIENT, EBookClientClass))

G_BEGIN_DECLS

#define E_BOOK_CLIENT_ERROR e_book_client_error_quark ()

GQuark e_book_client_error_quark (void) G_GNUC_CONST;

typedef enum {
	E_BOOK_CLIENT_ERROR_SUCCESS,
	E_BOOK_CLIENT_ERROR_INVALID_ARG,
	E_BOOK_CLIENT_ERROR_BUSY,
	E_BOOK_CLIENT_ERROR_REPOSITORY_OFFLINE,
	E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK,
	E_BOOK_CLIENT_ERROR_NO_SELF_CONTACT,
	E_BOOK_CLIENT_ERROR_SOURCE_NOT_LOADED,
	E_BOOK_CLIENT_ERROR_SOURCE_ALREADY_LOADED,
	E_BOOK_CLIENT_ERROR_PERMISSION_DENIED,
	E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
	E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS,
	E_BOOK_CLIENT_ERROR_PROTOCOL_NOT_SUPPORTED,
	E_BOOK_CLIENT_ERROR_CANCELLED,
	E_BOOK_CLIENT_ERROR_COULD_NOT_CANCEL,
	E_BOOK_CLIENT_ERROR_AUTHENTICATION_FAILED,
	E_BOOK_CLIENT_ERROR_AUTHENTICATION_REQUIRED,
	E_BOOK_CLIENT_ERROR_TLS_NOT_AVAILABLE,
	E_BOOK_CLIENT_ERROR_DBUS_ERROR,
	E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE,
	E_BOOK_CLIENT_ERROR_OFFLINE_UNAVAILABLE,
	E_BOOK_CLIENT_ERROR_OTHER_ERROR,
	E_BOOK_CLIENT_ERROR_INVALID_SERVER_VERSION,
	E_BOOK_CLIENT_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD,
	E_BOOK_CLIENT_ERROR_NO_SPACE,
	E_BOOK_CLIENT_ERROR_NOT_SUPPORTED
} EBookClientError;

const gchar *e_book_client_error_to_string (EBookClientError code);

typedef struct _EBookClient        EBookClient;
typedef struct _EBookClientClass   EBookClientClass;
typedef struct _EBookClientPrivate EBookClientPrivate;

struct _EBookClient {
	EClient parent;

	/*< private >*/
	EBookClientPrivate *priv;
};

struct _EBookClientClass {
	EClientClass parent;
};

GType		e_book_client_get_type				(void);

/* Creating a new addressbook */
EBookClient *	e_book_client_new				(ESource *source, GError **error);
EBookClient *	e_book_client_new_from_uri			(const gchar *uri, GError **error);
EBookClient *	e_book_client_new_system_addressbook		(GError **error);
EBookClient *	e_book_client_new_default_addressbook		(GError **error);

/* Addressbook discovery */
gboolean	e_book_client_set_default			(EBookClient *client, GError **error);
gboolean	e_book_client_set_default_source		(ESource *source, GError **error);
gboolean	e_book_client_get_sources			(ESourceList **sources, GError **error);

/* Identity */
gboolean	e_book_client_get_self				(EContact **contact, EBookClient **client, GError **error);
gboolean	e_book_client_set_self				(EBookClient *client, EContact *contact, GError **error);
gboolean	e_book_client_is_self				(EContact *contact);

/* Addressbook methods */
guint32		e_book_client_get_capabilities			(EBookClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_get_capabilities_finish		(EBookClient *client, GAsyncResult *result, GSList **capabilities, GError **error);
gboolean	e_book_client_get_capabilities_sync		(EBookClient *client, GSList **capabilities, GCancellable *cancellable, GError **error);

guint32		e_book_client_get_required_fields		(EBookClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_get_required_fields_finish	(EBookClient *client, GAsyncResult *result, GSList **fields, GError **error);
gboolean	e_book_client_get_required_fields_sync		(EBookClient *client, GSList **fields, GCancellable *cancellable, GError **error);

guint32		e_book_client_get_supported_fields		(EBookClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_get_supported_fields_finish	(EBookClient *client, GAsyncResult *result, GSList **fields, GError **error);
gboolean	e_book_client_get_supported_fields_sync		(EBookClient *client, GSList **fields, GCancellable *cancellable, GError **error);

guint32		e_book_client_get_supported_auth_methods	(EBookClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_get_supported_auth_methods_finish	(EBookClient *client, GAsyncResult *result, GSList **auth_methods, GError **error);
gboolean	e_book_client_get_supported_auth_methods_sync	(EBookClient *client, GSList **auth_methods, GCancellable *cancellable, GError **error);

guint32		e_book_client_add_contact			(EBookClient *client, const EContact *contact, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_add_contact_finish		(EBookClient *client, GAsyncResult *result, gchar **added_uid, GError **error);
gboolean	e_book_client_add_contact_sync			(EBookClient *client, const EContact *contact, gchar **added_uid, GCancellable *cancellable, GError **error);

guint32		e_book_client_modify_contact			(EBookClient *client, const EContact *contact, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_modify_contact_finish		(EBookClient *client, GAsyncResult *result, GError **error);
gboolean	e_book_client_modify_contact_sync		(EBookClient *client, const EContact *contact, GCancellable *cancellable, GError **error);

guint32		e_book_client_remove_contact			(EBookClient *client, const EContact *contact, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_remove_contact_finish		(EBookClient *client, GAsyncResult *result, GError **error);
gboolean	e_book_client_remove_contact_sync		(EBookClient *client, const EContact *contact, GCancellable *cancellable, GError **error);

guint32		e_book_client_remove_contact_by_uid		(EBookClient *client, const gchar *uid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_remove_contact_by_uid_finish	(EBookClient *client, GAsyncResult *result, GError **error);
gboolean	e_book_client_remove_contact_by_uid_sync	(EBookClient *client, const gchar *uid, GCancellable *cancellable, GError **error);

guint32		e_book_client_remove_contacts			(EBookClient *client, const GSList *uids, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_remove_contacts_finish		(EBookClient *client, GAsyncResult *result, GError **error);
gboolean	e_book_client_remove_contacts_sync		(EBookClient *client, const GSList *uids, GCancellable *cancellable, GError **error);

guint32		e_book_client_get_contact			(EBookClient *client, const gchar *uid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_get_contact_finish		(EBookClient *client, GAsyncResult *result, EContact **contact, GError **error);
gboolean	e_book_client_get_contact_sync			(EBookClient *client, const gchar *uid, EContact **contact, GCancellable *cancellable, GError **error);

guint32		e_book_client_get_contacts			(EBookClient *client, const EBookQuery *query, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_get_contacts_finish		(EBookClient *client, GAsyncResult *result, GSList **contacts, GError **error);
gboolean	e_book_client_get_contacts_sync			(EBookClient *client, const EBookQuery *query, GSList **contacts, GCancellable *cancellable, GError **error);

guint32		e_book_client_get_view				(EBookClient *client, const EBookQuery *query, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_book_client_get_view_finish			(EBookClient *client, GAsyncResult *result, EBookView **book_view, GError **error);
gboolean	e_book_client_get_view_sync			(EBookClient *client, const EBookQuery *query, EBookView **book_view, GCancellable *cancellable, GError **error);

G_END_DECLS

#endif /* E_BOOK_CLIENT_H */
