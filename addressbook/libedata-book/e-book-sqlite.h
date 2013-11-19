/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-book-sqlitedb.h
 *
 * Copyright (C) 2013 Intel Corporation
 *
 * Authors:
 *     Tristan Van Berkom <tristanvb@openismus.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_BOOK_SQLITE_H
#define E_BOOK_SQLITE_H

#include <libebook-contacts/libebook-contacts.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_SQLITE            (e_book_sqlite_get_type ())
#define E_BOOK_SQLITE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_BOOK_SQLITE, EBookSqlite))
#define E_BOOK_SQLITE_CLASS(cls)      (G_TYPE_CHECK_CLASS_CAST ((cls), E_TYPE_BOOK_SQLITE, EBookSqliteClass))
#define E_IS_BOOK_SQLITE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_BOOK_SQLITE))
#define E_IS_BOOK_SQLITE_CLASS(cls)   (G_TYPE_CHECK_CLASS_TYPE ((cls), E_TYPE_BOOK_SQLITE))
#define E_BOOK_SQLITE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_SQLITE, EBookSqliteClass))

/**
 * E_BOOK_SQL_ERROR:
 *
 * Error domain for #EBookSqlite operations.
 *
 * Since: 3.12
 **/
#define E_BOOK_SQL_ERROR (e_book_sqlite_error_quark ())

/**
 * E_BOOK_SQL_IS_POPULATED_KEY:
 *
 * This key can be used with e_book_sqlite_get_key_value().
 *
 * In the case of a migration from an older SQLite, any value which
 * was previously stored with e_book_sqlitedb_set_is_populated()
 * can be retrieved with this key.
 *
 * Since: 3.12
 **/
#define E_BOOK_SQL_IS_POPULATED_KEY "eds-reserved-namespace-is-populated"

G_BEGIN_DECLS

typedef struct _EBookSqlite EBookSqlite;
typedef struct _EBookSqliteClass EBookSqliteClass;
typedef struct _EBookSqlitePrivate EBookSqlitePrivate;

/**
 * EBookSqlError:
 * @E_BOOK_SQL_ERROR_CONSTRAINT: The error occurred due to an explicit constraint
 * @E_BOOK_SQL_ERROR_CONTACT_NOT_FOUND: A contact was not found by UID (this is different
 *                                      from a query that returns no results, which is not an error).
 * @E_BOOK_SQL_ERROR_OTHER: Another error occurred
 * @E_BOOK_SQL_ERROR_NOT_SUPPORTED: A query was not supported
 * @E_BOOK_SQL_ERROR_INVALID_QUERY: A query was invalid. This can happen if the sexp could not be parsed
 *                                  or if a phone number query contained non-phonenumber input.
 * @E_BOOK_SQL_ERROR_END_OF_LIST: An attempt was made to fetch results past the end of a contact list
 *
 * Defines the types of possible errors reported by the #EBookSqlite
 */
typedef enum {
	E_BOOK_SQL_ERROR_CONSTRAINT,
	E_BOOK_SQL_ERROR_CONTACT_NOT_FOUND,
	E_BOOK_SQL_ERROR_OTHER,
	E_BOOK_SQL_ERROR_NOT_SUPPORTED,
	E_BOOK_SQL_ERROR_INVALID_QUERY,
	E_BOOK_SQL_ERROR_END_OF_LIST
} EBookSqlError;

/**
 * EBookSqlite:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.12
 **/
struct _EBookSqlite {
	/*< private >*/
	GObject parent;
	EBookSqlitePrivate *priv;
};

/**
 * EBookSqliteClass:
 *
 * Class structure for the #EBookSqlite class.
 *
 * Since: 3.12
 */
struct _EBookSqliteClass {
	/*< private >*/
	GObjectClass parent_class;
};

/**
 * EbSdbSearchData:
 * @uid: The %E_CONTACT_UID field of this contact
 * @vcard: The the vcard string
 * @extra: Any extra data associated to the vcard
 *
 * This structure is used to represent contacts returned
 * by the #EBookSqlite from various functions
 * such as e_book_sqlitedb_search().
 *
 * The @extra parameter will contain any data which was
 * previously passed for this contact in e_book_sqlite_add_contact().
 *
 * These should be freed with e_book_sqlite_search_data_free().
 *
 * Since: 3.12
 **/
typedef struct {
	gchar *uid;
	gchar *vcard;
	gchar *extra;
} EbSqlSearchData;

/**
 * EbSqlChangeCallback:
 * @uid: A contact UID
 * @vcard: The vcard string for this UID
 * @user_data: Pointer to user provided data
 *
 * A function which may be called in response to a change
 * in contact data. It can happen that contacts change
 * as a side effect of setting the locale for instance.
 *
 * <note><para>This user callback is called inside a lock,
 * you must not call the #EBookSqlite API from
 * this callback.</para></note>
 *
 * Since: 3.12
 **/
typedef void    (* EbSqlChangeCallback) (const gchar        *uid,
					 const gchar        *vcard,
					 gpointer            user_data);


/**
 * EbSqlVCardCallback:
 * @uid: A contact UID
 * @extra: The extra data associated to the contact
 * @user_data: User data previously passed to e_book_sqlite_new()
 *
 * If this callback is passed to e_book_sqlite_new(), then
 * vcards are not stored in the SQLite and instead this callback
 * is invoked to fetch the vcard.
 *
 * <note><para>This user callback is called inside a lock,
 * you must not call the #EBookSqlite API from
 * this callback.</para></note>
 *
 * Returns: (transfer full): The appropriate vcard indicated by @uid
 *
 * Since: 3.12
 **/
typedef gchar * (* EbSqlVCardCallback)  (const gchar        *uid,
					 const gchar        *extra,
					 gpointer            user_data);


/**
 * EbSqlCuror:
 *
 * An opaque cursor pointer
 *
 * Since: 3.12
 */
typedef struct _EbSqlCursor EbSqlCursor;

/**
 * EbSqlCursorOrigin:
 * @EBSQL_CURSOR_ORIGIN_CURRENT:  The current cursor position
 * @EBSQL_CURSOR_ORIGIN_BEGIN:    The beginning of the cursor results.
 * @EBSQL_CURSOR_ORIGIN_END:      The ending of the cursor results.
 *
 * Specifies the start position to in the list of traversed contacts
 * in calls to e_book_sqlite_cursor_step().
 *
 * When an #EbSqlCuror is created, the current position implied by %EBSQL_CURSOR_ORIGIN_CURRENT
 * is the same as %EBSQL_CURSOR_ORIGIN_BEGIN.
 *
 * Since: 3.12
 */
typedef enum {
	EBSQL_CURSOR_ORIGIN_CURRENT = 0,
	EBSQL_CURSOR_ORIGIN_BEGIN,
	EBSQL_CURSOR_ORIGIN_END
} EbSqlCursorOrigin;

/**
 * EbSqlCursorStepFlags:
 * @EBSQL_CURSOR_STEP_MOVE:  The cursor position should be modified while stepping
 * @EBSQL_CURSOR_STEP_FETCH: Traversed contacts should be listed and returned while stepping.
 *
 * Defines the behaviour of e_book_sqlite_cursor_step().
 *
 * Since: 3.12
 */
typedef enum {
	EBSQL_CURSOR_STEP_MOVE  = (1 << 0),
	EBSQL_CURSOR_STEP_FETCH = (1 << 1)
} EbSqlCursorStepFlags;

GType	     e_book_sqlite_get_type             (void) G_GNUC_CONST;
GQuark       e_book_sqlite_error_quark          (void);
void	     e_book_sqlite_search_data_free     (EbSqlSearchData *data);

EBookSqlite *e_book_sqlite_new		        (const gchar *path,
						 GError **error);
EBookSqlite *e_book_sqlite_new_shallow          (const gchar *path,
						 EbSqlVCardCallback callback,
						 gpointer user_data,
						 GError **error);
EBookSqlite *e_book_sqlite_new_full             (const gchar *path,
						 const gchar *folderid,
						 EbSqlVCardCallback callback,
						 gpointer user_data,
						 ESourceBackendSummarySetup *setup,
						 GError **error);
gboolean     e_book_sqlite_lock_updates         (EBookSqlite *ebsql,
						 gboolean writer_lock,
						 GError **error);
gboolean     e_book_sqlite_unlock_updates       (EBookSqlite *ebsql,
						 gboolean do_commit,
						 GError **error);
gboolean     e_book_sqlite_set_locale           (EBookSqlite *ebsql,
						 const gchar *lc_collate,
						 EbSqlChangeCallback callback,
						 gpointer user_data,
						 GError **error);
gboolean     e_book_sqlite_get_locale           (EBookSqlite *ebsql,
						 gchar **locale_out,
						 GError **error);
GHashTable  *e_book_sqlite_get_uids_and_rev	(EBookSqlite *ebsql,
						 GError **error);

ECollator   *e_book_sqlite_ref_collator         (EBookSqlite *ebsql);

/* Adding / Removing / Searching contacts */
gboolean     e_book_sqlite_add_contact          (EBookSqlite *ebsql,
						 EContact *contact,
						 const gchar *extra,
						 gboolean replace,
						 GError **error);
gboolean     e_book_sqlite_add_contacts         (EBookSqlite *ebsql,
						 GSList *contacts,
						 GSList *extra,
						 gboolean replace,
						 GError **error);
gboolean     e_book_sqlite_remove_contact       (EBookSqlite *ebsql,
						 const gchar *uid,
						 GError **error);
gboolean     e_book_sqlite_remove_contacts	(EBookSqlite *ebsql,
						 GSList *uids,
						 GError **error);
gboolean     e_book_sqlite_has_contact          (EBookSqlite *ebsql,
						 const gchar *uid,
						 gboolean *exists,
						 GError **error);
gboolean     e_book_sqlite_get_contact          (EBookSqlite *ebsql,
						 const gchar *uid,
						 gboolean meta_contact,
						 EContact **ret_contact,
						 GError **error);
gboolean     e_book_sqlite_get_vcard            (EBookSqlite *ebsql,
						 const gchar *uid,
						 gboolean meta_contact,
						 gchar **vcard,
						 GError **error);
gboolean     e_book_sqlite_search               (EBookSqlite *ebsql,
						 const gchar *sexp,
						 gboolean meta_contacts,
						 GSList **ret_list,
						 GError **error);
gboolean     e_book_sqlite_search_uids          (EBookSqlite *ebsql,
						 const gchar *sexp,
						 GSList **ret_list,
						 GError **error);

/* Key / Value convenience API */
gboolean     e_book_sqlite_get_key_value	(EBookSqlite *ebsql,
						 const gchar *key,
						 gchar **value,
						 GError **error);
gboolean     e_book_sqlite_set_key_value	(EBookSqlite *ebsql,
						 const gchar *key,
						 const gchar *value,
						 GError **error);
gboolean     e_book_sqlite_get_key_value_int	(EBookSqlite *ebsql,
						 const gchar *key,
						 gint *value,
						 GError **error);
gboolean     e_book_sqlite_set_key_value_int    (EBookSqlite *ebsql,
						 const gchar *key,
						 gint value,
						 GError **error);

/* Cursor API */
EbSqlCursor *e_book_sqlite_cursor_new           (EBookSqlite *ebsql,
						 const gchar *sexp,
						 const EContactField *sort_fields,
						 const EBookCursorSortType *sort_types,
						 guint n_sort_fields,
						 GError **error);
void         e_book_sqlite_cursor_free          (EBookSqlite *ebsql,
						 EbSqlCursor *cursor);
gint         e_book_sqlite_cursor_step          (EBookSqlite *ebsql,
						 EbSqlCursor *cursor,
						 EbSqlCursorStepFlags flags,
						 EbSqlCursorOrigin origin,
						 gint count,
						 GSList **results,
						 GError **error);
void         e_book_sqlite_cursor_set_target_alphabetic_index
                                                (EBookSqlite *ebsql,
						 EbSqlCursor *cursor,
						 gint idx);
gboolean     e_book_sqlite_cursor_set_sexp      (EBookSqlite *ebsql,
						 EbSqlCursor *cursor,
						 const gchar *sexp,
						 GError **error);
gboolean     e_book_sqlite_cursor_calculate     (EBookSqlite *ebsql,
						 EbSqlCursor *cursor,
						 gint *total,
						 gint *position,
						 GError **error);
gint         e_book_sqlite_cursor_compare_contact
                                                (EBookSqlite *ebsql,
						 EbSqlCursor *cursor,
						 EContact *contact,
						 gboolean *matches_sexp);

G_END_DECLS

#endif /* E_BOOK_SQLITE_H */
