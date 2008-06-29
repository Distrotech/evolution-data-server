/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: 
 *    Srinivasa Ragavan <sragavan@novell.com>
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

#ifndef EXCHANGE_MAPI_FOLDER_H
#define EXCHANGE_MAPI_FOLDER_H

typedef enum  {
	MAPI_FOLDER_TYPE_MAIL=1,
	MAPI_FOLDER_TYPE_APPOINTMENT,
	MAPI_FOLDER_TYPE_CONTACT,
	MAPI_FOLDER_TYPE_MEMO,
	MAPI_FOLDER_TYPE_JOURNAL,
	MAPI_FOLDER_TYPE_TASK,
	MAPI_FOLDER_TYPE_NOTE_HOMEPAGE,
	MAPI_FOLDER_TYPE_UNKNOWN
} ExchangeMAPIFolderType;

typedef enum {
	MAPI_PERSONAL_FOLDER,
	MAPI_FAVOURITE_FOLDER,
	MAPI_FOREIGN_FOLDER
} ExchangeMAPIFolderCategory;

typedef struct _ExchangeMAPIFolder {
	/* We'll need this separation of 'owner' and 'user' when we do delegation */
	gchar *owner_name;
	gchar *owner_email;
	gchar *user_name;
	gchar *user_email;

	/* Need this info - default calendars/address books/notes folders can't be deleted */
	gboolean is_default;

	gchar *folder_name;
	gchar *parent_folder_name;
	ExchangeMAPIFolderType container_class;
	ExchangeMAPIFolderCategory category;
	guint64 folder_id;
	guint64 parent_folder_id;
	guint32 child_count;
	guint32 unread_count;
	guint32 total;

	/* reserved */
	gpointer reserved1;
	gpointer reserved2;
	gpointer reserved3;
} ExchangeMAPIFolder;

ExchangeMAPIFolder *
exchange_mapi_folder_new (const char *folder_name, const char *parent_folder_name, const char *container_class, 
			  ExchangeMAPIFolderCategory catgory, 
			  uint64_t folder_id, uint64_t parent_folder_id, 
			  uint32_t child_count, uint32_t unread_count, uint32_t total);
ExchangeMAPIFolderType exchange_mapi_container_class (char *type);

const gchar* exchange_mapi_folder_get_name (ExchangeMAPIFolder *folder);
guint64 exchange_mapi_folder_get_fid (ExchangeMAPIFolder *folder);
guint64 exchange_mapi_folder_get_parent_id (ExchangeMAPIFolder *folder);
ExchangeMAPIFolderType exchange_mapi_folder_get_type (ExchangeMAPIFolder *folder);
guint32 exchange_mapi_folder_get_unread_count (ExchangeMAPIFolder *folder);
guint32 exchange_mapi_folder_get_total_count (ExchangeMAPIFolder *folder);

gboolean exchange_mapi_folder_is_root (ExchangeMAPIFolder *folder);
GSList * exchange_mapi_peek_folder_list (void);
void exchange_mapi_folder_list_free (void);
ExchangeMAPIFolder * exchange_mapi_folder_get_folder (uint64_t fid);
void exchange_mapi_folder_list_add (ExchangeMAPIFolder *folder);

#endif
