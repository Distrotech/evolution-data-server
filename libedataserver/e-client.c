/*
 * e-client.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gio/gio.h>

#include "e-gdbus-marshallers.h"
#include "e-operation-pool.h"

#include "e-client.h"

struct _EClientPrivate
{
	GStaticRecMutex prop_mutex;

	ESource *source;
	gchar *uri;
	gboolean online;
	gboolean readonly;
	gboolean opened;
	gboolean capabilities_retrieved;
	GSList *capabilities;

	GStaticRecMutex ops_mutex;
	guint32 last_opid;
	GHashTable *ops; /* opid to GCancellable */
};

enum {
	PROP_0,
	PROP_SOURCE,
	PROP_CAPABILITIES,
	PROP_READONLY,
	PROP_ONLINE,
	PROP_OPENED
};

enum {
	AUTHENTICATE,
	BACKEND_ERROR,
	BACKEND_DIED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];
static EOperationPool *ops_pool = NULL;

G_DEFINE_ABSTRACT_TYPE (EClient, e_client, G_TYPE_OBJECT)

static void client_set_source (EClient *client, ESource *source);
static void client_operation_thread (gpointer data, gpointer user_data);
static void client_handle_authentication (EClient *client, const ECredentials *credentials);

static void
e_client_init (EClient *client)
{
	client->priv = G_TYPE_INSTANCE_GET_PRIVATE (client, E_TYPE_CLIENT, EClientPrivate);

	client->priv->readonly = TRUE;

	g_static_rec_mutex_init (&client->priv->prop_mutex);

	g_static_rec_mutex_init (&client->priv->ops_mutex);
	client->priv->last_opid = 0;
	client->priv->ops = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
}

static void
client_dispose (GObject *object)
{
	EClient *client;

	client = E_CLIENT (object);
	g_return_if_fail (client != NULL);
	g_return_if_fail (client->priv != NULL);

	e_client_cancel_all (client);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_client_parent_class)->dispose (object);
}

static void
client_finalize (GObject *object)
{
	EClient *client;
	EClientPrivate *priv;

	client = E_CLIENT (object);
	g_return_if_fail (client != NULL);
	g_return_if_fail (client->priv != NULL);

	priv = client->priv;

	g_static_rec_mutex_lock (&priv->prop_mutex);

	if (priv->source) {
		g_object_unref (priv->source);
		priv->source = NULL;
	}

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->capabilities) {
		g_slist_foreach (priv->capabilities, (GFunc) g_free, NULL);
		g_slist_free (priv->capabilities);
		priv->capabilities = NULL;
	}

	if (priv->ops) {
		g_hash_table_destroy (priv->ops);
		priv->ops = NULL;
	}

	g_static_rec_mutex_unlock (&priv->prop_mutex);
	g_static_rec_mutex_free (&priv->prop_mutex);
	g_static_rec_mutex_free (&priv->ops_mutex);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_client_parent_class)->finalize (object);
}

static void
client_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			client_set_source (E_CLIENT (object), g_value_get_object (value));
			return;

		case PROP_ONLINE:
			e_client_set_online (E_CLIENT (object), g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
client_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			g_value_set_object (value, e_client_get_source (E_CLIENT (object)));
			return;

		case PROP_CAPABILITIES:
			g_value_set_pointer (value, (gpointer) e_client_get_capabilities (E_CLIENT (object)));
			return;

		case PROP_READONLY:
			g_value_set_boolean (value, e_client_is_readonly (E_CLIENT (object)));
			return;

		case PROP_ONLINE:
			g_value_set_boolean (value, e_client_is_online (E_CLIENT (object)));
			return;

		case PROP_OPENED:
			g_value_set_boolean (value, e_client_is_opened (E_CLIENT (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_client_class_init (EClientClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (EClientPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = client_set_property;
	object_class->get_property = client_get_property;
	object_class->dispose = client_dispose;
	object_class->finalize = client_finalize;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			NULL,
			NULL,
			E_TYPE_SOURCE,
			G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_CAPABILITIES,
		g_param_spec_pointer (
			"capabilities",
			NULL,
			NULL,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_READONLY,
		g_param_spec_boolean (
			"readonly",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_ONLINE,
		g_param_spec_boolean (
			"online",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_OPENED,
		g_param_spec_boolean (
			"opened",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READABLE));

	signals[AUTHENTICATE] = g_signal_new (
		"authenticate",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EClientClass, authenticate),
		NULL, NULL,
		e_gdbus_marshallers_BOOLEAN__POINTER,
		G_TYPE_BOOLEAN, 1,
		G_TYPE_POINTER);

	signals[BACKEND_ERROR] = g_signal_new (
		"backend-error",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (EClientClass, backend_error),
		NULL, NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);

	signals[BACKEND_DIED] = g_signal_new (
		"backend-died",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EClientClass, backend_died),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	if (!ops_pool)
		ops_pool = e_operation_pool_new (2, client_operation_thread, NULL);
}

typedef enum {
	E_CLIENT_OP_AUTHENTICATE
} EClientOp;

typedef struct _EClientOpData {
	EClient *client;
	EClientOp op;

	union {
		ECredentials *credentials;
	} d;
} EClientOpData;

static void
client_operation_thread (gpointer data, gpointer user_data)
{
	EClientOpData *op_data = data;

	g_return_if_fail (op_data != NULL);

	switch (op_data->op) {
	case E_CLIENT_OP_AUTHENTICATE:
		if (e_client_emit_authenticate (op_data->client, op_data->d.credentials))
			client_handle_authentication (op_data->client, op_data->d.credentials);
		e_credentials_free (op_data->d.credentials);
		break;
	}

	g_object_unref (op_data->client);
	g_free (op_data);
}

static void
client_set_source (EClient *client, ESource *source)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv != NULL);
	g_return_if_fail (source != NULL);
	g_return_if_fail (E_IS_SOURCE (source));

	g_object_ref (source);

	if (client->priv->source)
		g_object_unref (client->priv->source);

	client->priv->source = source;
}

/**
 * e_client_get_source:
 * @client: an #EClient
 *
 * Get the #ESource that this client has assigned.
 *
 * Returns: The source.
 *
 * Since: 3.2
 **/
ESource *
e_client_get_source (EClient *client)
{
	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (E_IS_CLIENT (client), NULL);
	g_return_val_if_fail (client->priv != NULL, NULL);

	return client->priv->source;
}

/**
 * e_client_get_uri:
 * @client: an #EClient
 *
 * Get the URI that this client has assigned. This string should not be freed.
 *
 * Returns: The URI.
 *
 * Since: 3.2
 **/
const gchar *
e_client_get_uri (EClient *client)
{
	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (E_IS_CLIENT (client), NULL);
	g_return_val_if_fail (client->priv != NULL, NULL);

	if (!client->priv->uri)
		client->priv->uri = e_source_get_uri (e_client_get_source (client));

	return client->priv->uri;
}

static void
client_ensure_capabilities (EClient *client)
{
	EClientClass *klass;
	gchar *capabilities;

	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv != NULL);

	klass = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (klass != NULL);
	g_return_if_fail (klass->retrieve_capabilities != NULL);

	if (client->priv->capabilities_retrieved || client->priv->capabilities)
		return;

	g_static_rec_mutex_lock (&client->priv->prop_mutex);

	capabilities = klass->retrieve_capabilities (client);

	e_client_set_capabilities (client, capabilities);

	g_free (capabilities);

	client->priv->capabilities_retrieved = TRUE;

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);
}

/**
 * e_client_get_capabilities:
 * @client: an #EClient
 *
 * Get list of strings with capabilities advertised by a backend.
 * This list, together with inner strings, is owned by the @client.
 * To check for individual capabilities use e_client_check_capability().
 *
 * Returns: #GSList of const strings of capabilities
 *
 * Since: 3.2
 **/
const GSList *
e_client_get_capabilities (EClient *client)
{
	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (E_IS_CLIENT (client), NULL);
	g_return_val_if_fail (client->priv != NULL, NULL);

	client_ensure_capabilities (client);

	return client->priv->capabilities;
}

/**
 * e_client_check_capability:
 * @client: an #EClient
 * @capability: a capability
 *
 * Check if backend supports particular capability.
 * To get all capabilities use e_client_get_capabilities().
 *
 * Returns: #GSList of const strings of capabilities
 *
 * Since: 3.2
 **/
gboolean
e_client_check_capability (EClient *client, const gchar *capability)
{
	GSList *iter;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);
	g_return_val_if_fail (capability, FALSE);

	g_static_rec_mutex_lock (&client->priv->prop_mutex);

	client_ensure_capabilities (client);

	for (iter = client->priv->capabilities; iter; iter = g_slist_next (iter)) {
		const gchar *cap = iter->data;

		if (cap && g_ascii_strcasecmp (cap, capability) == 0) {
			g_static_rec_mutex_unlock (&client->priv->prop_mutex);
			return TRUE;
		}
	}

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);

	return FALSE;
}

/* capabilities - comma-separated list of capabilities; can be NULL to unset */
void
e_client_set_capabilities (EClient *client, const gchar *capabilities)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv != NULL);

	g_static_rec_mutex_lock (&client->priv->prop_mutex);

	if (!capabilities)
		client->priv->capabilities_retrieved = FALSE;

	g_slist_foreach (client->priv->capabilities, (GFunc) g_free, NULL);
	g_slist_free (client->priv->capabilities);
	client->priv->capabilities = e_client_util_parse_capabilities (capabilities);

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);

	g_object_notify (G_OBJECT (client), "capabilities");
}

/**
 * e_client_is_readonly:
 * @client: an #EClient
 *
 * Check if this @client is read-only.
 *
 * Returns: %TRUE if this @client is read-only, otherwise %FALSE.
 *
 * Since: 3.2
 **/
gboolean
e_client_is_readonly (EClient *client)
{
	g_return_val_if_fail (client != NULL, TRUE);
	g_return_val_if_fail (E_IS_CLIENT (client), TRUE);
	g_return_val_if_fail (client->priv != NULL, TRUE);

	return client->priv->readonly;
}

void
e_client_set_readonly (EClient *client, gboolean readonly)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv != NULL);

	g_static_rec_mutex_lock (&client->priv->prop_mutex);
	if ((readonly ? 1 : 0) == (client->priv->readonly ? 1 : 0)) {
		g_static_rec_mutex_unlock (&client->priv->prop_mutex);
		return;
	}

	client->priv->readonly = readonly;

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);

	g_object_notify (G_OBJECT (client), "readonly");
}

/**
 * e_client_is_online:
 * @client: an #EClient
 *
 * Check if this @client is connected.
 *
 * Returns: %TRUE if this @client is connected, otherwise %FALSE.
 *
 * Since: 3.2
 **/
gboolean
e_client_is_online (EClient *client)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);

	return client->priv->online;
}

void
e_client_set_online (EClient *client, gboolean is_online)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv != NULL);

	/* newly connected/disconnected => make sure capabilities will be correct */
	e_client_set_capabilities (client, NULL);

	g_static_rec_mutex_lock (&client->priv->prop_mutex);
	if ((is_online ? 1: 0) == (client->priv->online ? 1 : 0)) {
		g_static_rec_mutex_unlock (&client->priv->prop_mutex);
		return;
	}

	client->priv->online = is_online;

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);

	g_object_notify (G_OBJECT (client), "online");
}

/**
 * e_client_is_opened:
 * @client: an #EClient
 *
 * Check if this @client has been opened.
 *
 * Returns: %TRUE if this @client has been opened, otherwise %FALSE.
 *
 * Since: 3.2.
 **/
gboolean
e_client_is_opened (EClient *client)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);

	return client->priv->opened;
}

/**
 * e_client_cancel_op:
 * @client: an #EClient
 * @opid: asynchronous operation ID
 *
 * Cancels particular asynchronous operation. The @opid is returned from
 * an asynchronous function, like e_client_open(). The function does nothing
 * if the asynchronous operation doesn't exist any more.
 *
 * Since: 3.2
 **/
void
e_client_cancel_op (EClient *client, guint32 opid)
{
	GCancellable *cancellable;

	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv != NULL);
	g_return_if_fail (client->priv->ops != NULL);

	g_static_rec_mutex_lock (&client->priv->ops_mutex);

	cancellable = g_hash_table_lookup (client->priv->ops, GINT_TO_POINTER (opid));
	if (cancellable)
		g_cancellable_cancel (cancellable);

	g_static_rec_mutex_unlock (&client->priv->ops_mutex);
}

static void
gather_opids_cb (gpointer opid, gpointer cancellable, gpointer ids_list)
{
	GSList **ids = ids_list;

	g_return_if_fail (ids_list != NULL);

	*ids = g_slist_prepend (*ids, opid);
}

static void
cancel_op_cb (gpointer opid, gpointer client)
{
	e_client_cancel_op (client, GPOINTER_TO_INT (opid));
}

/**
 * e_client_cancel_all:
 * @client: an #EClient
 *
 * Cancels all pending operations started on @client.
 *
 * Since: 3.2
 **/
void
e_client_cancel_all (EClient *client)
{
	GSList *opids = NULL;

	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv != NULL);
	g_return_if_fail (client->priv->ops != NULL);

	g_static_rec_mutex_lock (&client->priv->ops_mutex);

	g_hash_table_foreach (client->priv->ops, gather_opids_cb, &opids);

	g_slist_foreach (opids, cancel_op_cb, client);
	g_slist_free (opids);

	g_static_rec_mutex_unlock (&client->priv->ops_mutex);
}

guint32
e_client_register_op (EClient *client, GCancellable *cancellable)
{
	guint32 opid;

	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (E_IS_CLIENT (client), 0);
	g_return_val_if_fail (client->priv != NULL, 0);
	g_return_val_if_fail (client->priv->ops != NULL, 0);
	g_return_val_if_fail (cancellable != NULL, 0);

	g_static_rec_mutex_lock (&client->priv->ops_mutex);

	client->priv->last_opid++;
	if (!client->priv->last_opid)
		client->priv->last_opid++;

	while (g_hash_table_lookup (client->priv->ops, GINT_TO_POINTER (client->priv->last_opid)))
		client->priv->last_opid++;

	g_return_val_if_fail (client->priv->last_opid != 0, 0);

	opid = client->priv->last_opid;
	g_hash_table_insert (client->priv->ops, GINT_TO_POINTER (opid), g_object_ref (cancellable));

	g_static_rec_mutex_unlock (&client->priv->ops_mutex);

	return opid;
}

void
e_client_unregister_op (EClient *client, guint32 opid)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv != NULL);
	g_return_if_fail (client->priv->ops != NULL);

	g_static_rec_mutex_lock (&client->priv->ops_mutex);
	g_hash_table_remove (client->priv->ops, GINT_TO_POINTER (opid));
	g_static_rec_mutex_unlock (&client->priv->ops_mutex);
}

static void
client_handle_authentication (EClient *client, const ECredentials *credentials)
{
	EClientClass *klass;

	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (credentials != NULL);

	klass = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (klass != NULL);
	g_return_if_fail (klass->handle_authentication != NULL);

	return klass->handle_authentication (client, credentials);
}

/* Processes authentication request in a new thread. Usual steps are:
   a) backend sends an auth-required signal
   b) EClient implementation calls this function
   c) a new thread is run which emits authenticate signal by e_client_emit_authenticate()
   d) if anyone responds (returns true), the EClient::handle_authentication
      is called from the same extra thread with new credentials
   e) EClient implementation passes results to backend in the EClient::handle_authentication
*/
void
e_client_process_authentication (EClient *client, const ECredentials *credentials)
{
	EClientOpData *op_data;

	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));

	op_data = g_new0 (EClientOpData, 1);
	op_data->client = g_object_ref (client);
	op_data->op = E_CLIENT_OP_AUTHENTICATE;
	op_data->d.credentials = credentials ? e_credentials_new_clone (credentials) : e_credentials_new ();

	e_operation_pool_push (ops_pool, op_data);
}

gboolean
e_client_emit_authenticate (EClient *client, ECredentials *credentials)
{
	gboolean handled = FALSE;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (credentials != NULL, FALSE);

	g_signal_emit (client, signals[AUTHENTICATE], 0, credentials, &handled);

	return handled;
}

void
e_client_emit_backend_error (EClient *client, const gchar *error_msg)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (error_msg != NULL);

	g_signal_emit (client, signals[BACKEND_ERROR], 0, error_msg);
}

void
e_client_emit_backend_died (EClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));

	g_signal_emit (client, signals[BACKEND_DIED], 0);
}

/**
 * e_client_open:
 * @client: an #EClient
 * @only_if_exists: if %TRUE, fail if this book doesn't already exist, otherwise create it first
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Opens the @client, making it ready for queries and other operations.
 * The call is finished by e_client_open_finish() from the @callback.
 *
 * Returns: Asynchronous operation ID, which can be passed to e_client_cancel_op(),
 * or zero on a failure.
 *
 * Since: 3.2
 **/
guint32
e_client_open (EClient *client, gboolean only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	EClientClass *klass;

	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (E_IS_CLIENT (client), 0);
	g_return_val_if_fail (client->priv != NULL, 0);
	g_return_val_if_fail (callback != NULL, 0);

	klass = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (klass != NULL, 0);
	g_return_val_if_fail (klass->open != NULL, 0);

	return klass->open (client, only_if_exists, cancellable, callback, user_data);
}

/**
 * e_client_open_finish:
 * @client: an #EClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_client_open().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_open_finish (EClient *client, GAsyncResult *result, GError **error)
{
	EClientClass *klass;
	gboolean res;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);

	klass = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->open_finish != NULL, FALSE);

	res = klass->open_finish (client, result, error);

	client->priv->opened = res;

	return res;
}

/**
 * e_client_open_sync:
 * @client: an #EClient
 * @only_if_exists: if %TRUE, fail if this book doesn't already exist, otherwise create it first
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Opens the @client, making it ready for queries and other operations.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_open_sync (EClient *client, gboolean only_if_exists, GCancellable *cancellable, GError **error)
{
	EClientClass *klass;
	gboolean res;

	klass = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->open_sync != NULL, FALSE);

	res = klass->open_sync (client, only_if_exists, cancellable, error);

	client->priv->opened = res;

	return res;
}

/**
 * e_client_remove:
 * @client: an #EClient
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Removes the backing data for this #EClient. For example, with the file backend this
 * deletes the database file. You cannot get it back!
 * The call is finished by e_client_remove_finish() from the @callback.
 *
 * Returns: Asynchronous operation ID, which can be passed to e_client_cancel_op(),
 * or zero on a failure.
 *
 * Since: 3.2
 **/
guint32
e_client_remove (EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	EClientClass *klass;

	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (E_IS_CLIENT (client), 0);
	g_return_val_if_fail (client->priv != NULL, 0);
	g_return_val_if_fail (callback != NULL, 0);

	klass = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (klass != NULL, 0);
	g_return_val_if_fail (klass->remove != NULL, 0);

	return klass->remove (client, cancellable, callback, user_data);
}

/**
 * e_client_remove_finish:
 * @client: an #EClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_client_remove().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_remove_finish (EClient *client, GAsyncResult *result, GError **error)
{
	EClientClass *klass;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv != NULL, FALSE);

	klass = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->remove_finish != NULL, FALSE);

	return klass->remove_finish (client, result, error);
}

/**
 * e_client_remove_sync:
 * @client: an #EClient
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Removes the backing data for this #EClient. For example, with the file backend this
 * deletes the database file. You cannot get it back!
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_remove_sync (EClient *client, GCancellable *cancellable, GError **error)
{
	EClientClass *klass;

	klass = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->remove_sync != NULL, FALSE);

	return klass->remove_sync (client, cancellable, error);
}

/**
 * e_client_util_slist_to_strv:
 * @strings: a #GSList of strings (const gchar *)
 *
 * Convert list of strings into NULL-terminates array of strings.
 *
 * Returns: Newly allocated NULL-terminated array of strings.
 * Returned pointer should be freed with g_strfreev().
 *
 * Note: Pair function for this is e_client_util_strv_to_slist().
 *
 * Sice: 3.2
 **/
gchar **
e_client_util_slist_to_strv (const GSList *strings)
{
	const GSList *iter;
	GPtrArray *array;

	array = g_ptr_array_sized_new (g_slist_length ((GSList *) strings) + 1);

	for (iter = strings; iter; iter = iter->next) {
		const gchar *str = iter->data;

		if (str)
			g_ptr_array_add (array, g_strdup (str));
	}

	/* NULL-terminated */
	g_ptr_array_add (array, NULL);

	return (gchar **) g_ptr_array_free (array, FALSE);
}

/**
 * e_client_util_strv_to_slist:
 * @strv: a NULL-terminated array of strings (const gchar *)
 *
 * Convert NULL-terminated array of strings to a list of strings.
 *
 * Returns: Newly allocated #GSList of newly allocated strings.
 * Returned pointer should be freed with e_client_util_free_string_slist().
 *
 * Note: Pair function for this is e_client_util_slist_to_strv().
 *
 * Sice: 3.2
 **/
GSList *
e_client_util_strv_to_slist (const gchar * const *strv)
{
	GSList *slist = NULL;
	gint ii;

	if (!strv)
		return NULL;

	for (ii = 0; strv[ii]; ii++) {
		slist = g_slist_prepend (slist, g_strdup (strv[ii]));
	}

	return g_slist_reverse (slist);
}

/**
 * e_client_util_copy_string_slist:
 * @copy_to: Where to copy; can be NULL
 * @strings: GSList of strings to be copied
 *
 * Copies GSList of strings at the end of @copy_to.
 *
 * Returns: New head of @copy_to.
 * Returned pointer can be freed with e_client_util_free_string_slist().
 *
 * Since: 3.2
 **/
GSList *
e_client_util_copy_string_slist	(GSList *copy_to, const GSList *strings)
{
	GSList *res = copy_to;
	const GSList *iter;

	for (iter = strings; iter; iter = iter->next) {
		res = g_slist_append (res, g_strdup (iter->data));
	}

	return res;
}

/**
 * e_client_util_copy_object_slist:
 * @copy_to: Where to copy; can be NULL
 * @objects: GSList of GObject-s to be copied
 *
 * Copies GSList of GObject-s at the end of @copy_to.
 *
 * Returns: New head of @copy_to.
 * Returned pointer can be freed with e_client_util_free_object_slist().
 *
 * Since: 3.2
 **/
GSList *
e_client_util_copy_object_slist	(GSList *copy_to, const GSList *objects)
{
	GSList *res = copy_to;
	const GSList *iter;

	for (iter = objects; iter; iter = iter->next) {
		res = g_slist_append (res, g_object_ref (iter->data));
	}

	return res;
}

/**
 * e_client_util_free_string_slist:
 * @strings: a #GSList of strings (gchar *)
 *
 * Frees memory previously allocated by e_client_util_strv_to_slist().
 *
 * Sice: 3.2
 **/
void
e_client_util_free_string_slist (GSList *strings)
{
	if (!strings)
		return;

	g_slist_foreach (strings, (GFunc) g_free, NULL);
	g_slist_free (strings);
}

/**
 * e_client_util_free_object_slist:
 * @objects: a #GSList of #GObject-s
 *
 * Calls g_object_unref() on each member of @objects and then frees
 * also @objects itself.
 *
 * Sice: 3.2
 **/
void
e_client_util_free_object_slist (GSList *objects)
{
	if (!objects)
		return;

	g_slist_foreach (objects, (GFunc) g_object_unref, NULL);
	g_slist_free (objects);
}

/**
 * e_client_util_parse_capabilities:
 * @capabilitites: string of capabilities
 *
 * Parse comma-separated list of capabilities into #GSList.
 *
 * Reeturns: Newly allocated #GSList of newly allocated strings
 * corresponding to capabilities as parsed from @capabilities.
 * Free returned pointer with e_client_util_free_string_slist().
 *
 * Since: 3.2
 **/
GSList *
e_client_util_parse_capabilities (const gchar *capabilities)
{
	GSList *caps_slist = NULL;
	gchar **caps_strv = NULL;
	gint ii;

	if (!capabilities || !*capabilities)
		return NULL;

	caps_strv = g_strsplit (capabilities, ",", -1);
	g_return_val_if_fail (caps_strv != NULL, NULL);

	for (ii = 0; caps_strv && caps_strv[ii]; ii++) {
		gchar *cap = g_strstrip (caps_strv[ii]);

		if (cap && *cap)
			caps_slist = g_slist_prepend (caps_slist, g_strdup (cap));
	}

	g_strfreev (caps_strv);

	return g_slist_reverse (caps_slist);
}

/* for each known source calls check_func, which should return TRUE if the required
   source have been found. Function returns NULL or the source on which was returned
   TRUE by the check_func. Non-NULL pointer should be unreffed by g_object_unref. */
static ESource *
search_known_sources (ESourceList *sources, gboolean (*check_func)(ESource *source, gpointer user_data), gpointer user_data)
{
	ESource *res = NULL;
	GSList *g;

	g_return_val_if_fail (check_func != NULL, NULL);
	g_return_val_if_fail (sources != NULL, NULL);

	for (g = e_source_list_peek_groups (sources); g; g = g->next) {
		ESourceGroup *group = E_SOURCE_GROUP (g->data);
		GSList *s;

		for (s = e_source_group_peek_sources (group); s; s = s->next) {
			ESource *source = E_SOURCE (s->data);

			if (check_func (source, user_data)) {
				res = g_object_ref (source);
				break;
			}
		}

		if (res)
			break;
	}

	return res;
}

static gboolean
check_uri (ESource *source, gpointer uri)
{
	const gchar *suri;

	g_return_val_if_fail (source != NULL, FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	suri = e_source_peek_absolute_uri (source);

	if (suri && g_ascii_strcasecmp (suri, uri) == 0)
		return TRUE;

	if (!suri && e_source_peek_group (source)) {
		gboolean res = FALSE;
		gchar *my_uri = g_strconcat (
			e_source_group_peek_base_uri (e_source_peek_group (source)),
			e_source_peek_relative_uri (source),
			NULL);

		res = my_uri && g_ascii_strcasecmp (my_uri, uri) == 0;

		g_free (my_uri);

		return res;
	}

	return FALSE;
}

struct check_system_data
{
	const gchar *uri;
	ESource *uri_source;
};

static gboolean
check_system (ESource *source, gpointer data)
{
	struct check_system_data *csd = data;

	g_return_val_if_fail (source != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	if (e_source_get_property (source, "system")) {
		return TRUE;
	}

	if (check_uri (source, (gpointer) csd->uri)) {
		if (csd->uri_source)
			g_object_unref (csd->uri_source);
		csd->uri_source = g_object_ref (source);
	}

	return FALSE;
}

ESource *
e_client_util_get_system_source (ESourceList *source_list)
{
	struct check_system_data csd;
	ESource *system_source = NULL;

	g_return_val_if_fail (source_list != NULL, NULL);
	g_return_val_if_fail (E_IS_SOURCE_LIST (source_list), NULL);

	csd.uri = "local:system";
	csd.uri_source = NULL;

	system_source = search_known_sources (source_list, check_system, &csd);

	if (!system_source) {
		system_source = csd.uri_source;
		csd.uri_source = NULL;
	}

	if (csd.uri_source)
		g_object_unref (csd.uri_source);

	return system_source;
}

gboolean
e_client_util_set_default (ESourceList *source_list, ESource *source)
{
	const gchar *uid;
	GSList *g;

	g_return_val_if_fail (source_list != NULL, FALSE);
	g_return_val_if_fail (E_IS_SOURCE_LIST (source_list), FALSE);
	g_return_val_if_fail (source != NULL, FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	uid = e_source_peek_uid (source);

	/* make sure the source is actually in the ESourceList.  If
	   it's not we don't bother adding it, just return an error */
	source = e_source_list_peek_source_by_uid (source_list, uid);
	if (!source)
		return FALSE;

	/* loop over all the sources clearing out any "default"
	   properties we find */
	for (g = e_source_list_peek_groups (source_list); g; g = g->next) {
		GSList *s;
		for (s = e_source_group_peek_sources (E_SOURCE_GROUP (g->data));
		     s; s = s->next) {
			e_source_set_property (E_SOURCE (s->data), "default", NULL);
		}
	}

	/* set the "default" property on the source */
	e_source_set_property (source, "default", "true");

	return TRUE;
}

ESource *
e_client_util_get_source_for_uri (ESourceList *source_list, const gchar *uri)
{
	ESource *source;

	g_return_val_if_fail (source_list != NULL, NULL);
	g_return_val_if_fail (E_IS_SOURCE_LIST (source_list), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	source = search_known_sources (source_list, check_uri, (gpointer) uri);
	if (!source)
		source = e_source_new_with_absolute_uri ("", uri);

	return source;
}

GDBusProxy *
e_client_get_dbus_proxy (EClient *client)
{
	EClientClass *klass;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (E_IS_CLIENT (client), NULL);

	klass = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (klass != NULL, NULL);
	g_return_val_if_fail (klass->get_dbus_proxy != NULL, NULL);

	return klass->get_dbus_proxy (client);
}

/**
 * Unwraps D-Bus error to local error. dbus_error is automatically freed.
 * dbus_erorr and out_error can point to the same variable.
 **/
void
e_client_unwrap_dbus_error (EClient *client, GError *dbus_error, GError **out_error)
{
	EClientClass *klass;

	g_return_if_fail (client != NULL);
	g_return_if_fail (E_IS_CLIENT (client));

	klass = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (klass != NULL);
	g_return_if_fail (klass->unwrap_dbus_error != NULL);

	if (!dbus_error || !out_error) {
		if (dbus_error)
			g_error_free (dbus_error);
	} else {
		klass->unwrap_dbus_error (client, dbus_error, out_error);
	}
}

typedef struct _EClientAsyncOpData
{
	EClient *client;
	guint32 opid;

	gpointer source_tag;
	GAsyncReadyCallback callback;
	gpointer user_data;

	gboolean result; /* result of the finish function call */

	/* only one can be non-NULL, and the type is telling which 'out' value is valid */
	EClientProxyFinishVoidFunc finish_void;
	EClientProxyFinishBooleanFunc finish_boolean;
	EClientProxyFinishStringFunc finish_string;
	EClientProxyFinishStrvFunc finish_strv;
	EClientProxyFinishUintFunc finish_uint;

	union {
		gboolean val_boolean;
		gchar *val_string;
		gchar **val_strv;
		guint val_uint;
	} out;
} EClientAsyncOpData;

static void
async_data_free (EClientAsyncOpData *async_data)
{
	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->client != NULL);

	e_client_unregister_op (async_data->client, async_data->opid);

	if (async_data->finish_string)
		g_free (async_data->out.val_string);
	else if (async_data->finish_strv)
		g_strfreev (async_data->out.val_strv);

	g_object_unref (async_data->client);
	g_free (async_data);
}

static gboolean
complete_async_op_in_idle_cb (gpointer user_data)
{
	GSimpleAsyncResult *simple = user_data;
	gint run_main_depth;

	g_return_val_if_fail (simple != NULL, FALSE);

	run_main_depth = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (simple), "run-main-depth"));
	if (run_main_depth < 1)
		run_main_depth = 1;

	/* do not receive in higher level than was initially run */
	if (g_main_depth () > run_main_depth) {
		return TRUE;
	}

	g_simple_async_result_complete (simple);
	g_object_unref (simple);

	return FALSE;
}

static void
finish_async_op (EClientAsyncOpData *async_data, const GError *error, gboolean in_idle)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->source_tag != NULL);
	g_return_if_fail (async_data->client != NULL);

	simple = g_simple_async_result_new (G_OBJECT (async_data->client), async_data->callback, async_data->user_data, async_data->source_tag);
	g_simple_async_result_set_op_res_gpointer (simple, async_data, (GDestroyNotify) async_data_free);

	if (error != NULL)
		g_simple_async_result_set_from_error (simple, error);

	if (in_idle) {
		g_object_set_data (G_OBJECT (simple), "run-main-depth", GINT_TO_POINTER (g_main_depth ()));
		g_idle_add (complete_async_op_in_idle_cb, simple);
	} else {
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
	}
}

static void
async_result_ready_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;
	EClientAsyncOpData *async_data;
	EClient *client;

	g_return_if_fail (result != NULL);
	g_return_if_fail (source_object != NULL);

	async_data = user_data;
	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->client != NULL);

	client = async_data->client;
	g_return_if_fail (e_client_get_dbus_proxy (client) == G_DBUS_PROXY (source_object));

	if (async_data->finish_void)
		async_data->result = async_data->finish_void (G_DBUS_PROXY (source_object), result, &error);
	else if (async_data->finish_boolean)
		async_data->result = async_data->finish_boolean (G_DBUS_PROXY (source_object), result, &async_data->out.val_boolean, &error);
	else if (async_data->finish_string)
		async_data->result = async_data->finish_string (G_DBUS_PROXY (source_object), result, &async_data->out.val_string, &error);
	else if (async_data->finish_strv)
		async_data->result = async_data->finish_strv (G_DBUS_PROXY (source_object), result, &async_data->out.val_strv, &error);
	else if (async_data->finish_uint)
		async_data->result = async_data->finish_uint (G_DBUS_PROXY (source_object), result, &async_data->out.val_uint, &error);
	else
		g_warning ("%s: Do not know how to finish async operation", G_STRFUNC);

	finish_async_op (async_data, error, FALSE);

	if (error != NULL)
		g_error_free (error);
}

static EClientAsyncOpData *
prepare_async_data (EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint, GDBusProxy **proxy, guint32 *opid, GCancellable **out_cancellable)
{
	EClientAsyncOpData *async_data;
	GCancellable *use_cancellable;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (callback != NULL, NULL);
	g_return_val_if_fail (source_tag != NULL, NULL);
	g_return_val_if_fail (proxy != NULL, NULL);
	g_return_val_if_fail (opid != NULL, NULL);
	g_return_val_if_fail (out_cancellable != NULL, NULL);
	g_return_val_if_fail (finish_void || finish_boolean || finish_string || finish_strv || finish_uint, NULL);

	if (finish_void) {
		g_return_val_if_fail (finish_boolean == NULL, NULL);
		g_return_val_if_fail (finish_string == NULL, NULL);
		g_return_val_if_fail (finish_strv == NULL, NULL);
		g_return_val_if_fail (finish_uint == NULL, NULL);
	}

	if (finish_boolean) {
		g_return_val_if_fail (finish_void == NULL, NULL);
		g_return_val_if_fail (finish_string == NULL, NULL);
		g_return_val_if_fail (finish_strv == NULL, NULL);
		g_return_val_if_fail (finish_uint == NULL, NULL);
	}

	if (finish_string) {
		g_return_val_if_fail (finish_void == NULL, NULL);
		g_return_val_if_fail (finish_boolean == NULL, NULL);
		g_return_val_if_fail (finish_strv == NULL, NULL);
		g_return_val_if_fail (finish_uint == NULL, NULL);
	}

	if (finish_strv) {
		g_return_val_if_fail (finish_void == NULL, NULL);
		g_return_val_if_fail (finish_boolean == NULL, NULL);
		g_return_val_if_fail (finish_string == NULL, NULL);
		g_return_val_if_fail (finish_uint == NULL, NULL);
	}

	if (finish_uint) {
		g_return_val_if_fail (finish_void == NULL, NULL);
		g_return_val_if_fail (finish_boolean == NULL, NULL);
		g_return_val_if_fail (finish_string == NULL, NULL);
		g_return_val_if_fail (finish_strv == NULL, NULL);
	}

	*proxy = e_client_get_dbus_proxy (client);
	if (!*proxy)
		return NULL;

	use_cancellable = cancellable;
	if (!use_cancellable)
		use_cancellable = g_cancellable_new ();

	*opid = e_client_register_op (client, use_cancellable);
	async_data = g_new0 (EClientAsyncOpData, 1);
	async_data->client = g_object_ref (client);
	async_data->opid = *opid;
	async_data->source_tag = source_tag;
	async_data->callback = callback;
	async_data->user_data = user_data;
	async_data->finish_void = finish_void;
	async_data->finish_boolean = finish_boolean;
	async_data->finish_string = finish_string;
	async_data->finish_strv = finish_strv;
	async_data->finish_uint = finish_uint;

	/* EClient from e_client_register_op() took ownership of the use_cancellable */
	if (use_cancellable != cancellable)
		g_object_unref (use_cancellable);

	*out_cancellable = use_cancellable;

	return async_data;
}

guint32
e_client_proxy_call_void (EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	guint32 opid = 0;
	GDBusProxy *proxy = NULL;

	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (E_IS_CLIENT (client), 0);
	g_return_val_if_fail (callback != NULL, 0);
	g_return_val_if_fail (source_tag != NULL, 0);
	g_return_val_if_fail (func != NULL, 0);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &opid, &cancellable);
	g_return_val_if_fail (async_data != NULL, 0);

	func (proxy, cancellable, async_result_ready_cb, async_data);

	return opid;
}

guint32
e_client_proxy_call_boolean (EClient *client, gboolean in_boolean, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	guint32 opid = 0;
	GDBusProxy *proxy = NULL;

	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (E_IS_CLIENT (client), 0);
	g_return_val_if_fail (callback != NULL, 0);
	g_return_val_if_fail (source_tag != NULL, 0);
	g_return_val_if_fail (func != NULL, 0);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &opid, &cancellable);
	g_return_val_if_fail (async_data != NULL, 0);

	func (proxy, in_boolean, cancellable, async_result_ready_cb, async_data);

	return opid;
}

guint32
e_client_proxy_call_string (EClient *client, const gchar *in_string, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, const gchar * in_string, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	guint32 opid = 0;
	GDBusProxy *proxy = NULL;

	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (E_IS_CLIENT (client), 0);
	g_return_val_if_fail (callback != NULL, 0);
	g_return_val_if_fail (source_tag != NULL, 0);
	g_return_val_if_fail (func != NULL, 0);
	g_return_val_if_fail (in_string != NULL, 0);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &opid, &cancellable);
	g_return_val_if_fail (async_data != NULL, 0);

	func (proxy, in_string, cancellable, async_result_ready_cb, async_data);

	return opid;
}

guint32
e_client_proxy_call_strv (EClient *client, const gchar * const *in_strv, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, const gchar * const * in_strv, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	guint32 opid = 0;
	GDBusProxy *proxy = NULL;

	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (E_IS_CLIENT (client), 0);
	g_return_val_if_fail (callback != NULL, 0);
	g_return_val_if_fail (source_tag != NULL, 0);
	g_return_val_if_fail (func != NULL, 0);
	g_return_val_if_fail (in_strv != NULL, 0);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &opid, &cancellable);
	g_return_val_if_fail (async_data != NULL, 0);

	func (proxy, in_strv, cancellable, async_result_ready_cb, async_data);

	return opid;
}

guint32
e_client_proxy_call_uint (EClient *client, guint in_uint, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	guint32 opid = 0;
	GDBusProxy *proxy = NULL;

	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (E_IS_CLIENT (client), 0);
	g_return_val_if_fail (callback != NULL, 0);
	g_return_val_if_fail (source_tag != NULL, 0);
	g_return_val_if_fail (func != NULL, 0);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &opid, &cancellable);
	g_return_val_if_fail (async_data != NULL, 0);

	func (proxy, in_uint, cancellable, async_result_ready_cb, async_data);

	return opid;
}

gboolean
e_client_proxy_call_finish_void (EClient *client, GAsyncResult *result, GError **error, gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	return async_data->result;
}

gboolean
e_client_proxy_call_finish_boolean (EClient *client, GAsyncResult *result, gboolean *out_boolean, GError **error, gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (out_boolean != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	*out_boolean = async_data->out.val_boolean;

	return async_data->result;
}

gboolean
e_client_proxy_call_finish_string (EClient *client, GAsyncResult *result, gchar **out_string, GError **error, gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (out_string != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	*out_string = async_data->out.val_string;
	async_data->out.val_string = NULL;

	return async_data->result;
}

gboolean
e_client_proxy_call_finish_strv (EClient *client, GAsyncResult *result, gchar ***out_strv, GError **error, gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (out_strv != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	*out_strv = async_data->out.val_strv;
	async_data->out.val_strv = NULL;

	return async_data->result;
}

gboolean
e_client_proxy_call_finish_uint (EClient *client, GAsyncResult *result, guint *out_uint, GError **error, gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (out_uint != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	*out_uint = async_data->out.val_uint;

	return async_data->result;
}

#define SYNC_CALL_TEMPLATE(_out_test,_the_call)			\
	GDBusProxy *proxy;					\
	GCancellable *use_cancellable;				\
	guint32 opid;						\
	gboolean result;					\
	GError *local_error = NULL;				\
								\
	g_return_val_if_fail (client != NULL, FALSE);		\
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);	\
	g_return_val_if_fail (func != NULL, FALSE);		\
	g_return_val_if_fail (_out_test != NULL, FALSE);	\
								\
	proxy = e_client_get_dbus_proxy (client);		\
	g_return_val_if_fail (proxy != NULL, FALSE);		\
								\
	use_cancellable = cancellable;				\
	if (!use_cancellable)					\
		use_cancellable = g_cancellable_new ();		\
								\
	g_object_ref (client);					\
	opid = e_client_register_op (client, use_cancellable);	\
								\
	result = func _the_call;				\
								\
	e_client_unregister_op (client, opid);			\
	g_object_unref (client);				\
								\
	if (use_cancellable != cancellable)			\
		g_object_unref (use_cancellable);		\
								\
	e_client_unwrap_dbus_error (client, local_error, error);\
								\
	return result;

gboolean
e_client_proxy_call_sync_void__void (EClient *client, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_void__boolean (EClient *client, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean *out_boolean, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_void__string (EClient *client, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gchar **out_string, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_void__strv (EClient *client, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gchar ***out_strv, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_void__uint (EClient *client, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint *out_uint, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, out_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__void (EClient *client, gboolean in_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, in_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__boolean (EClient *client, gboolean in_boolean, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, gboolean *out_boolean, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, in_boolean, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__string (EClient *client, gboolean in_boolean, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, gchar **out_string, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, in_boolean, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__strv (EClient *client, gboolean in_boolean, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, gchar ***out_strv, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, in_boolean, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__uint (EClient *client, gboolean in_boolean, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, guint *out_uint, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, in_boolean, out_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__void (EClient *client, const gchar *in_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, in_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__boolean (EClient *client, const gchar *in_string, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, gboolean *out_boolean, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, in_string, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__string (EClient *client, const gchar *in_string, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, gchar **out_string, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, in_string, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__strv (EClient *client, const gchar *in_string, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, gchar ***out_strv, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, in_string, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__uint (EClient *client, const gchar *in_string, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, guint *out_uint, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, in_string, out_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__void (EClient *client, const gchar * const *in_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, in_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__boolean (EClient *client, const gchar * const *in_strv, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, gboolean *out_boolean, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, in_strv, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__string (EClient *client, const gchar * const *in_strv, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, gchar **out_string, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, in_strv, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__strv (EClient *client, const gchar * const *in_strv, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, gchar ***out_strv, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, in_strv, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__uint (EClient *client, const gchar * const *in_strv, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, guint *out_uint, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, in_strv, out_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__void (EClient *client, guint in_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, in_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__boolean (EClient *client, guint in_uint, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, gboolean *out_boolean, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, in_uint, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__string (EClient *client, guint in_uint, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, gchar **out_string, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, in_uint, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__strv (EClient *client, guint in_uint, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, gchar ***out_strv, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, in_uint, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__uint (EClient *client, guint in_uint, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, guint *out_uint, GCancellable *cancellable, GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, in_uint, out_uint, use_cancellable, &local_error))
}

#undef SYNC_CALL_TEMPLATE
