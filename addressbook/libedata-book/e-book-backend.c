/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <config.h>

#include <glib/gi18n-lib.h>

#include <libedataserver/e-data-server-util.h>

#include "e-data-book-view.h"
#include "e-data-book.h"
#include "e-book-backend.h"

struct _EBookBackendPrivate {
	GMutex *clients_mutex;
	GSList *clients;

	ESource *source;
	gboolean loaded, readonly, removed, online;

	GMutex *views_mutex;
	GSList *views;

	gchar *cache_dir;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_CACHE_DIR
};

/* Signal IDs */
enum {
	LAST_CLIENT_GONE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EBookBackend, e_book_backend, G_TYPE_OBJECT)

static void
book_backend_set_default_cache_dir (EBookBackend *backend)
{
	ESource *source;
	const gchar *user_cache_dir;
	gchar *mangled_uri;
	gchar *filename;

	user_cache_dir = e_get_user_cache_dir ();

	source = e_book_backend_get_source (backend);
	g_return_if_fail (source != NULL);

	/* Mangle the URI to not contain invalid characters. */
	mangled_uri = g_strdelimit (e_source_get_uri (source), ":/", '_');

	filename = g_build_filename (
		user_cache_dir, "addressbook", mangled_uri, NULL);
	e_book_backend_set_cache_dir (backend, filename);
	g_free (filename);

	g_free (mangled_uri);
}

static void
book_backend_get_backend_property (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (book != NULL);
	g_return_if_fail (prop_name != NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_LOADED)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_book_backend_is_loaded (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_ONLINE)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, backend->priv->online ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_READONLY)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_book_backend_is_readonly (backend) ? "TRUE" : "FALSE");
	} else if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CACHE_DIR)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_book_backend_get_cache_dir (backend));
	} else {
		e_data_book_respond_get_backend_property (book, opid, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_NOT_SUPPORTED, _("Unknown book property '%s'"), prop_name), NULL);
	}
}

static void
book_backend_set_backend_property (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (book != NULL);
	g_return_if_fail (prop_name != NULL);

	e_data_book_respond_set_backend_property (book, opid, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_NOT_SUPPORTED, _("Cannot change value of book property '%s'"), prop_name));
}

static void
book_backend_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE_DIR:
			e_book_backend_set_cache_dir (
				E_BOOK_BACKEND (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
book_backend_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE_DIR:
			g_value_set_string (
				value, e_book_backend_get_cache_dir (
				E_BOOK_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
book_backend_dispose (GObject *object)
{
	EBookBackendPrivate *priv;

	priv = E_BOOK_BACKEND (object)->priv;

	if (priv->views != NULL) {
		g_slist_free (priv->views);
		priv->views = NULL;
	}

	if (priv->source != NULL) {
		g_object_unref (priv->source);
		priv->source = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->dispose (object);
}

static void
book_backend_finalize (GObject *object)
{
	EBookBackendPrivate *priv;

	priv = E_BOOK_BACKEND (object)->priv;

	g_slist_free (priv->clients);

	g_mutex_free (priv->clients_mutex);
	g_mutex_free (priv->views_mutex);

	g_free (priv->cache_dir);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_parent_class)->finalize (object);
}

static void
e_book_backend_class_init (EBookBackendClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (EBookBackendPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = book_backend_set_property;
	object_class->get_property = book_backend_get_property;
	object_class->dispose = book_backend_dispose;
	object_class->finalize = book_backend_finalize;

	klass->get_backend_property = book_backend_get_backend_property;
	klass->set_backend_property = book_backend_set_backend_property;

	g_object_class_install_property (
		object_class,
		PROP_CACHE_DIR,
		g_param_spec_string (
			"cache-dir",
			NULL,
			NULL,
			NULL,
			G_PARAM_READWRITE));

	signals[LAST_CLIENT_GONE] = g_signal_new (
		"last-client-gone",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (EBookBackendClass, last_client_gone),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
e_book_backend_init (EBookBackend *backend)
{
	backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		backend, E_TYPE_BOOK_BACKEND, EBookBackendPrivate);

	backend->priv->clients = NULL;
	backend->priv->clients_mutex = g_mutex_new ();

	backend->priv->views = NULL;
	backend->priv->views_mutex = g_mutex_new ();
}

/**
 * e_book_backend_get_cache_dir:
 * @backend: an #EBookBackend
 *
 * Returns the cache directory for the given backend.
 *
 * Returns: the cache directory for the backend
 *
 * Since: 2.32
 **/
const gchar *
e_book_backend_get_cache_dir (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	return backend->priv->cache_dir;
}

/**
 * e_book_backend_set_cache_dir:
 * @backend: an #EBookBackend
 * @cache_dir: a local cache directory
 *
 * Sets the cache directory for the given backend.
 *
 * Note that #EBookBackend is initialized with a usable default based on
 * the #ESource given to e_book_backend_open().  Backends should
 * not override the default without good reason.
 *
 * Since: 2.32
 **/
void
e_book_backend_set_cache_dir (EBookBackend *backend,
                              const gchar *cache_dir)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (cache_dir != NULL);

	g_free (backend->priv->cache_dir);
	backend->priv->cache_dir = g_strdup (cache_dir);

	g_object_notify (G_OBJECT (backend), "cache-dir");
}

/**
 * e_book_backend_get_source:
 * @backend: An addressbook backend.
 *
 * Queries the source that an addressbook backend is serving.
 *
 * Returns: ESource for the backend.
 **/
ESource *
e_book_backend_get_source (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	return backend->priv->source;
}

/**
 * e_book_backend_open:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @only_if_exists: %TRUE to prevent the creation of a new book
 *
 * Executes an 'open' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_open().
 **/
void
e_book_backend_open (EBookBackend *backend,
		     EDataBook    *book,
		     guint32       opid,
		     GCancellable *cancellable,
		     gboolean      only_if_exists)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	if (backend->priv->loaded) {
		e_data_book_report_readonly (book, backend->priv->readonly);
		e_data_book_report_online (book, backend->priv->online);

		e_data_book_respond_open (book, opid, NULL);
	} else {
		ESource *source = e_data_book_get_source (book);

		g_return_if_fail (E_IS_BOOK_BACKEND (backend));
		g_return_if_fail (source != NULL);

		/* Subclasses may need to call e_book_backend_get_cache_dir() in
		 * their open() methods, so get the "cache-dir" property
		 * initialized before we call the method. */
		backend->priv->source = g_object_ref (source);
		book_backend_set_default_cache_dir (backend);

		g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->open != NULL);

		(* E_BOOK_BACKEND_GET_CLASS (backend)->open) (backend, book, opid, cancellable, only_if_exists);
	}
}

/**
 * e_book_backend_remove:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @cancellable: a #GCancellable for the operation
 * @opid: the ID to use for this operation
 *
 * Executes a 'remove' request to remove all of @backend's data,
 * specified by @opid on @book.
 * This might be finished with e_data_book_respond_remove().
 **/
void
e_book_backend_remove (EBookBackend *backend,
		       EDataBook    *book,
		       guint32       opid,
		       GCancellable *cancellable)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->remove);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->remove) (backend, book, opid, cancellable);
}

/**
 * e_book_backend_create_contact:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @vcard: the VCard to add
 *
 * Executes a 'create contact' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_create().
 **/
void
e_book_backend_create_contact (EBookBackend *backend,
			       EDataBook    *book,
			       guint32       opid,
			       GCancellable *cancellable,
			       const gchar   *vcard)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (vcard);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->create_contact);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->create_contact) (backend, book, opid, cancellable, vcard);
}

/**
 * e_book_backend_remove_contacts:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @id_list: list of string IDs to remove
 *
 * Executes a 'remove contacts' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_remove_contacts().
 **/
void
e_book_backend_remove_contacts (EBookBackend *backend,
				EDataBook    *book,
				guint32       opid,
			        GCancellable *cancellable,
				const GSList *id_list)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (id_list);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->remove_contacts);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->remove_contacts) (backend, book, opid, cancellable, id_list);
}

/**
 * e_book_backend_modify_contact:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @vcard: the VCard to update
 *
 * Executes a 'modify contact' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_modify().
 **/
void
e_book_backend_modify_contact (EBookBackend *backend,
			       EDataBook    *book,
			       guint32       opid,
			       GCancellable *cancellable,
			       const gchar   *vcard)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (vcard);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->modify_contact);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->modify_contact) (backend, book, opid, cancellable, vcard);
}

/**
 * e_book_backend_get_contact:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @id: the ID of the contact to get
 *
 * Executes a 'get contact' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_get_contact().
 **/
void
e_book_backend_get_contact (EBookBackend *backend,
			    EDataBook    *book,
			    guint32       opid,
			    GCancellable *cancellable,
			    const gchar   *id)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (id);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->get_contact);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->get_contact) (backend, book, opid, cancellable, id);
}

/**
 * e_book_backend_get_contact_list:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @query: the s-expression to match
 *
 * Executes a 'get contact list' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_get_contact_list().
 **/
void
e_book_backend_get_contact_list (EBookBackend *backend,
				 EDataBook    *book,
				 guint32       opid,
				 GCancellable *cancellable,
				 const gchar   *query)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (query);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->get_contact_list);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->get_contact_list) (backend, book, opid, cancellable, query);
}

/**
 * e_book_backend_start_book_view:
 * @backend: an #EBookBackend
 * @book_view: the #EDataBookView to start
 *
 * Starts running the query specified by @book_view, emitting
 * signals for matching contacts.
 **/
void
e_book_backend_start_book_view (EBookBackend  *backend,
				EDataBookView *book_view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (book_view));
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->start_book_view);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->start_book_view) (backend, book_view);
}

/**
 * e_book_backend_stop_book_view:
 * @backend: an #EBookBackend
 * @book_view: the #EDataBookView to stop
 *
 * Stops running the query specified by @book_view, emitting
 * no more signals.
 **/
void
e_book_backend_stop_book_view (EBookBackend  *backend,
			       EDataBookView *book_view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (book_view));
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->stop_book_view);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->stop_book_view) (backend, book_view);
}

/**
 * e_book_backend_authenticate_user:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @credentials: #ECredentials to use for authentication
 *
 * Executes an 'authenticate' request specified by @opid on @book
 * using @backend.
 * This might be finished with e_data_book_respond_authenticate_user().
 **/
void
e_book_backend_authenticate_user (EBookBackend *backend,
				  EDataBook    *book,
				  guint32       opid,
				  GCancellable *cancellable,
				  ECredentials *credentials)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (credentials != NULL);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->authenticate_user);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->authenticate_user) (backend, book, opid, cancellable, credentials);
}

static void
last_client_gone (EBookBackend *backend)
{
	g_signal_emit (backend, signals[LAST_CLIENT_GONE], 0);
}

/**
 * e_book_backend_add_book_view:
 * @backend: an #EBookBackend
 * @view: an #EDataBookView
 *
 * Adds @view to @backend for querying.
 **/
void
e_book_backend_add_book_view (EBookBackend *backend,
			      EDataBookView *view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	backend->priv->views = g_slist_append (backend->priv->views, view);

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_book_backend_remove_book_view:
 * @backend: an #EBookBackend
 * @view: an #EDataBookView
 *
 * Removes @view from @backend.
 **/
void
e_book_backend_remove_book_view (EBookBackend *backend,
				 EDataBookView *view)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_mutex_lock (backend->priv->views_mutex);

	backend->priv->views = g_slist_remove (backend->priv->views, view);

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_book_backend_add_client:
 * @backend: An addressbook backend.
 * @book: the corba object representing the client connection.
 *
 * Adds a client to an addressbook backend.
 *
 * Returns: TRUE on success, FALSE on failure to add the client.
 */
gboolean
e_book_backend_add_client (EBookBackend      *backend,
			   EDataBook         *book)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);

	g_mutex_lock (backend->priv->clients_mutex);
	backend->priv->clients = g_slist_prepend (backend->priv->clients, book);
	g_mutex_unlock (backend->priv->clients_mutex);

	return TRUE;
}

/**
 * e_book_backend_remove_client:
 * @backend: an #EBookBackend
 * @book: an #EDataBook to remove
 *
 * Removes @book from the list of @backend's clients.
 **/
void
e_book_backend_remove_client (EBookBackend *backend,
			      EDataBook    *book)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	/* up our backend's refcount here so that last_client_gone
	   doesn't end up unreffing us (while we're holding the
	   lock) */
	g_object_ref (backend);

	/* Disconnect */
	g_mutex_lock (backend->priv->clients_mutex);
	backend->priv->clients = g_slist_remove (backend->priv->clients, book);

	/* When all clients go away, notify the parent factory about it so that
	 * it may decide whether to kill the backend or not.
	 */
	if (!backend->priv->clients)
		last_client_gone (backend);

	g_mutex_unlock (backend->priv->clients_mutex);

	g_object_unref (backend);
}

/**
 * e_book_backend_foreach_view:
 * @backend: an #EBookBackend
 * @callback: callback to call
 * @user_data: user_data passed into the @callback
 *
 * Calls @callback for each known book view of this @backend.
 * @callback returns %FALSE to stop further processing.
 **/
void
e_book_backend_foreach_view (EBookBackend *backend, gboolean (* callback) (EDataBookView *view, gpointer user_data), gpointer user_data)
{
	const GSList *views;
	EDataBookView *view;
	gboolean stop = FALSE;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (callback != NULL);

	g_mutex_lock (backend->priv->views_mutex);

	for (views = backend->priv->views; views && !stop; views = views->next) {
		view = E_DATA_BOOK_VIEW (views->data);

		e_data_book_view_ref (view);
		stop = !callback (view, user_data);
		e_data_book_view_unref (view);
	}

	g_mutex_unlock (backend->priv->views_mutex);
}

/**
 * e_book_backend_get_book_backend_property:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @prop_name: property name to get value of; cannot be NULL
 *
 * Calls the get_backend_property method on the given backend.
 * This might be finished with e_data_book_respond_get_backend_property().
 * Default implementation takes care of common properties and returns
 * an 'unsupported' error for any unknown properties. The subclass may
 * always call this default implementation for properties which fetching
 * it doesn't overwrite.
 *
 * Since: 3.2
 **/
void
e_book_backend_get_backend_property (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->get_backend_property);

	E_BOOK_BACKEND_GET_CLASS (backend)->get_backend_property (backend, book, opid, cancellable, prop_name);
}

/**
 * e_book_backend_set_backend_property:
 * @backend: an #EBookBackend
 * @book: an #EDataBook
 * @opid: the ID to use for this operation
 * @cancellable: a #GCancellable for the operation
 * @prop_name: property name to change; cannot be NULL
 * @prop_value: value to set to @prop_name; cannot be NULL
 *
 * Calls the set_backend_property method on the given backend.
 * This might be finished with e_data_book_respond_set_backend_property().
 * Default implementation simply returns an 'unsupported' error.
 * The subclass may always call this default implementation for properties
 * which fetching it doesn't overwrite.
 *
 * Since: 3.2
 **/
void
e_book_backend_set_backend_property (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (prop_value != NULL);
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->set_backend_property != NULL);

	E_BOOK_BACKEND_GET_CLASS (backend)->set_backend_property (backend, book, opid, cancellable, prop_name, prop_value);
}

/**
 * e_book_backend_is_loaded:
 * @backend: an #EBookBackend
 *
 * Checks if @backend's storage has been opened and the backend
 * itself is ready for accessing.
 *
 * Returns: %TRUE if loaded, %FALSE otherwise.
 **/
gboolean
e_book_backend_is_loaded (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->loaded;
}

/**
 * e_book_backend_set_is_loaded:
 * @backend: an #EBookBackend
 * @is_loaded: A flag indicating whether the backend is loaded
 *
 * Sets the flag indicating whether @backend is loaded to @is_loaded.
 * Meant to be used by backend implementations.
 **/
void
e_book_backend_set_is_loaded (EBookBackend *backend, gboolean is_loaded)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	backend->priv->loaded = is_loaded;
}

/**
 * e_book_backend_is_readonly:
 * @backend: an #EBookBackend
 *
 * Checks if we can write to @backend.
 *
 * Returns: %TRUE if writeable, %FALSE if not.
 **/
gboolean
e_book_backend_is_readonly (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->readonly;
}

/**
 * e_book_backend_set_is_readonly:
 * @backend: an #EBookBackend
 * @is_readonly: A flag indicating whether the backend is readonly
 *
 * Sets the flag indicating whether @backend is readonly to @is_readonly.
 * Meant to be used by backend implementations.
 **/
void
e_book_backend_set_is_readonly (EBookBackend *backend, gboolean is_readonly)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	backend->priv->readonly = is_readonly;
}

/**
 * e_book_backend_is_removed:
 * @backend: an #EBookBackend
 *
 * Checks if @backend has been removed from its physical storage.
 *
 * Returns: %TRUE if @backend has been removed, %FALSE otherwise.
 **/
gboolean
e_book_backend_is_removed (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), FALSE);

	return backend->priv->removed;
}

/**
 * e_book_backend_set_is_removed:
 * @backend: an #EBookBackend
 * @is_removed: A flag indicating whether the backend's storage was removed
 *
 * Sets the flag indicating whether @backend was removed to @is_removed.
 * Meant to be used by backend implementations.
 **/
void
e_book_backend_set_is_removed (EBookBackend *backend, gboolean is_removed)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	backend->priv->removed = is_removed;
}

/**
 * e_book_backend_set_online:
 * @backend: an #EBookbackend
 * @is_online: a mode indicating the online/offline status of the backend
 *
 * Sets @backend's online/offline mode to @is_online.
 **/
void
e_book_backend_set_online (EBookBackend *backend, gboolean is_online)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (E_BOOK_BACKEND_GET_CLASS (backend)->set_online);

	(* E_BOOK_BACKEND_GET_CLASS (backend)->set_online) (backend,  is_online);
}

/**
 * e_book_backend_sync:
 * @backend: an #EBookbackend
 *
 * Write all pending data to disk.  This is only required under special
 * circumstances (for example before a live backup) and should not be used in
 * normal use.
 *
 * Since: 1.12
 */
void
e_book_backend_sync (EBookBackend *backend)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_object_ref (backend);

	if (E_BOOK_BACKEND_GET_CLASS (backend)->sync)
		(* E_BOOK_BACKEND_GET_CLASS (backend)->sync) (backend);

	g_object_unref (backend);
}



static gboolean
view_notify_update (EDataBookView *view, gpointer contact)
{
	e_data_book_view_notify_update (view, contact);

	return TRUE;
}

/**
 * e_book_backend_notify_update:
 * @backend: an #EBookBackend
 * @contact: a new or modified contact
 *
 * Notifies all of @backend's book views about the new or modified
 * contacts @contact.
 *
 * e_data_book_respond_create() and e_data_book_respond_modify() call this
 * function for you. You only need to call this from your backend if
 * contacts are created or modified by another (non-PAS-using) client.
 **/
void
e_book_backend_notify_update (EBookBackend *backend, const EContact *contact)
{
	e_book_backend_foreach_view (backend, view_notify_update, (gpointer) contact);
}

static gboolean
view_notify_remove (EDataBookView *view, gpointer id)
{
	e_data_book_view_notify_remove (view, id);

	return TRUE;
}

/**
 * e_book_backend_notify_remove:
 * @backend: an #EBookBackend
 * @id: a contact id
 *
 * Notifies all of @backend's book views that the contact with UID
 * @id has been removed.
 *
 * e_data_book_respond_remove_contacts() calls this function for you. You
 * only need to call this from your backend if contacts are removed by
 * another (non-PAS-using) client.
 **/
void
e_book_backend_notify_remove (EBookBackend *backend, const gchar *id)
{
	e_book_backend_foreach_view (backend, view_notify_remove, (gpointer) id);
}

static gboolean
view_notify_complete (EDataBookView *view, gpointer unused)
{
	e_data_book_view_notify_complete (view, NULL /* SUCCESS */);

	return TRUE;
}

/**
 * e_book_backend_notify_complete:
 * @backend: an #EBookbackend
 *
 * Notifies all of @backend's book views that the current set of
 * notifications is complete; use this after a series of
 * e_book_backend_notify_update() and e_book_backend_notify_remove() calls.
 **/
void
e_book_backend_notify_complete (EBookBackend *backend)
{
	e_book_backend_foreach_view (backend, view_notify_complete, NULL);
}


/**
 * e_book_backend_notify_error:
 * @backend: an #EBookBackend
 * @message: an error message
 *
 * Notifies each backend listener about an error. This is meant to be used
 * for cases where is no GError return possibility, to notify user about
 * an issue.
 **/
void
e_book_backend_notify_error (EBookBackend *backend, const gchar *message)
{
	EBookBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;

	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_book_report_error (E_DATA_BOOK (clients->data), message);

	g_mutex_unlock (priv->clients_mutex);
}

/**
 * e_book_backend_notify_readonly:
 * @backend: an #EBookBackend
 * @is_readonly: flag indicating readonly status
 *
 * Notifies all backend's clients about the current readonly state.
 **/
void
e_book_backend_notify_readonly (EBookBackend *backend, gboolean is_readonly)
{
	EBookBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;
	priv->readonly = is_readonly;
	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_book_report_readonly (E_DATA_BOOK (clients->data), is_readonly);

	g_mutex_unlock (priv->clients_mutex);

}

/**
 * e_book_backend_notify_online:
 * @backend: an #EBookBackend
 * @is_online: flag indicating whether @backend is connected and online
 *
 * Notifies clients of @backend's connection status indicated by @is_online.
 * Meant to be used by backend implementations.
 **/
void
e_book_backend_notify_online (EBookBackend *backend, gboolean is_online)
{
	EBookBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;
	priv->online = is_online;
	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_book_report_online (E_DATA_BOOK (clients->data), is_online);

	g_mutex_unlock (priv->clients_mutex);
}

/**
 * e_book_backend_notify_auth_required:
 * @backend: an #EBookBackend
 * @credentials: an #ECredentials that contains extra information for
 *    a source for which authentication is requested.
 *    This parameter can be NULL to indicate "for this book".
 *
 * Notifies clients that @backend requires authentication in order to
 * connect. Means to be used by backend implementations.
 **/
void
e_book_backend_notify_auth_required (EBookBackend *backend, const ECredentials *credentials)
{
	EBookBackendPrivate *priv;
	GSList *clients;

	priv = backend->priv;
	g_mutex_lock (priv->clients_mutex);

	for (clients = priv->clients; clients != NULL; clients = g_slist_next (clients))
		e_data_book_report_auth_required (E_DATA_BOOK (clients->data), credentials);
	g_mutex_unlock (priv->clients_mutex);
}
