/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-book-backend-sync.h"

G_DEFINE_TYPE (EBookBackendSync, e_book_backend_sync, E_TYPE_BOOK_BACKEND)

struct _EBookBackendSyncPrivate {
	gint mumble;
};

static GObjectClass *parent_class;

/**
 * e_book_backend_sync_construct:
 * @backend: an #EBookBackendSync
 *
 * Does nothing.
 *
 * Returns: %TRUE.
 **/
gboolean
e_book_backend_sync_construct (EBookBackendSync *backend)
{
	return TRUE;
}

/**
 * e_book_backend_sync_open:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @only_if_exists: whether open only if exists
 * @error: #GError to set, when something fails
 *
 * Opens @backend, which can involve connecting it to a remote server.
 **/
void
e_book_backend_sync_open (EBookBackendSync *backend,
			  EDataBook *book,
			  GCancellable *cancellable,
			  gboolean only_if_exists,
			  GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->open_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->open_sync) (backend, book, cancellable, only_if_exists, error);
}

/**
 * e_book_backend_sync_create_contact:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @vcard: a VCard representation of a contact
 * @contact: a pointer to a location to store the resulting #EContact
 * @error: #GError to set, when something fails
 *
 * Creates a new contact with the contents of @vcard in @backend.
 **/
void
e_book_backend_sync_create_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    GCancellable *cancellable,
				    const gchar *vcard,
				    EContact **contact,
				    GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (vcard, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (contact, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contact_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->create_contact_sync) (backend, book, cancellable, vcard, contact, error);
}

/**
 * e_book_backend_sync_remove:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @error: #GError to set, when something fails
 *
 * Remove @book's database and storage overhead from the storage
 * medium. This will delete all contacts in @book.
 **/
void
e_book_backend_sync_remove (EBookBackendSync *backend,
			    EDataBook *book,
			    GCancellable *cancellable,
			    GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_sync) (backend, book, cancellable, error);
}

/**
 * e_book_backend_sync_get_capabilities:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @capabilities: a string of comma-separated capabilities for this backend
 * @removed_ids: a pointer to a location to store a list of the contacts actually removed
 * @error: #GError to set, when something fails
 *
 * Queries for @capabilities of the @backend.
 **/
void
e_book_backend_sync_get_capabilities (EBookBackendSync *backend,
				      EDataBook *book,
				      GCancellable *cancellable,
				      gchar **capabilities,
				      GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (capabilities, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_capabilities_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_capabilities_sync) (backend, book, cancellable, capabilities, error);
}

/**
 * e_book_backend_sync_remove_contacts:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @id_list: a #GSList of pointers to unique contact ID strings
 * @removed_ids: a pointer to a location to store a list of the contacts actually removed
 * @error: #GError to set, when something fails
 *
 * Removes the contacts specified by @id_list from @backend. The returned list
 * of removed contacts is in the same format as the passed-in list, and must be
 * freed by the caller.
 **/
void
e_book_backend_sync_remove_contacts (EBookBackendSync *backend,
				     EDataBook *book,
				     GCancellable *cancellable,
				     const GSList *id_list,
				     GSList **removed_ids,
				     GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (id_list, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (removed_ids, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_contacts_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->remove_contacts_sync) (backend, book, cancellable, id_list, removed_ids, error);
}

/**
 * e_book_backend_sync_modify_contact:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @vcard: the string representation of a contact
 * @contact: a pointer to a location to store the resulting #EContact
 * @error: #GError to set, when something fails
 *
 * Modifies the contact specified by the ID embedded in @vcard, to
 * reflect the full contents of @vcard.
 **/
void
e_book_backend_sync_modify_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    GCancellable *cancellable,
				    const gchar *vcard,
				    EContact **contact,
				    GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (vcard, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (contact, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contact_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->modify_contact_sync) (backend, book, cancellable, vcard, contact, error);
}

/**
 * e_book_backend_sync_get_contact:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @id: a unique contact ID
 * @vcard: a pointer to a location to store the resulting VCard string
 *
 * Gets a contact from @book.
 **/
void
e_book_backend_sync_get_contact (EBookBackendSync *backend,
				 EDataBook *book,
				 GCancellable *cancellable,
				 const gchar *id,
				 gchar **vcard,
				 GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (id, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (vcard, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_sync) (backend, book, cancellable, id, vcard, error);
}

/**
 * e_book_backend_sync_get_contact_list:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @query: an s-expression of the query to perform
 * @contacts: a pointer to a location to store the resulting list of VCard strings
 * @error: #GError to set, when something fails
 *
 * Gets a list of contacts from @book. The list and its elements must be freed
 * by the caller.
 **/
void
e_book_backend_sync_get_contact_list (EBookBackendSync *backend,
				      EDataBook *book,
				      GCancellable *cancellable,
				      const gchar *query,
				      GSList **contacts,
				      GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (query, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (contacts, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_contact_list_sync) (backend, book, cancellable, query, contacts, error);
}

/**
 * e_book_backend_sync_authenticate_user:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @credentials: an #ECredentials to authenticate with
 * @error: #GError to set, when something fails
 *
 * Authenticates @backend with given @credentials.
 **/
void
e_book_backend_sync_authenticate_user (EBookBackendSync *backend,
				       EDataBook *book,
				       GCancellable *cancellable,
				       ECredentials *credentials,
				       GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (credentials, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->authenticate_user_sync) (backend, book, cancellable, credentials, error);
}

/**
 * e_book_backend_sync_get_required_fields:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @fields: a pointer to a location to store the fields
 * @error: #GError to set, when something fails
 *
 * Gets a list of the fields required for all contacts in @book. The
 * fields are represented by strings from #e_contact_field_name. The list
 * and its contents must be freed by the caller.
 **/
void
e_book_backend_sync_get_required_fields (EBookBackendSync *backend,
					 EDataBook *book,
					 GCancellable *cancellable,
					 GSList **fields,
					 GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (fields, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_required_fields_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_required_fields_sync) (backend, book, cancellable, fields, error);
}

/**
 * e_book_backend_sync_get_supported_fields:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @fields: a pointer to a location to store the fields
 * @error: #GError to set, when something fails
 *
 * Gets a list of the fields supported for contacts in @book. Other fields
 * may not be stored. The fields are represented by strings from #e_contact_field_name.
 * The list and its contents must be freed by the caller.
 **/
void
e_book_backend_sync_get_supported_fields (EBookBackendSync *backend,
					  EDataBook *book,
					  GCancellable *cancellable,
					  GSList **fields,
					  GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (fields, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_fields_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_fields_sync) (backend, book, cancellable, fields, error);
}

/**
 * e_book_backend_sync_get_supported_auth_methods:
 * @backend: an #EBookBackendSync
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @methods: a pointer to a location to store the methods
 * @error: #GError to set, when something fails
 *
 * Gets a list of the authentication methods supported by @book. The
 * methods are represented by strings. The list and its contents must
 * be freed by the caller.
 **/
void
e_book_backend_sync_get_supported_auth_methods (EBookBackendSync *backend,
						EDataBook *book,
						GCancellable *cancellable,
						GSList **methods,
						GError **error)
{
	e_return_data_book_error_if_fail (E_IS_BOOK_BACKEND_SYNC (backend), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_IS_DATA_BOOK (book), E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (methods, E_DATA_BOOK_STATUS_INVALID_ARG);
	e_return_data_book_error_if_fail (E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_auth_methods_sync, E_DATA_BOOK_STATUS_NOT_SUPPORTED);

	(* E_BOOK_BACKEND_SYNC_GET_CLASS (backend)->get_supported_auth_methods_sync) (backend, book, cancellable, methods, error);
}

static void
_e_book_backend_open (EBookBackend *backend,
		      EDataBook    *book,
		      guint32       opid,
		      GCancellable *cancellable,
		      gboolean only_if_exists)
{
	GError *error = NULL;

	e_book_backend_sync_open (E_BOOK_BACKEND_SYNC (backend), book, cancellable, only_if_exists, &error);

	e_data_book_respond_open (book, opid, error);
}

static void
_e_book_backend_remove (EBookBackend *backend,
			EDataBook    *book,
			guint32       opid,
			GCancellable *cancellable)
{
	GError *error = NULL;

	e_book_backend_sync_remove (E_BOOK_BACKEND_SYNC (backend), book, cancellable, &error);

	e_data_book_respond_remove (book, opid, error);
}

static void
_e_book_backend_get_capabilities (EBookBackend *backend,
				  EDataBook    *book,
				  guint32       opid,
				  GCancellable *cancellable)
{
	GError *error = NULL;
	gchar *capabilities = NULL;

	e_book_backend_sync_get_capabilities (E_BOOK_BACKEND_SYNC (backend), book, cancellable, &capabilities, &error);

	e_data_book_respond_get_capabilities (book, opid, error, capabilities);

	g_free (capabilities);
}

static void
_e_book_backend_create_contact (EBookBackend *backend,
				EDataBook    *book,
				guint32       opid,
				GCancellable *cancellable,
				const gchar   *vcard)
{
	GError *error = NULL;
	EContact *contact = NULL;

	e_book_backend_sync_create_contact (E_BOOK_BACKEND_SYNC (backend), book, cancellable, vcard, &contact, &error);

	e_data_book_respond_create (book, opid, error, contact);

	if (contact)
		g_object_unref (contact);
}

static void
_e_book_backend_remove_contacts (EBookBackend *backend,
				 EDataBook    *book,
				 guint32       opid,
				 GCancellable *cancellable,
				 const GSList *id_list)
{
	GError *error = NULL;
	GSList *ids = NULL;

	e_book_backend_sync_remove_contacts (E_BOOK_BACKEND_SYNC (backend), book, cancellable, id_list, &ids, &error);

	e_data_book_respond_remove_contacts (book, opid, error, ids);

	if (ids) {
		g_slist_foreach (ids, (GFunc) g_free, NULL);
		g_slist_free (ids);
	}
}

static void
_e_book_backend_modify_contact (EBookBackend *backend,
				EDataBook    *book,
				guint32       opid,
				GCancellable *cancellable,
				const gchar   *vcard)
{
	GError *error = NULL;
	EContact *contact = NULL;

	e_book_backend_sync_modify_contact (E_BOOK_BACKEND_SYNC (backend), book, cancellable, vcard, &contact, &error);

	e_data_book_respond_modify (book, opid, error, contact);

	if (contact)
		g_object_unref (contact);
}

static void
_e_book_backend_get_contact (EBookBackend *backend,
			     EDataBook    *book,
			     guint32       opid,
			     GCancellable *cancellable,
			     const gchar   *id)
{
	GError *error = NULL;
	gchar *vcard = NULL;

	e_book_backend_sync_get_contact (E_BOOK_BACKEND_SYNC (backend), book, cancellable, id, &vcard, &error);

	e_data_book_respond_get_contact (book, opid, error, vcard);

	if (vcard)
		g_free (vcard);
}

static void
_e_book_backend_get_contact_list (EBookBackend *backend,
				  EDataBook    *book,
				  guint32       opid,
				  GCancellable *cancellable,
				  const gchar   *query)
{
	GError *error = NULL;
	GSList *cards = NULL;

	e_book_backend_sync_get_contact_list (E_BOOK_BACKEND_SYNC (backend), book, cancellable, query, &cards, &error);

	e_data_book_respond_get_contact_list (book, opid, error, cards);

	g_slist_foreach (cards, (GFunc) g_free, NULL);
	g_slist_free (cards);
}

static void
_e_book_backend_authenticate_user (EBookBackend *backend,
				   EDataBook    *book,
				   guint32       opid,
				   GCancellable *cancellable,
				   ECredentials *credentials)
{
	GError *error = NULL;

	e_book_backend_sync_authenticate_user (E_BOOK_BACKEND_SYNC (backend), book, cancellable, credentials, &error);

	e_data_book_respond_authenticate_user (book, opid, error);
}

static void
_e_book_backend_get_required_fields (EBookBackend *backend,
				     EDataBook    *book,
				     guint32       opid,
				     GCancellable *cancellable)
{
	GError *error = NULL;
	GSList *fields = NULL;

	e_book_backend_sync_get_required_fields (E_BOOK_BACKEND_SYNC (backend), book, cancellable, &fields, &error);

	e_data_book_respond_get_required_fields (book, opid, error, fields);

	if (fields) {
		g_slist_foreach (fields, (GFunc) g_free, NULL);
		g_slist_free (fields);
	}
}

static void
_e_book_backend_get_supported_fields (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      GCancellable *cancellable)
{
	GError *error = NULL;
	GSList *fields = NULL;

	e_book_backend_sync_get_supported_fields (E_BOOK_BACKEND_SYNC (backend), book, cancellable, &fields, &error);

	e_data_book_respond_get_supported_fields (book, opid, error, fields);

	if (fields) {
		g_slist_foreach (fields, (GFunc) g_free, NULL);
		g_slist_free (fields);
	}
}

static void
_e_book_backend_get_supported_auth_methods (EBookBackend *backend,
					    EDataBook    *book,
					    guint32       opid,
					    GCancellable *cancellable)
{
	GError *error = NULL;
	GSList *methods = NULL;

	e_book_backend_sync_get_supported_auth_methods (E_BOOK_BACKEND_SYNC (backend), book, cancellable, &methods, &error);

	e_data_book_respond_get_supported_auth_methods (book, opid, error, methods);

	if (methods) {
		g_slist_foreach (methods, (GFunc) g_free, NULL);
		g_slist_free (methods);
	}
}

static void
e_book_backend_sync_init (EBookBackendSync *backend)
{
	EBookBackendSyncPrivate *priv;

	priv          = g_new0 (EBookBackendSyncPrivate, 1);

	backend->priv = priv;
}

static void
e_book_backend_sync_dispose (GObject *object)
{
	EBookBackendSync *backend;

	backend = E_BOOK_BACKEND_SYNC (object);

	if (backend->priv) {
		g_free (backend->priv);

		backend->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_book_backend_sync_class_init (EBookBackendSyncClass *klass)
{
	GObjectClass *object_class;
	EBookBackendClass *backend_class = E_BOOK_BACKEND_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass *) klass;

	backend_class->open = _e_book_backend_open;
	backend_class->authenticate_user = _e_book_backend_authenticate_user;
	backend_class->remove = _e_book_backend_remove;
	backend_class->get_capabilities = _e_book_backend_get_capabilities;
	backend_class->create_contact = _e_book_backend_create_contact;
	backend_class->remove_contacts = _e_book_backend_remove_contacts;
	backend_class->modify_contact = _e_book_backend_modify_contact;
	backend_class->get_contact = _e_book_backend_get_contact;
	backend_class->get_contact_list = _e_book_backend_get_contact_list;
	backend_class->get_required_fields = _e_book_backend_get_required_fields;
	backend_class->get_supported_fields = _e_book_backend_get_supported_fields;
	backend_class->get_supported_auth_methods = _e_book_backend_get_supported_auth_methods;

	object_class->dispose = e_book_backend_sync_dispose;
}
