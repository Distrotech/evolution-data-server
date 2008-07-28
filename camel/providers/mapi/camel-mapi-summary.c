/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007 Novell Inc.
 *
 * Johnny Jacob <jjohnny@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 3 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "camel-data-cache.h"
#include "camel-file-utils.h"
#include "camel-folder.h"

#include "camel-mapi-folder.h"
#include "camel-mapi-summary.h"

/*Prototypes*/
static int mapi_summary_header_load (CamelFolderSummary *, FILE *);
static int mapi_summary_header_save (CamelFolderSummary *, FILE *);

static CamelMessageInfo *mapi_message_info_load (CamelFolderSummary *s, FILE *in) ;

static int mapi_message_info_save (CamelFolderSummary *s, FILE *out, CamelMessageInfo *info) ;
static CamelMessageContentInfo * mapi_content_info_load (CamelFolderSummary *s, FILE *in) ;
static int mapi_content_info_save (CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *info) ;
static gboolean mapi_info_set_flags(CamelMessageInfo *info, guint32 flags, guint32 set);		

static void camel_mapi_summary_class_init (CamelMapiSummaryClass *klass);
static void camel_mapi_summary_init       (CamelMapiSummary *obj);


/*End of Prototypes*/


static CamelFolderSummaryClass *camel_mapi_summary_parent ;


CamelType
camel_mapi_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(
				camel_folder_summary_get_type(), "CamelMapiSummary",
				sizeof (CamelMapiSummary),
				sizeof (CamelMapiSummaryClass),
				(CamelObjectClassInitFunc) camel_mapi_summary_class_init,
				NULL,
				(CamelObjectInitFunc) camel_mapi_summary_init,
				NULL);
	}

	return type;
}

static CamelMessageInfo *
mapi_message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelMapiMessageInfo *to;
	const CamelMapiMessageInfo *from = (const CamelMapiMessageInfo *)mi;

	to = (CamelMapiMessageInfo *)camel_mapi_summary_parent->message_info_clone(s, mi);
	to->server_flags = from->server_flags;

	/* FIXME: parent clone should do this */
	to->info.content = camel_folder_summary_content_info_new(s);

	return (CamelMessageInfo *)to;
}

static void
camel_mapi_summary_class_init (CamelMapiSummaryClass *klass)
{
	CamelFolderSummaryClass *cfs_class = (CamelFolderSummaryClass *) klass;

	camel_mapi_summary_parent = CAMEL_FOLDER_SUMMARY_CLASS (camel_type_get_global_classfuncs (camel_folder_summary_get_type()));

	cfs_class->message_info_clone = mapi_message_info_clone ;
	cfs_class->summary_header_load = mapi_summary_header_load;
	cfs_class->summary_header_save = mapi_summary_header_save;
	cfs_class->message_info_load = mapi_message_info_load;
	cfs_class->message_info_save = mapi_message_info_save;
	cfs_class->content_info_load = mapi_content_info_load;
	cfs_class->content_info_save = mapi_content_info_save;
}


static void
camel_mapi_summary_init (CamelMapiSummary *obj)
{
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelMapiMessageInfo);
	s->content_info_size = sizeof(CamelMapiMessageContentInfo);
	
	/* Meta-summary - Overriding UID len */
	s->meta_summary->uid_len = 2048;
}


/**
 * camel_mapi_summary_new:
 * @filename: the file to store the summary in.
 *
 * This will create a new CamelMapiSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Return value: A new CamelMapiSummary object.
 **/
CamelFolderSummary *
camel_mapi_summary_new (struct _CamelFolder *folder, const char *filename)
{
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY (
			camel_object_new (camel_mapi_summary_get_type ()));
	
	summary->folder = folder ;
	camel_folder_summary_set_build_content (summary, TRUE);
	camel_folder_summary_set_filename (summary, filename);

	if (camel_folder_summary_load (summary) == -1) {
		camel_folder_summary_clear (summary);
		camel_folder_summary_touch (summary);
	}

	return summary;
}

static int
mapi_summary_header_load (CamelFolderSummary *summary, FILE *in) 
{
	if (camel_mapi_summary_parent->summary_header_load (summary, in) == -1)
		return -1 ;

	return 0;
}
static int 
mapi_summary_header_save (CamelFolderSummary *summary, FILE *out) 
{
	if (camel_mapi_summary_parent->summary_header_save (summary, out) == -1)
		return -1;

	return 0;
}

static CamelMessageInfo*
mapi_message_info_load (CamelFolderSummary *s, FILE *in) 
{
	CamelMessageInfo *info ;

	info = camel_mapi_summary_parent->message_info_load(s,in) ;

	return info ;
}

static int 
mapi_message_info_save (CamelFolderSummary *s, FILE *out, CamelMessageInfo *info) 
{
	if (camel_mapi_summary_parent->message_info_save (s, out, info) == -1)
		return -1;

	return 0;
}

static CamelMessageContentInfo* 
mapi_content_info_load (CamelFolderSummary *s, FILE *in) 
{

	if (fgetc (in))
		return camel_mapi_summary_parent->content_info_load (s, in);
	else
		return camel_folder_summary_content_info_new (s);
}

static int
mapi_content_info_save (CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *info)
{
	if (info->type) {
		fputc (1, out);
		return camel_mapi_summary_parent->content_info_save (s, out, info);
	} else
		return fputc (0, out);
}

void
mapi_summary_clear (CamelFolderSummary *summary, gboolean uncache)
{
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;
	int i, count;
	const char *uid;

	changes = camel_folder_change_info_new ();
	count = camel_folder_summary_count (summary);
	for (i = 0; i < count; i++) {
		if (!(info = camel_folder_summary_index (summary, i)))
			continue;

		uid = camel_message_info_uid (info);
		camel_folder_change_info_remove_uid (changes, uid);
		camel_folder_summary_remove_uid (summary, uid);
		camel_message_info_free(info);
	}

	camel_folder_summary_clear (summary);
	camel_folder_summary_save (summary);

	if (uncache)
		camel_data_cache_clear (((CamelMapiFolder *) summary->folder)->cache, "cache", NULL);

	if (camel_folder_change_info_changed (changes))
		camel_object_trigger_event (summary->folder, "folder_changed", changes);
	camel_folder_change_info_free (changes);
}
