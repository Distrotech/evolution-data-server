/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_IMAPX_SERVER_H
#define CAMEL_IMAPX_SERVER_H

#include <camel/camel-data-cache.h>
#include <camel/camel-session.h>
#include <camel/camel-store.h>

#include "camel-imapx-command.h"
#include "camel-imapx-mailbox.h"
#include "camel-imapx-namespace-response.h"
#include "camel-imapx-stream.h"
#include "camel-imapx-store-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_SERVER \
	(camel_imapx_server_get_type ())
#define CAMEL_IMAPX_SERVER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_SERVER, CamelIMAPXServer))
#define CAMEL_IMAPX_SERVER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_SERVER, CamelIMAPXServerClass))
#define CAMEL_IS_IMAPX_SERVER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_SERVER))
#define CAMEL_IS_IMAPX_SERVER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_SERVER))
#define CAMEL_IMAPX_SERVER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_SERVER, CamelIMAPXServerClass))

G_BEGIN_DECLS

/* Avoid a circular reference. */
struct _CamelIMAPXStore;
struct _CamelIMAPXSettings;

typedef struct _CamelIMAPXServer CamelIMAPXServer;
typedef struct _CamelIMAPXServerClass CamelIMAPXServerClass;
typedef struct _CamelIMAPXServerPrivate CamelIMAPXServerPrivate;

typedef struct _CamelIMAPXIdle CamelIMAPXIdle;

/* untagged response handling */
typedef gboolean
		(*CamelIMAPXUntaggedRespHandler)
						(CamelIMAPXServer *server,
						 CamelIMAPXStream *stream,
						 GCancellable *cancellable,
						 GError **error);

/**
 * CamelIMAPXUntaggedRespHandlerDesc:
 * @untagged_response: a string representation of the IMAP
 *                     untagged response code. Must be
 *                     all-uppercase with underscores allowed
 *                     (see RFC 3501)
 * @handler: an untagged response handler function for #CamelIMAPXServer
 * @next_response: the IMAP untagged code to call a registered
 *                 handler for directly after successfully
 *                 running @handler. If not NULL, @skip_stream_when_done
 *                 for the current handler has no effect
 * @skip_stream_when_done: whether or not to skip the current IMAP
 *                         untagged response in the #CamelIMAPXStream.
 *                         Set to TRUE if your handler does not eat
 *                         the stream up to the next response token
 *
 * IMAP untagged response handler function descriptor. Use in conjunction
 * with camel_imapx_server_register_untagged_handler() to register a new
 * handler function for a given untagged response code
 *
 * Since: 3.6
 */
typedef struct _CamelIMAPXUntaggedRespHandlerDesc CamelIMAPXUntaggedRespHandlerDesc;
struct _CamelIMAPXUntaggedRespHandlerDesc {
	const gchar *untagged_response;
	const CamelIMAPXUntaggedRespHandler handler;
	const gchar *next_response;
	gboolean skip_stream_when_done;
};

struct _CamelIMAPXServer {
	CamelObject parent;
	CamelIMAPXServerPrivate *priv;

	/* Info about the current connection */
	struct _capability_info *cinfo;
	gboolean is_process_stream;

	/* incoming jobs */
	GQueue jobs;

	gchar tagprefix;
	gint state : 4;

	/* Current command/work queue.  All commands are stored in one list,
	 * all the time, so they can be cleaned up in exception cases */
	GRecMutex queue_lock;
	CamelIMAPXCommand *literal;
	CamelIMAPXCommandQueue *queue;
	CamelIMAPXCommandQueue *active;
	CamelIMAPXCommandQueue *done;

	/* Idle */
	CamelIMAPXIdle *idle;

	gboolean use_qresync;
};

struct _CamelIMAPXServerClass {
	CamelObjectClass parent_class;

	gchar tagprefix;

	/* Signals */
	void		(*mailbox_created)	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox);
	void		(*mailbox_renamed)	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 const gchar *oldname);
	void		(*mailbox_updated)	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox);
};

GType		camel_imapx_server_get_type	(void);
CamelIMAPXServer *
		camel_imapx_server_new		(struct _CamelIMAPXStore *store);
struct _CamelIMAPXStore *
		camel_imapx_server_ref_store	(CamelIMAPXServer *is);
struct _CamelIMAPXSettings *
		camel_imapx_server_ref_settings	(CamelIMAPXServer *is);
CamelIMAPXStream *
		camel_imapx_server_ref_stream	(CamelIMAPXServer *is);
CamelIMAPXNamespaceResponse *
		camel_imapx_server_ref_namespaces
						(CamelIMAPXServer *is);
CamelIMAPXMailbox *
		camel_imapx_server_ref_mailbox	(CamelIMAPXServer *is,
						 const gchar *mailbox_name);
CamelIMAPXMailbox *
		camel_imapx_server_ref_selected	(CamelIMAPXServer *is);
GList *		camel_imapx_server_list_mailboxes
						(CamelIMAPXServer *is,
						 CamelIMAPXNamespace *namespace_,
						 const gchar *pattern);
gboolean	camel_imapx_server_connect	(CamelIMAPXServer *is,
						 GCancellable *cancellable,
						 GError **error);
gboolean	imapx_connect_to_server		(CamelIMAPXServer *is,
						 GCancellable *cancellable,
						 GError **error);
CamelAuthenticationResult
		camel_imapx_server_authenticate	(CamelIMAPXServer *is,
						 const gchar *mechanism,
						 GCancellable *cancellable,
						 GError **error);
GPtrArray *	camel_imapx_server_list		(CamelIMAPXServer *is,
						 const gchar *pattern,
						 CamelStoreGetFolderInfoFlags flags,
						 const gchar *ext,
						 GCancellable *cancellable,
						 GError **error);
CamelFolderChangeInfo *
		camel_imapx_server_refresh_info	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_sync_changes	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_expunge	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 GCancellable *cancellable,
						 GError **error);
CamelFolderChangeInfo *
		camel_imapx_server_fetch_messages
						(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 CamelFetchType type,
						 gint limit,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_noop		(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 GCancellable *cancellable,
						 GError **error);
CamelStream *	camel_imapx_server_get_message	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 CamelFolderSummary *summary,
						 CamelDataCache *message_cache,
						 const gchar *message_uid,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_copy_message	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 CamelIMAPXMailbox *destination,
						 GPtrArray *uids,
						 gboolean delete_originals,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_append_message
						(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 CamelFolderSummary *summary,
						 CamelDataCache *message_cache,
						 CamelMimeMessage *message,
						 const CamelMessageInfo *mi,
						 gchar **append_uid,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_sync_message	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 CamelFolderSummary *summary,
						 CamelDataCache *message_cache,
						 const gchar *message_uid,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_create_mailbox
						(CamelIMAPXServer *is,
						 const gchar *mailbox_name,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_delete_mailbox
						(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_rename_mailbox
						(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 const gchar *new_mailbox_name,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_subscribe_mailbox
						(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_unsubscribe_mailbox
						(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imapx_server_update_quota_info
						(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 GCancellable *cancellable,
						 GError **error);
GPtrArray *	camel_imapx_server_uid_search	(CamelIMAPXServer *is,
						 CamelIMAPXMailbox *mailbox,
						 const gchar *criteria,
						 GCancellable *cancellable,
						 GError **error);
const CamelIMAPXUntaggedRespHandlerDesc *
		camel_imapx_server_register_untagged_handler
						(CamelIMAPXServer *is,
						 const gchar *untagged_response,
						 const CamelIMAPXUntaggedRespHandlerDesc *desc);

G_END_DECLS

#endif /* CAMEL_IMAPX_SERVER_H */
