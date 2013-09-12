/*
 * camel-imapx-mailbox.h
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_IMAPX_MAILBOX_H
#define CAMEL_IMAPX_MAILBOX_H

#include <camel/camel-imapx-namespace.h>
#include <camel/camel-imapx-list-response.h>
#include <camel/camel-imapx-status-response.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_MAILBOX \
	(camel_imapx_mailbox_get_type ())
#define CAMEL_IMAPX_MAILBOX(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_MAILBOX, CamelIMAPXMailbox))
#define CAMEL_IMAPX_MAILBOX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_MAILBOX, CamelIMAPXMailboxClass))
#define CAMEL_IS_IMAPX_MAILBOX(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_MAILBOX))
#define CAMEL_IS_IMAPX_MAILBOX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_MAILBOX))
#define CAMEL_IMAPX_MAILBOX_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_MAILBOX, CamelIMAPXMailboxClass))

G_BEGIN_DECLS

typedef struct _CamelIMAPXMailbox CamelIMAPXMailbox;
typedef struct _CamelIMAPXMailboxClass CamelIMAPXMailboxClass;
typedef struct _CamelIMAPXMailboxPrivate CamelIMAPXMailboxPrivate;

/**
 * CamelIMAPXMailbox:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.12
 **/
struct _CamelIMAPXMailbox {
	GObject parent;
	CamelIMAPXMailboxPrivate *priv;
};

struct _CamelIMAPXMailboxClass {
	GObjectClass parent_class;
};

GType		camel_imapx_mailbox_get_type
					(void) G_GNUC_CONST;
CamelIMAPXMailbox *
		camel_imapx_mailbox_new	(CamelIMAPXListResponse *response,
					 CamelIMAPXNamespace *namespace_);
CamelIMAPXMailbox *
		camel_imapx_mailbox_clone
					(CamelIMAPXMailbox *mailbox,
					 const gchar *new_mailbox_name);
gboolean	camel_imapx_mailbox_exists
					(CamelIMAPXMailbox *mailbox);
gint		camel_imapx_mailbox_compare
					(CamelIMAPXMailbox *mailbox_a,
					 CamelIMAPXMailbox *mailbox_b);
gboolean	camel_imapx_mailbox_matches
					(CamelIMAPXMailbox *mailbox,
					 const gchar *pattern);
const gchar *	camel_imapx_mailbox_get_name
					(CamelIMAPXMailbox *mailbox);
gchar		camel_imapx_mailbox_get_separator
					(CamelIMAPXMailbox *mailbox);
CamelIMAPXNamespace *
		camel_imapx_mailbox_get_namespace
					(CamelIMAPXMailbox *mailbox);
guint32		camel_imapx_mailbox_get_messages
					(CamelIMAPXMailbox *mailbox);
void		camel_imapx_mailbox_set_messages
					(CamelIMAPXMailbox *mailbox,
					 guint32 messages);
guint32		camel_imapx_mailbox_get_recent
					(CamelIMAPXMailbox *mailbox);
void		camel_imapx_mailbox_set_recent
					(CamelIMAPXMailbox *mailbox,
					 guint32 recent);
guint32		camel_imapx_mailbox_get_unseen
					(CamelIMAPXMailbox *mailbox);
void		camel_imapx_mailbox_set_unseen
					(CamelIMAPXMailbox *mailbox,
					 guint32 unseen);
guint32		camel_imapx_mailbox_get_uidnext
					(CamelIMAPXMailbox *mailbox);
void		camel_imapx_mailbox_set_uidnext
					(CamelIMAPXMailbox *mailbox,
					 guint32 uidnext);
guint32		camel_imapx_mailbox_get_uidvalidity
					(CamelIMAPXMailbox *mailbox);
void		camel_imapx_mailbox_set_uidvalidity
					(CamelIMAPXMailbox *mailbox,
					 guint32 uidvalidity);
guint64		camel_imapx_mailbox_get_highestmodseq
					(CamelIMAPXMailbox *mailbox);
void		camel_imapx_mailbox_set_highestmodseq
					(CamelIMAPXMailbox *mailbox,
					 guint64 highestmodseq);
gchar **	camel_imapx_mailbox_dup_quota_roots
					(CamelIMAPXMailbox *mailbox);
void		camel_imapx_mailbox_set_quota_roots
					(CamelIMAPXMailbox *mailbox,
					 const gchar **quota_roots);
void		camel_imapx_mailbox_deleted
					(CamelIMAPXMailbox *mailbox);
void		camel_imapx_mailbox_subscribed
					(CamelIMAPXMailbox *mailbox);
void		camel_imapx_mailbox_unsubscribed
					(CamelIMAPXMailbox *mailbox);
gboolean	camel_imapx_mailbox_has_attribute
					(CamelIMAPXMailbox *mailbox,
					 const gchar *attribute);
void		camel_imapx_mailbox_handle_list_response
					(CamelIMAPXMailbox *mailbox,
					 CamelIMAPXListResponse *response);
void		camel_imapx_mailbox_handle_lsub_response
					(CamelIMAPXMailbox *mailbox,
					 CamelIMAPXListResponse *response);
void		camel_imapx_mailbox_handle_status_response
					(CamelIMAPXMailbox *mailbox,
					 CamelIMAPXStatusResponse *response);

G_END_DECLS

#endif /* CAMEL_IMAPX_MAILBOX_H */

