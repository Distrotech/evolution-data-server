/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: 
 *    Srinivasa Ragavan <sragavan@novell.com>
 *    Suman Manjunath <msuman@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU Lesser General Public
 *  License as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public 
 *  License along with this program; if not, write to: 
 *  Free Software Foundation, 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#ifndef EXCHANGE_MAPI_CONNECTION_H
#define EXCHANGE_MAPI_CONNECTION_H 

#include <libmapi/libmapi.h>
#include "exchange-mapi-defs.h"

typedef enum _ExchangeMapiOptions {
	MAPI_OPTIONS_FETCH_ATTACHMENTS = 1<<0,
	MAPI_OPTIONS_FETCH_RECIPIENTS = 1<<1,
	MAPI_OPTIONS_FETCH_BODY_STREAM = 1<<2,
	MAPI_OPTIONS_FETCH_GENERIC_STREAMS = 1<<3, 
	MAPI_OPTIONS_DONT_SUBMIT = 1<<4
} ExchangeMapiOptions;

#define MAPI_OPTIONS_FETCH_ALL MAPI_OPTIONS_FETCH_ATTACHMENTS | \
	                       MAPI_OPTIONS_FETCH_RECIPIENTS | \
	                       MAPI_OPTIONS_FETCH_BODY_STREAM | \
	                       MAPI_OPTIONS_FETCH_GENERIC_STREAMS

/* FIXME: need to accomodate rendering position */
typedef struct {
	GByteArray *value;
	const gchar *filename;
	const gchar *mime_type;
} ExchangeMAPIAttachment;

typedef struct {
	GByteArray *value;
	uint32_t proptag;
} ExchangeMAPIStream;

typedef struct {
	GByteArray *value;
	uint32_t proptag;
	uint32_t editor_format;
} ExchangeMAPIBodyStream;

typedef struct {
	/* MANDATORY */
	const char *email_id;

	/* It is ideal to set all these properties on all recipients 
	 * as we never know if a recipient would be resolved or not. */ 
	struct {
		/* These are properties which would be set on the 
		 * recipients regardless if the recipient is resolved or not */
		uint32_t req_cValues; 
		struct SPropValue *req_lpProps;

		/* These are properties which would be set on the 
		 * recipients only if the recipient is MAPI_UNRESOLVED */
		uint32_t ext_cValues; 
		struct SPropValue *ext_lpProps;
	} in; 

	struct {
		/* These are properties which would be set on the 
		 * recipients after GetRecipientTable() */
		uint32_t all_cValues; 
		struct SPropValue *all_lpProps;
	} out; 
} ExchangeMAPIRecipient;

struct id_list {
	mapi_id_t id;
};

typedef gboolean (*FetchCallback) 	(struct mapi_SPropValue_array *, const mapi_id_t fid, const mapi_id_t mid, 
					GSList *streams, GSList *recipients, GSList *attachments, gpointer data);
typedef gboolean (*BuildNameID) 	(struct mapi_nameid *nameid, gpointer data);
typedef int 	 (*BuildProps) 		(struct SPropValue **, struct SPropTagArray *, gpointer data);

gboolean 
exchange_mapi_connection_new (const char *profile, const char *password);

void
exchange_mapi_connection_close (void);

gboolean
exchange_mapi_connection_exists (void);

gboolean
exchange_mapi_connection_fetch_item (mapi_id_t fid, mapi_id_t mid, 
				     const uint32_t *GetPropsList, const uint16_t cn_props, 
				     BuildNameID build_name_id, gpointer build_name_data, 
				     FetchCallback cb, gpointer data, 
				     guint32 options);
gboolean
exchange_mapi_connection_fetch_items   (mapi_id_t fid, 
					struct mapi_SRestriction *res,
					const uint32_t *GetPropsList, const uint16_t cn_props, 
					BuildNameID build_name_id, gpointer build_name_data, 
					FetchCallback cb, gpointer data, 
					guint32 options);

mapi_id_t 
exchange_mapi_create_folder (uint32_t olFolder, mapi_id_t pfid, const char *name);
gboolean 
exchange_mapi_remove_folder (uint32_t olFolder, mapi_id_t fid);
gboolean 
exchange_mapi_rename_folder (mapi_id_t fid, const char *new_name);

guint32
exchange_mapi_util_check_restriction (mapi_id_t fid, struct mapi_SRestriction *res);

mapi_id_t
exchange_mapi_create_item (uint32_t olFolder, mapi_id_t fid, 
			   BuildNameID build_name_id, gpointer ni_data, 
			   BuildProps build_props, gpointer p_data, 
			   GSList *recipients, GSList *attachments, GSList *generic_streams,
			   uint32_t options);
gboolean
exchange_mapi_modify_item (uint32_t olFolder, mapi_id_t fid, mapi_id_t mid, 
			   BuildNameID build_name_id, gpointer ni_data, 
			   BuildProps build_props, gpointer p_data,
			   GSList *recipients, GSList *attachments,
			   uint32_t options);

gboolean
exchange_mapi_set_flags (uint32_t olFolder, mapi_id_t fid, GSList *mid_list, uint32_t flag);

gboolean
exchange_mapi_remove_items (uint32_t olFolder, mapi_id_t fid, GSList *mids);

gboolean
exchange_mapi_copy_items ( mapi_id_t src_fid, mapi_id_t dest_fid, GSList *mids);

gboolean
exchange_mapi_move_items ( mapi_id_t src_fid, mapi_id_t dest_fid, GSList *mids);


gboolean exchange_mapi_get_folders_list (GSList **mapi_folders); 
gboolean exchange_mapi_get_pf_folders_list (GSList **mapi_folders); 

struct SPropTagArray *
exchange_mapi_util_resolve_named_props (uint32_t olFolder, mapi_id_t fid, 
				   BuildNameID build_name_id, gpointer ni_data);
struct SPropTagArray *
exchange_mapi_util_resolve_named_prop (uint32_t olFolder, mapi_id_t fid, 
				       uint16_t lid, const char *OLEGUID);
uint32_t
exchange_mapi_util_create_named_prop (uint32_t olFolder, mapi_id_t fid, 
				      const char *named_prop_name, uint32_t ptype);

#endif
