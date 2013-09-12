/*
 * camel-imapx-mailbox.c
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
 */

/**
 * SECTION: camel-imapx-mailbox
 * @include: camel/camel.h
 * @short_description: Stores the state of an IMAP mailbox
 *
 * #CamelIMAPXMailbox models the current state of an IMAP mailbox as
 * accumulated from untagged IMAP server responses in the current session.
 *
 * In particular, a #CamelIMAPXMailbox should <emphasis>not</emphasis> be
 * populated with locally cached information from the previous session.
 * This is why instantiation requires a #CamelIMAPXListResponse.
 **/

#include "camel-imapx-mailbox.h"

#define CAMEL_IMAPX_MAILBOX_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_MAILBOX, CamelIMAPXMailboxPrivate))

struct _CamelIMAPXMailboxPrivate {
	gchar *name;
	gchar separator;
	CamelIMAPXNamespace *namespace;

	guint32 messages;
	guint32 recent;
	guint32 unseen;
	guint32 uidnext;
	guint32 uidvalidity;
	guint64 highestmodseq;

	GMutex property_lock;

	/* Protected by the "property_lock". */
	GHashTable *attributes;
	gchar **quota_roots;
};

G_DEFINE_TYPE (
	CamelIMAPXMailbox,
	camel_imapx_mailbox,
	G_TYPE_OBJECT)

static void
imapx_mailbox_dispose (GObject *object)
{
	CamelIMAPXMailboxPrivate *priv;

	priv = CAMEL_IMAPX_MAILBOX_GET_PRIVATE (object);

	g_clear_object (&priv->namespace);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_mailbox_parent_class)->dispose (object);
}

static void
imapx_mailbox_finalize (GObject *object)
{
	CamelIMAPXMailboxPrivate *priv;

	priv = CAMEL_IMAPX_MAILBOX_GET_PRIVATE (object);

	g_free (priv->name);

	g_mutex_clear (&priv->property_lock);
	g_hash_table_destroy (priv->attributes);
	g_strfreev (priv->quota_roots);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_mailbox_parent_class)->finalize (object);
}

static void
camel_imapx_mailbox_class_init (CamelIMAPXMailboxClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelIMAPXMailboxPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = imapx_mailbox_dispose;
	object_class->finalize = imapx_mailbox_finalize;
}

static void
camel_imapx_mailbox_init (CamelIMAPXMailbox *mailbox)
{
	mailbox->priv = CAMEL_IMAPX_MAILBOX_GET_PRIVATE (mailbox);

	g_mutex_init (&mailbox->priv->property_lock);
}

/**
 * camel_imapx_mailbox_new:
 * @response: a #CamelIMAPXListResponse
 * @namespace_: a #CamelIMAPXNamespace
 *
 * Creates a new #CamelIMAPXMailbox from @response and @namespace.
 *
 * The mailbox's name, path separator character, and attribute set are
 * initialized from the #CamelIMAPXListResponse.
 *
 * Returns: a #CamelIMAPXMailbox
 *
 * Since: 3.12
 **/
CamelIMAPXMailbox *
camel_imapx_mailbox_new (CamelIMAPXListResponse *response,
                         CamelIMAPXNamespace *namespace)
{
	CamelIMAPXMailbox *mailbox;
	GHashTable *attributes;
	const gchar *name;
	gchar separator;

	g_return_val_if_fail (CAMEL_IS_IMAPX_LIST_RESPONSE (response), NULL);
	g_return_val_if_fail (CAMEL_IS_IMAPX_NAMESPACE (namespace), NULL);

	name = camel_imapx_list_response_get_mailbox_name (response);
	separator = camel_imapx_list_response_get_separator (response);
	attributes = camel_imapx_list_response_dup_attributes (response);

	/* The INBOX mailbox is case-insensitive. */
	if (g_ascii_strcasecmp (name, "INBOX") == 0)
		name = "INBOX";

	mailbox = g_object_new (CAMEL_TYPE_IMAPX_MAILBOX, NULL);
	mailbox->priv->name = g_strdup (name);
	mailbox->priv->separator = separator;
	mailbox->priv->namespace = g_object_ref (namespace);
	mailbox->priv->attributes = attributes;  /* takes ownership */

	return mailbox;
}

/**
 * camel_imapx_mailbox_clone:
 * @mailbox: a #CamelIMAPXMailbox
 * @new_mailbox_name: new name for the cloned mailbox
 *
 * Creates an identical copy of @mailbox, except for the mailbox name.
 * The copied #CamelIMAPXMailbox is given the name @new_mailbox_name.
 *
 * The @new_mailbox_name must be in the same IMAP namespace as @mailbox.
 *
 * This is primarily useful for handling mailbox renames.  It is safer to
 * create a new #CamelIMAPXMailbox instance with the new name than to try
 * and rename an existing #CamelIMAPXMailbox, which could disrupt mailbox
 * operations in progress as well as data structures that track mailboxes
 * by name.
 *
 * Returns: a copy of @mailbox, named @new_mailbox_name
 *
 * Since: 3.12
 **/
CamelIMAPXMailbox *
camel_imapx_mailbox_clone (CamelIMAPXMailbox *mailbox,
                           const gchar *new_mailbox_name)
{
	CamelIMAPXMailbox *clone;
	GHashTableIter iter;
	gpointer key;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), NULL);
	g_return_val_if_fail (new_mailbox_name != NULL, NULL);

	/* The INBOX mailbox is case-insensitive. */
	if (g_ascii_strcasecmp (new_mailbox_name, "INBOX") == 0)
		new_mailbox_name = "INBOX";

	clone = g_object_new (CAMEL_TYPE_IMAPX_MAILBOX, NULL);
	clone->priv->name = g_strdup (new_mailbox_name);
	clone->priv->separator = mailbox->priv->separator;
	clone->priv->namespace = g_object_ref (mailbox->priv->namespace);

	clone->priv->messages = mailbox->priv->messages;
	clone->priv->recent = mailbox->priv->recent;
	clone->priv->unseen = mailbox->priv->unseen;
	clone->priv->uidnext = mailbox->priv->uidnext;
	clone->priv->uidvalidity = mailbox->priv->uidvalidity;
	clone->priv->highestmodseq = mailbox->priv->highestmodseq;

	clone->priv->quota_roots = g_strdupv (mailbox->priv->quota_roots);

	/* Use camel_imapx_list_response_dup_attributes()
	 * as a guide for cloning the mailbox attributes. */

	clone->priv->attributes = g_hash_table_new (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal);

	g_mutex_lock (&mailbox->priv->property_lock);

	g_hash_table_iter_init (&iter, mailbox->priv->attributes);

	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_hash_table_add (clone->priv->attributes, key);

	g_mutex_unlock (&mailbox->priv->property_lock);

	return clone;
}

/**
 * camel_imapx_mailbox_exists:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Convenience function returns whether @mailbox exists; that is, whether it
 * <emphasis>lacks</emphasis> a #CAMEL_IMAPX_LIST_ATTR_NONEXISTENT attribute.
 *
 * Non-existent mailboxes should generally be disregarded.
 *
 * Returns: whether @mailbox exists
 *
 * Since: 3.12
 **/
gboolean
camel_imapx_mailbox_exists (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);

	return !camel_imapx_mailbox_has_attribute (
		mailbox, CAMEL_IMAPX_LIST_ATTR_NONEXISTENT);
}

/**
 * camel_imapx_mailbox_compare:
 * @mailbox_a: the first #CamelIMAPXMailbox
 * @mailbox_b: the second #CamelIMAPXMailbox
 *
 * Compares two #CamelIMAPXMailbox instances by their mailbox names.
 *
 * Returns: a negative value if @mailbox_a compares before @mailbox_b,
 *          zero if they compare equal, or a positive value if @mailbox_a
 *          compares after @mailbox_b
 *
 * Since: 3.12
 **/
gint
camel_imapx_mailbox_compare (CamelIMAPXMailbox *mailbox_a,
                             CamelIMAPXMailbox *mailbox_b)
{
	const gchar *mailbox_name_a;
	const gchar *mailbox_name_b;

	mailbox_name_a = camel_imapx_mailbox_get_name (mailbox_a);
	mailbox_name_b = camel_imapx_mailbox_get_name (mailbox_b);

	return g_strcmp0 (mailbox_name_a, mailbox_name_b);
}

/**
 * camel_imapx_mailbox_matches:
 * @mailbox: a #CamelIMAPXMailbox
 * @pattern: mailbox name with possible wildcards
 *
 * Returns %TRUE if @mailbox's name matches @pattern.  The @pattern may
 * contain wildcard characters '*' and '%', which are interpreted similar
 * to the IMAP LIST command.
 *
 * Returns: %TRUE if @mailbox's name matches @pattern, %FALSE otherwise
 *
 * Since: 3.12
 **/
gboolean
camel_imapx_mailbox_matches (CamelIMAPXMailbox *mailbox,
                             const gchar *pattern)
{
	const gchar *name;
	gchar separator;
	gchar name_ch;
	gchar patt_ch;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);
	g_return_val_if_fail (pattern != NULL, FALSE);

	name = camel_imapx_mailbox_get_name (mailbox);
	separator = camel_imapx_mailbox_get_separator (mailbox);

	name_ch = *name++;
	patt_ch = *pattern++;

	while (name_ch != '\0' && patt_ch != '\0') {
		if (name_ch == patt_ch) {
			name_ch = *name++;
			patt_ch = *pattern++;
		} else if (patt_ch == '%') {
			if (name_ch != separator)
				name_ch = *name++;
			else
				patt_ch = *pattern++;
		} else {
			return (patt_ch == '*');
		}
	}

	return (name_ch == '\0') &&
		(patt_ch == '%' || patt_ch == '*' || patt_ch == '\0');
}

/**
 * camel_imapx_mailbox_get_name:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the mailbox name for @mailbox.
 *
 * Returns: the mailbox name
 *
 * Since: 3.12
 **/
const gchar *
camel_imapx_mailbox_get_name (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), NULL);

	return mailbox->priv->name;
}

/**
 * camel_imapx_mailbox_get_separator:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the path separator character for @mailbox.
 *
 * Returns: the mailbox path separator character
 *
 * Since: 3.12
 **/
gchar
camel_imapx_mailbox_get_separator (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), '\0');

	return mailbox->priv->separator;
}

/**
 * camel_imapx_mailbox_get_namespace:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the #CamelIMAPXNamespace representing the IMAP server namespace
 * to which @mailbox belongs.
 *
 * Returns: a #CamelIMAPXNamespace
 *
 * Since: 3.12
 **/
CamelIMAPXNamespace *
camel_imapx_mailbox_get_namespace (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), NULL);

	return mailbox->priv->namespace;
}

/**
 * camel_imapx_mailbox_get_messages:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the last known number of messages in the mailbox.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Returns: the last known "MESSAGES" value
 *
 * Since: 3.12
 **/
guint32
camel_imapx_mailbox_get_messages (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), 0);

	return mailbox->priv->messages;
}

/**
 * camel_imapx_mailbox_set_messages:
 * @mailbox: a #CamelIMAPXMailbox
 * @messages: a newly-reported "MESSAGES" value
 *
 * Updates the last known number of messages in the mailbox.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_set_messages (CamelIMAPXMailbox *mailbox,
                                  guint32 messages)
{
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	mailbox->priv->messages = messages;
}

/**
 * camel_imapx_mailbox_get_recent:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the last known number of messages with the \Recent flag set.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Returns: the last known "RECENT" value
 *
 * Since: 3.12
 **/
guint32
camel_imapx_mailbox_get_recent (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), 0);

	return mailbox->priv->recent;
}

/**
 * camel_imapx_mailbox_set_recent:
 * @mailbox: a #CamelIMAPXMailbox
 * @recent: a newly-reported "RECENT" value
 *
 * Updates the last known number of messages with the \Recent flag set.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_set_recent (CamelIMAPXMailbox *mailbox,
                                guint32 recent)
{
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	mailbox->priv->recent = recent;
}

/**
 * camel_imapx_mailbox_get_unseen:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the last known number of messages which do not have the \Seen
 * flag set.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Returns: the last known "UNSEEN" value
 *
 * Since: 3.12
 **/
guint32
camel_imapx_mailbox_get_unseen (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), 0);

	return mailbox->priv->unseen;
}

/**
 * camel_imapx_mailbox_set_unseen:
 * @mailbox: a #CamelIMAPXMailbox
 * @unseen: a newly-reported "UNSEEN" value
 *
 * Updates the last known number of messages which do not have the \Seen
 * flag set.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_set_unseen (CamelIMAPXMailbox *mailbox,
                                guint32 unseen)
{
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	mailbox->priv->unseen = unseen;
}

/**
 * camel_imapx_mailbox_get_uidnext:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the last known next unique identifier value of the mailbox.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Returns: the last known "UIDNEXT" value
 *
 * Since: 3.12
 **/
guint32
camel_imapx_mailbox_get_uidnext (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), 0);

	return mailbox->priv->uidnext;
}

/**
 * camel_imapx_mailbox_set_uidnext:
 * @mailbox: a #CamelIMAPXMailbox
 * @uidnext: a newly-reported "UIDNEXT" value
 *
 * Updates the last known next unique identifier value of the mailbox.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_set_uidnext (CamelIMAPXMailbox *mailbox,
                                 guint32 uidnext)
{
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	mailbox->priv->uidnext = uidnext;
}

/**
 * camel_imapx_mailbox_get_uidvalidity:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the last known unique identifier validity value of the mailbox.
 *
 * This valud should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Returns: the last known "UIDVALIDITY" value
 *
 * Since: 3.12
 **/
guint32
camel_imapx_mailbox_get_uidvalidity (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), 0);

	return mailbox->priv->uidvalidity;
}

/**
 * camel_imapx_mailbox_set_uidvalidity:
 * @mailbox: a #CamelIMAPXMailbox
 * @uidvalidity: a newly-reported "UIDVALIDITY" value
 *
 * Updates the last known unique identifier validity value of the mailbox.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_set_uidvalidity (CamelIMAPXMailbox *mailbox,
                                     guint32 uidvalidity)
{
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	mailbox->priv->uidvalidity = uidvalidity;
}

/**
 * camel_imapx_mailbox_get_highestmodseq:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the last known highest mod-sequence value of all messages in the
 * mailbox, or zero if the server does not support the persistent storage of
 * mod-sequences for the mailbox.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Returns: the last known "HIGHESTMODSEQ" value
 *
 * Since: 3.12
 **/
guint64
camel_imapx_mailbox_get_highestmodseq (CamelIMAPXMailbox *mailbox)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), 0);

	return mailbox->priv->highestmodseq;
}

/**
 * camel_imapx_mailbox_set_highestmodseq:
 * @mailbox: a #CamelIMAPXMailbox
 * @highestmodseq: a newly-reported "HIGHESTMODSEQ" value
 *
 * Updates the last known highest mod-sequence value of all messages in
 * the mailbox.  If the server does not support the persistent storage of
 * mod-sequences for the mailbox then the value should remain zero.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_set_highestmodseq (CamelIMAPXMailbox *mailbox,
                                       guint64 highestmodseq)
{
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	mailbox->priv->highestmodseq = highestmodseq;
}

/**
 * camel_imapx_mailbox_dup_quota_roots:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Returns the last known list of quota roots for @mailbox as described
 * in <ulink url="http://tools.ietf.org/html/rfc2087">RFC 2087</ulink>,
 * or %NULL if no quota information for @mailbox is available.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * The returned newly-allocated, %NULL-terminated string array should
 * be freed with g_strfreev() when finished with it.
 *
 * Returns: the last known "QUOTAROOT" value
 *
 * Since: 3.12
 **/
gchar **
camel_imapx_mailbox_dup_quota_roots (CamelIMAPXMailbox *mailbox)
{
	gchar **quota_roots;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), NULL);

	g_mutex_lock (&mailbox->priv->property_lock);

	quota_roots = g_strdupv (mailbox->priv->quota_roots);

	g_mutex_unlock (&mailbox->priv->property_lock);

	return quota_roots;
}

/**
 * camel_imapx_mailbox_set_quota_roots:
 * @mailbox: a #CamelIMAPXMailbox
 * @quota_roots: a newly-reported "QUOTAROOT" value
 *
 * Updates the last known list of quota roots for @mailbox as described
 * in <ulink url="http://tools.ietf.org/html/rfc2087">RFC 2087</ulink>.
 *
 * This value should reflect the present state of the IMAP server as
 * reported through untagged server responses in the current session.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_set_quota_roots (CamelIMAPXMailbox *mailbox,
                                     const gchar **quota_roots)
{
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	g_mutex_lock (&mailbox->priv->property_lock);

	g_strfreev (mailbox->priv->quota_roots);
	mailbox->priv->quota_roots = g_strdupv ((gchar **) quota_roots);

	g_mutex_unlock (&mailbox->priv->property_lock);
}

/**
 * camel_imapx_mailbox_deleted:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Adds the #CAMEL_IMAPX_LIST_ATTR_NONEXISTENT attribute to @mailbox.
 *
 * Call this function after successfully completing a DELETE command.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_deleted (CamelIMAPXMailbox *mailbox)
{
	const gchar *attribute;

	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	attribute = CAMEL_IMAPX_LIST_ATTR_NONEXISTENT;

	g_mutex_lock (&mailbox->priv->property_lock);

	g_hash_table_add (
		mailbox->priv->attributes,
		(gpointer) g_intern_string (attribute));

	g_mutex_unlock (&mailbox->priv->property_lock);
}

/**
 * camel_imapx_mailbox_subscribed:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Add the #CAMEL_IMAPX_LIST_ATTR_SUBSCRIBED attribute to @mailbox.
 *
 * Call this function after successfully completing a SUBSCRIBE command.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_subscribed (CamelIMAPXMailbox *mailbox)
{
	const gchar *attribute;

	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	attribute = CAMEL_IMAPX_LIST_ATTR_SUBSCRIBED;

	g_mutex_lock (&mailbox->priv->property_lock);

	g_hash_table_add (
		mailbox->priv->attributes,
		(gpointer) g_intern_string (attribute));

	g_mutex_unlock (&mailbox->priv->property_lock);
}

/**
 * camel_imapx_mailbox_unsubscribed:
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Removes the #CAMEL_IMAPX_LIST_ATTR_SUBSCRIBED attribute from @mailbox.
 *
 * Call this function after successfully completing an UNSUBSCRIBE command.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_unsubscribed (CamelIMAPXMailbox *mailbox)
{
	const gchar *attribute;

	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	attribute = CAMEL_IMAPX_LIST_ATTR_SUBSCRIBED;

	g_mutex_lock (&mailbox->priv->property_lock);

	g_hash_table_remove (mailbox->priv->attributes, attribute);

	g_mutex_unlock (&mailbox->priv->property_lock);
}

/**
 * camel_imapx_mailbox_has_attribute:
 * @mailbox: a #CamelIMAPXMailbox
 * @attribute: a mailbox attribute
 *
 * Returns whether @mailbox includes the given mailbox attribute.
 * The @attribute should be one of the LIST attribute macros defined
 * for #CamelIMAPXListResponse.
 *
 * Returns: %TRUE if @mailbox has @attribute, or else %FALSE
 *
 * Since: 3.12
 **/
gboolean
camel_imapx_mailbox_has_attribute (CamelIMAPXMailbox *mailbox,
                                   const gchar *attribute)
{
	gboolean has_it;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox), FALSE);
	g_return_val_if_fail (attribute != NULL, FALSE);

	g_mutex_lock (&mailbox->priv->property_lock);

	has_it = g_hash_table_contains (mailbox->priv->attributes, attribute);

	g_mutex_unlock (&mailbox->priv->property_lock);

	return has_it;
}

/**
 * camel_imapx_mailbox_handle_list_response:
 * @mailbox: a #CamelIMAPXMailbox
 * @response: a #CamelIMAPXListResponse
 *
 * Updates the internal state of @mailbox from the data in @response.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_handle_list_response (CamelIMAPXMailbox *mailbox,
                                          CamelIMAPXListResponse *response)
{
	GHashTable *attributes;

	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));
	g_return_if_fail (CAMEL_IS_IMAPX_LIST_RESPONSE (response));

	attributes = camel_imapx_list_response_dup_attributes (response);

	g_mutex_lock (&mailbox->priv->property_lock);

	g_hash_table_destroy (mailbox->priv->attributes);
	mailbox->priv->attributes = attributes;  /* takes ownership */

	g_mutex_unlock (&mailbox->priv->property_lock);
}

/**
 * camel_imapx_mailbox_handle_lsub_response:
 * @mailbox: a #CamelIMAPXMailbox
 * @response: a #CamelIMAPXListResponse
 *
 * Updates the internal state of @mailbox from the data in @response.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_handle_lsub_response (CamelIMAPXMailbox *mailbox,
                                          CamelIMAPXListResponse *response)
{
	GHashTable *attributes;
	GHashTableIter iter;
	gpointer key;

	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));
	g_return_if_fail (CAMEL_IS_IMAPX_LIST_RESPONSE (response));

	/* LIST responses are more authoritative than LSUB responses,
	 * so instead of replacing the old attribute set as we would
	 * for a LIST response, we'll merge the LSUB attributes. */

	attributes = camel_imapx_list_response_dup_attributes (response);

	g_hash_table_iter_init (&iter, attributes);

	g_mutex_lock (&mailbox->priv->property_lock);

	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_hash_table_add (mailbox->priv->attributes, key);

	g_mutex_unlock (&mailbox->priv->property_lock);

	g_hash_table_destroy (attributes);
}

/**
 * camel_imapx_mailbox_handle_status_response:
 * @mailbox: a #CamelIMAPXMailbox
 * @response: a #CamelIMAPXStatusResponse
 *
 * Updates the internal state of @mailbox from the data in @response.
 *
 * Since: 3.12
 **/
void
camel_imapx_mailbox_handle_status_response (CamelIMAPXMailbox *mailbox,
                                            CamelIMAPXStatusResponse *response)
{
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));
	g_return_if_fail (CAMEL_IS_IMAPX_STATUS_RESPONSE (response));

	mailbox->priv->messages =
		camel_imapx_status_response_get_messages (response);
	mailbox->priv->recent =
		camel_imapx_status_response_get_recent (response);
	mailbox->priv->unseen =
		camel_imapx_status_response_get_unseen (response);
	mailbox->priv->uidnext =
		camel_imapx_status_response_get_uidnext (response);
	mailbox->priv->uidvalidity =
		camel_imapx_status_response_get_uidvalidity (response);
	mailbox->priv->highestmodseq =
		camel_imapx_status_response_get_highestmodseq (response);
}

