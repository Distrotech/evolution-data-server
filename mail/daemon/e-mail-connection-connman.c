/* e-mail-connection-connman.c */

#include "e-mail-connection-connman.h"
#include <gio/gio.h>
#include "mail-session.h"

#define CONNMAN_DBUS_SERVICE   "net.connman"
#define CONNMAN_DBUS_INTERFACE "net.connman.Manager"
#define CONNMAN_DBUS_PATH      "/"

G_DEFINE_TYPE (EMailConnectionConnMan, e_mail_connection_connman, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_MAIL_TYPE_CONNECTION_CONNMAN, EMailConnectionConnManPrivate))

typedef struct _EMailConnectionConnManPrivate EMailConnectionConnManPrivate;

struct _EMailConnectionConnManPrivate {
	GDBusConnection *connection;
};

static gboolean connman_connect (EMailConnectionConnMan *manager);

static void
manager_set_state (EMailConnectionConnMan *manager, const gchar *state)
{
	/* EMailConnectionConnManPrivate *priv = GET_PRIVATE(manager); */

	camel_session_set_network_available (session, !g_strcmp0 (state, "online"));
	camel_session_set_online (session, !g_strcmp0 (state, "online"));
}

static void
connman_connection_closed_cb (GDBusConnection *pconnection, gboolean remote_peer_vanished, GError *error, gpointer user_data)
{
	EMailConnectionConnMan *manager = user_data;
	EMailConnectionConnManPrivate *priv = GET_PRIVATE(manager);

	g_object_unref (priv->connection);
	priv->connection = NULL;

	g_timeout_add_seconds (
		3, (GSourceFunc) connman_connect, manager);
}

static void
connman_signal_cb (GDBusConnection *connection,
	const gchar *sender_name,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *signal_name,
	GVariant *parameters,
	gpointer user_data)
{
	EMailConnectionConnMan *manager = user_data;
	gchar *state = NULL;

	if (g_strcmp0 (interface_name, CONNMAN_DBUS_INTERFACE) != 0
	    || g_strcmp0 (object_path, CONNMAN_DBUS_PATH) != 0
	    || g_strcmp0 (signal_name, "StateChanged") != 0)
		return;

	g_variant_get (parameters, "(s)", &state);
	manager_set_state (manager, state);
	g_free (state);
}

static void
connman_check_initial_state (EMailConnectionConnMan *manager)
{
	GDBusMessage *message = NULL;
	GDBusMessage *response = NULL;
	GError *error = NULL;
	EMailConnectionConnManPrivate *priv = GET_PRIVATE(manager);

	message = g_dbus_message_new_method_call (
		CONNMAN_DBUS_SERVICE, CONNMAN_DBUS_PATH, CONNMAN_DBUS_INTERFACE, "GetState");

	/* XXX Assuming this should be safe to call synchronously. */
	response = g_dbus_connection_send_message_with_reply_sync (
		priv->connection, message, G_DBUS_SEND_MESSAGE_FLAGS_NONE, 100, NULL, NULL, &error);

	if (response != NULL) {
		gchar *state = NULL;
		GVariant *body = g_dbus_message_get_body (response);

		g_variant_get (body, "(s)", &state);
		manager_set_state (manager, state);
		g_free (state);
	} else {
		g_warning ("%s: %s", G_STRFUNC, error ? error->message : "Unknown error");
		if (error)
			g_error_free (error);
		g_object_unref (message);
		return;
	}

	g_object_unref (message);
	g_object_unref (response);
}

static gboolean
connman_connect (EMailConnectionConnMan *manager)
{
	GError *error = NULL;
	EMailConnectionConnManPrivate *priv = GET_PRIVATE(manager);

	/* This is a timeout callback, so the return value denotes
	 * whether to reschedule, not whether we're successful. */

	if (priv->connection != NULL)
		return FALSE;

	priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (priv->connection == NULL) {
		g_warning ("%s: %s", G_STRFUNC, error ? error->message : "Unknown error");
		g_error_free (error);

		return TRUE;
	}

	g_dbus_connection_set_exit_on_close (priv->connection, FALSE);

	if (!g_dbus_connection_signal_subscribe (
		priv->connection,
		CONNMAN_DBUS_SERVICE,
		CONNMAN_DBUS_INTERFACE,
		NULL,
		CONNMAN_DBUS_PATH,
		NULL,
		G_DBUS_SIGNAL_FLAGS_NONE,
		connman_signal_cb,
		manager,
		NULL)) {
		g_warning ("%s: Failed to subscribe for a signal", G_STRFUNC);
		goto fail;
	}

	g_signal_connect (priv->connection, "closed", G_CALLBACK (connman_connection_closed_cb), manager);

	connman_check_initial_state (manager);

	return FALSE;

fail:
	g_object_unref (priv->connection);
	priv->connection = NULL;

	return TRUE;
}

static void
e_mail_connection_connman_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
e_mail_connection_connman_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
e_mail_connection_connman_dispose (GObject *object)
{
  G_OBJECT_CLASS (e_mail_connection_connman_parent_class)->dispose (object);
}

static void
e_mail_connection_connman_finalize (GObject *object)
{
  G_OBJECT_CLASS (e_mail_connection_connman_parent_class)->finalize (object);
}

static void
e_mail_connection_connman_constructed (GObject *object)
{
	connman_connect (E_MAIL_CONNECTION_CONNMAN(object));
}

static void
e_mail_connection_connman_class_init (EMailConnectionConnManClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EMailConnectionConnManPrivate));

  object_class->get_property = e_mail_connection_connman_get_property;
  object_class->set_property = e_mail_connection_connman_set_property;
  object_class->dispose = e_mail_connection_connman_dispose;
  object_class->finalize = e_mail_connection_connman_finalize;
object_class->constructed = e_mail_connection_connman_constructed;

}

static void
e_mail_connection_connman_init (EMailConnectionConnMan *self)
{
}

EMailConnectionConnMan*
e_mail_connection_connman_new (void)
{
  return g_object_new (E_MAIL_TYPE_CONNECTION_CONNMAN, NULL);
}

