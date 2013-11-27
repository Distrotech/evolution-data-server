/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-book-sqlite.c
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

#include "e-book-sqlite.h"

#include <locale.h>
#include <string.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <sqlite3.h>

/* For e_sqlite3_vfs_init() */
#include <libebackend/libebackend.h>

#include "e-book-backend-sexp.h"

/* Debugging
 *
 * Run EDS with EBSQL_DEBUG=statements:explain to print all statements
 * and explain query plans
 */
#define EBSQL_ENV_DEBUG   "EBSQL_DEBUG"

#define EBSQL_ERROR_STR(code)						\
	((code) == E_BOOK_SQL_ERROR_CONSTRAINT        ? "constraint" :	\
	 (code) == E_BOOK_SQL_ERROR_CONTACT_NOT_FOUND ? "contact not found" : \
	 (code) == E_BOOK_SQL_ERROR_OTHER             ? "other" :	\
	 (code) == E_BOOK_SQL_ERROR_NOT_SUPPORTED     ? "not supported" : \
	 (code) == E_BOOK_SQL_ERROR_INVALID_QUERY     ? "invalid query" : \
	 (code) == E_BOOK_SQL_ERROR_END_OF_LIST       ? "end of list" : "(unknown)")

typedef enum {
	EBSQL_DEBUG_STATEMENTS    = 1 << 0, /* Output all executed statements */
	EBSQL_DEBUG_EXPLAIN       = 1 << 1, /* Output SQLite's query plan for SELECT statements */
	EBSQL_DEBUG_LOCKS         = 1 << 2, /* Print which function locks and unlocks the mutex */
	EBSQL_DEBUG_ERRORS        = 1 << 3,
} EbSqlDebugFlag;

static const GDebugKey ebsql_debug_keys[] = {
	{ "statements",     EBSQL_DEBUG_STATEMENTS },
	{ "explain",        EBSQL_DEBUG_EXPLAIN    },
	{ "locks",          EBSQL_DEBUG_LOCKS      },
	{ "errors",         EBSQL_DEBUG_ERRORS     },
};

static EbSqlDebugFlag ebsql_debug_flags = 0;

static void
ebsql_init_debug (void)
{
	static gboolean initialized = FALSE;

	if (G_UNLIKELY (!initialized)) {
		const gchar *env_string;

		env_string = g_getenv (EBSQL_ENV_DEBUG);

		if (env_string != NULL)
			ebsql_debug_flags = 
				g_parse_debug_string (
					env_string,
					ebsql_debug_keys,
					G_N_ELEMENTS (ebsql_debug_keys));
	}
}

#define EBSQL_NOTE(type,action)					\
	G_STMT_START {						\
		if (ebsql_debug_flags & EBSQL_DEBUG_##type)	\
			{ action; };				\
	} G_STMT_END

#define EBSQL_LOCK_MUTEX(mutex)						\
	G_STMT_START {							\
		if (ebsql_debug_flags & EBSQL_DEBUG_LOCKS) {		\
			g_printerr ("%s: Locking %s\n", G_STRFUNC, #mutex); \
			g_mutex_lock (mutex);				\
			g_printerr ("%s: Locked %s\n", G_STRFUNC, #mutex); \
		} else {						\
			g_mutex_lock (mutex);				\
		}							\
	} G_STMT_END

#define EBSQL_UNLOCK_MUTEX(mutex)					\
	G_STMT_START {							\
		if (ebsql_debug_flags & EBSQL_DEBUG_LOCKS) {		\
			g_printerr ("%s: Unlocking %s\n", G_STRFUNC, #mutex); \
			g_mutex_unlock (mutex);				\
			g_printerr ("%s: Unlocked %s\n", G_STRFUNC, #mutex);	\
		} else {						\
			g_mutex_unlock (mutex);				\
		}							\
	} G_STMT_END

/* Format strings are passed through dgettext(), need to be reformatted */
#define EBSQL_SET_ERROR(error, code, fmt, args...)			\
	G_STMT_START {							\
		if (ebsql_debug_flags & EBSQL_DEBUG_ERRORS) {		\
			gchar *format = g_strdup_printf (		\
				"ERR [%%s]: Set error code '%%s': %s\n", fmt);	\
			g_printerr (format, G_STRFUNC,			\
				    EBSQL_ERROR_STR (code), ## args);	\
			g_free (format);				\
		}							\
		g_set_error (error, E_BOOK_SQL_ERROR, code, fmt, ## args); \
	} G_STMT_END

#define EBSQL_SET_ERROR_LITERAL(error, code, detail)			\
	G_STMT_START {							\
		if (ebsql_debug_flags & EBSQL_DEBUG_ERRORS) {		\
			g_printerr ("ERR [%s]: "			\
				    "Set error code %s: %s\n",		\
				    G_STRFUNC,				\
				    EBSQL_ERROR_STR (code), detail);	\
		}							\
		g_set_error_literal (error, E_BOOK_SQL_ERROR, code, detail); \
	} G_STMT_END

#define FOLDER_VERSION                8
#define INSERT_MULTI_STMT_BYTES       128
#define COLUMN_DEFINITION_BYTES       32
#define GENERATED_QUERY_BYTES         2048

#define DEFAULT_FOLDER_ID            "folder_id"

/* Number of contacts to relocalize at a time
 * while relocalizing the whole database
 */
#define EBSQL_UPGRADE_BATCH_SIZE      100

#define EBSQL_ESCAPE_SEQUENCE        "ESCAPE '^'"

/* Names for custom functions */
#define EBSQL_FUNC_COMPARE_VCARD     "compare_vcard"
#define EBSQL_FUNC_FETCH_VCARD       "fetch_vcard"
#define EBSQL_FUNC_EQPHONE_EXACT     "eqphone_exact"
#define EBSQL_FUNC_EQPHONE_NATIONAL  "eqphone_national"
#define EBSQL_FUNC_EQPHONE_SHORT     "eqphone_short"

/* Fallback collations are generated as with a prefix and an EContactField name */
#define EBSQL_COLLATE_PREFIX         "ebsql_"

/* A special vcard attribute that we use only for private vcards */
#define EBSQL_VCARD_SORT_KEY         "X-EVOLUTION-SORT-KEY"

/* Suffixes for column names used to store specialized data */
#define EBSQL_SUFFIX_REVERSE         "reverse"
#define EBSQL_SUFFIX_SORT_KEY        "localized"
#define EBSQL_SUFFIX_PHONE           "phone"
#define EBSQL_SUFFIX_COUNTRY         "country"

/* Track EBookIndexType's in a bit mask  */
#define INDEX_FLAG(type)  (1 << E_BOOK_INDEX_##type)

/* This macro is used to reffer to vcards in statements */
#define EBSQL_VCARD_FRAGMENT(ebsql)					\
	((ebsql)->priv->vcard_callback ?				\
	 EBSQL_FUNC_FETCH_VCARD " (summary.uid, summary.bdata)" :	\
	 "summary.vcard")

/* Signatures of SQLite callbacks, just for reference */
typedef gint (* EbSqlCollateFunc)    (gpointer         ref,
				      gint             len1,
				      const void      *str1,
				      gint             len2,
				      const void      *str2);
typedef void (* EbSqlGenCollateFunc) (gpointer         ref,
				      sqlite3         *db,
				      gint             eTextRep,
				      const gchar     *coll_name);
typedef void (* EbSqlCustomFunc)     (sqlite3_context *context,
				      gint             argc,
				      sqlite3_value  **argv);
typedef gint (* EbSqlRowFunc)        (gpointer         ref,
				      gint             n_cols,
				      gchar          **cols,
				      gchar          **names);

/* Some forward declarations */
static gboolean      ebsql_init_statements      (EBookSqlite *ebsql,
						 GError **error);
static gboolean      ebsql_insert_contact       (EBookSqlite *ebsql,
						 EContact *contact,
						 const gchar *extra,
						 gboolean replace_existing,
						 gboolean *e164_changed,
						 GError **error);
static gboolean      ebsql_exec                 (EBookSqlite *ebsql,
						 const gchar *stmt,
						 EbSqlRowFunc callback,
						 gpointer data,
						 GError **error);

typedef struct {
	EContactField field_id;           /* The EContact field */
	GType         type;               /* The GType (only support string or gboolean) */
	const gchar  *dbname;             /* The key for this field in the sqlite3 table */
	gint          index;              /* Types of searches this field should support (see EBookIndexType) */
	gchar        *aux_table;          /* Name of auxiliary table for this field, for multivalued fields only */
	gchar        *aux_table_symbolic; /* Symolic name of auxiliary table used in queries */
} SummaryField;

struct _EBookSqlitePrivate {

	/* Parameters and settings */
	gchar          *path;            /* Full file name of the file we're operating on (used for hash table entries) */
	gchar          *locale;          /* The current locale */
	gchar          *region_code;     /* Region code (for phone number parsing) */
	gchar          *folderid;        /* The summary table name (configurable, for support of legacy
					  * databases created by EBookSqliteDB) */

	/* For shallow addressbooks, this callback 
	 * can be used to generate a vcard on the fly
	 */
	EbSqlVCardCallback vcard_callback;
	gpointer           vcard_user_data;

	/* Summary configuration */
	SummaryField   *summary_fields;
	gint            n_summary_fields;

	GMutex          lock;            /* Main API lock */
	GMutex          updates_lock;    /* Lock used for calls to e_book_sqlite_lock_updates () */
	guint32         in_transaction;  /* Nested transaction counter */
	gboolean        writer_lock;     /* Whether a writer lock was acquired for this transaction */

	ECollator      *collator;        /* The ECollator to create sort keys for any sortable fields */

	/* SQLite resources  */
	sqlite3        *db;
	sqlite3_stmt   *insert_stmt;     /* Insert statement for main summary table */
	sqlite3_stmt   *replace_stmt;    /* Replace statement for main summary table */
	GHashTable     *multi_deletes;   /* Delete statement for each auxiliary table */
	GHashTable     *multi_inserts;   /* Insert statement for each auxiliary table */
};

G_DEFINE_TYPE (EBookSqlite, e_book_sqlite, G_TYPE_OBJECT)
G_DEFINE_QUARK (e-book-backend-sqlite-error-quark,
		e_book_sqlite_error)

/* The ColumnInfo struct is used to constant data
 * and dynamically allocated data, the 'type' and
 * 'extra' members are however always constant.
 */
typedef struct {
	gchar       *name;
	const gchar *type;
	const gchar *extra;
	gchar       *index;
} ColumnInfo;

static ColumnInfo main_table_columns[] = {
	{ (gchar *) "folder_id",       "TEXT",      "PRIMARY KEY", NULL },
	{ (gchar *) "version",         "INTEGER",    NULL,         NULL },
	{ (gchar *) "multivalues",     "TEXT",       NULL,         NULL },
	{ (gchar *) "lc_collate",      "TEXT",       NULL,         NULL },
	{ (gchar *) "countrycode",     "VARCHAR(2)", NULL,         NULL },
};

/* Default summary configuration */
static EContactField default_summary_fields[] = {
	E_CONTACT_UID,
	E_CONTACT_REV,
	E_CONTACT_FILE_AS,
	E_CONTACT_NICKNAME,
	E_CONTACT_FULL_NAME,
	E_CONTACT_GIVEN_NAME,
	E_CONTACT_FAMILY_NAME,
	E_CONTACT_EMAIL,
	E_CONTACT_TEL,
	E_CONTACT_IS_LIST,
	E_CONTACT_LIST_SHOW_ADDRESSES,
	E_CONTACT_WANTS_HTML
};

/* Create indexes on full_name and email fields as autocompletion 
 * queries would mainly rely on this.
 *
 * Add sort keys for name fields as those are likely targets for
 * cursor usage.
 */
static EContactField default_indexed_fields[] = {
	E_CONTACT_FULL_NAME,
	E_CONTACT_EMAIL,
	E_CONTACT_TEL,
	E_CONTACT_FILE_AS,
	E_CONTACT_FAMILY_NAME,
	E_CONTACT_GIVEN_NAME
};

static EBookIndexType default_index_types[] = {
	E_BOOK_INDEX_PREFIX,
	E_BOOK_INDEX_PREFIX,
	E_BOOK_INDEX_PREFIX,
	E_BOOK_INDEX_SORT_KEY,
	E_BOOK_INDEX_SORT_KEY,
	E_BOOK_INDEX_SORT_KEY
};

/******************************************************
 *                  Summary Fields                    *
 ******************************************************/
static ColumnInfo *
column_info_new (SummaryField *field,
		 const gchar  *folderid,
		 const gchar  *column_suffix,
		 const gchar  *column_type,
		 const gchar  *column_extra,
		 const gchar  *idx_prefix)
{
	ColumnInfo *info;

	info        = g_slice_new0 (ColumnInfo);
	info->type  = column_type;
	info->extra = column_extra;

	if (!info->type) {

		if (field->type == G_TYPE_STRING)
			info->type = "TEXT";
		else if (field->type == G_TYPE_BOOLEAN)
			info->type = "INTEGER";
		else
			g_warn_if_reached ();
	}

	if (field->type == E_TYPE_CONTACT_ATTR_LIST)
		/* Attribute lists are on their own table  */
		info->name = g_strconcat ("value",
					  column_suffix ? "_" : NULL,
					  column_suffix,
					  NULL);
	else
		/* Regular fields are named by their 'dbname' */
		info->name = g_strconcat (field->dbname,
					  column_suffix ? "_" : NULL,
					  column_suffix,
					  NULL);

	if (idx_prefix)
		info->index = 
			g_strconcat (idx_prefix,
				     "_", field->dbname,
				     "_", folderid,
				     NULL);

	return info;
}

static void
column_info_free (ColumnInfo *info)
{
	if (info) {
		g_free (info->name);
		g_free (info->index);
		g_slice_free (ColumnInfo, info);
	}
}

static gint
summary_field_array_index (GArray *array,
			   EContactField field)
{
	gint i;

	for (i = 0; i < array->len; i++) {
		SummaryField *iter = &g_array_index (array, SummaryField, i);
		if (field == iter->field_id)
			return i;
	}

	return -1;
}

static SummaryField *
summary_field_append (GArray *array,
		      const gchar *folderid,
                      EContactField field_id,
                      GError **error)
{
	const gchar *dbname = NULL;
	GType        type = G_TYPE_INVALID;
	gint         idx;
	SummaryField new_field = { 0, };

	if (field_id < 1 || field_id >= E_CONTACT_FIELD_LAST) {
		EBSQL_SET_ERROR (error, E_BOOK_SQL_ERROR_OTHER,
				 _("Invalid contact field '%d' specified in summary"),
				 field_id);
		return NULL;
	}

	/* Avoid including the same field twice in the summary */
	idx = summary_field_array_index (array, field_id);
	if (idx >= 0)
		return &g_array_index (array, SummaryField, idx);

	/* Resolve some exceptions, we store these
	 * specific contact fields with different names
	 * than those found in the EContactField table
	 */
	switch (field_id) {
	case E_CONTACT_UID:
		dbname = "uid";
		break;
	case E_CONTACT_IS_LIST:
		dbname = "is_list";
		break;
	default:
		dbname = e_contact_field_name (field_id);
		break;
	}

	type = e_contact_field_type (field_id);

	if (type != G_TYPE_STRING &&
	    type != G_TYPE_BOOLEAN &&
	    type != E_TYPE_CONTACT_ATTR_LIST) {
		EBSQL_SET_ERROR (error, E_BOOK_SQL_ERROR_OTHER,
				 _("Contact field '%s' of type '%s' specified in summary, "
				   "but only boolean, string and string list field types are supported"),
				 e_contact_pretty_name (field_id), g_type_name (type));
		return NULL;
	}

	if (type == E_TYPE_CONTACT_ATTR_LIST) {
		new_field.aux_table = g_strconcat (folderid, "_", dbname, "_list", NULL);
		new_field.aux_table_symbolic = g_strconcat (dbname, "_list", NULL);
	}

	new_field.field_id = field_id;
	new_field.dbname   = dbname;
	new_field.type     = type;
	new_field.index    = 0;
	g_array_append_val (array, new_field);

	return &g_array_index (array, SummaryField, array->len - 1);
}

static gboolean
summary_field_remove (GArray *array,
                      EContactField field)
{
	gint idx;

	idx = summary_field_array_index (array, field);
	if (idx < 0)
		return FALSE;

	g_array_remove_index_fast (array, idx);
	return TRUE;
}

static void
summary_fields_add_indexes (GArray *array,
                            EContactField *indexes,
                            EBookIndexType *index_types,
                            gint n_indexes)
{
	gint i, j;

	for (i = 0; i < array->len; i++) {
		SummaryField *sfield = &g_array_index (array, SummaryField, i);

		for (j = 0; j < n_indexes; j++) {
			if (sfield->field_id == indexes[j])
				sfield->index |= (1 << index_types[j]);

		}
	}
}

static SummaryField *
summary_field_get (EBookSqlite *ebsql,
		   EContactField field_id)
{
	gint i;

	for (i = 0; i < ebsql->priv->n_summary_fields; i++) {
		if (ebsql->priv->summary_fields[i].field_id == field_id)
			return &(ebsql->priv->summary_fields[i]);
	}

	return NULL;
}

static GSList *
summary_field_list_main_columns (SummaryField *field,
				 const gchar *folderid)
{
	GSList *columns = NULL;
	ColumnInfo *info;

	/* Only strings and booleans are supported in the main
	 * summary, string lists are supported in the auxiliary tables
	 */
	if (field->type != G_TYPE_STRING &&
	    field->type != G_TYPE_BOOLEAN)
		return NULL;

	/* Normal / default column */
	info = column_info_new (field, folderid, NULL, NULL,
				(field->field_id == E_CONTACT_UID) ? "PRIMARY KEY" : NULL,
				(field->index & INDEX_FLAG (PREFIX)) != 0 ? "INDEX" : NULL);
	columns = g_slist_prepend (columns, info);

	/* Localized column, for storing sort keys */
	if (field->type == G_TYPE_STRING && (field->index & INDEX_FLAG (SORT_KEY))) {
		info    = column_info_new (field, folderid, EBSQL_SUFFIX_SORT_KEY, "TEXT", NULL, "SINDEX");
		columns = g_slist_prepend (columns, info);
	}

	/* Suffix match column */
	if (field->type == G_TYPE_STRING && (field->index & INDEX_FLAG (SUFFIX)) != 0) {
		info    = column_info_new (field, folderid, EBSQL_SUFFIX_REVERSE, "TEXT", NULL, "RINDEX");
		columns = g_slist_prepend (columns, info);
	}

	/* Phone match column */
	if (field->type == G_TYPE_STRING && (field->index & INDEX_FLAG (PHONE)) != 0) {

		/* One indexed column for storing the national number */
		info    = column_info_new (field, folderid, EBSQL_SUFFIX_PHONE, "TEXT", NULL, "PINDEX");
		columns = g_slist_prepend (columns, info);

		/* One integer column for storing the country code */
		info    = column_info_new (field, folderid, EBSQL_SUFFIX_COUNTRY, "INTEGER", "DEFAULT 0", NULL);
		columns = g_slist_prepend (columns, info);
	}

	return g_slist_reverse (columns);
}

static GSList *
summary_field_list_aux_columns (SummaryField *field,
				const gchar *folderid)
{
	GSList *columns = NULL;
	ColumnInfo *info;

	if (field->type != E_TYPE_CONTACT_ATTR_LIST)
		return NULL;

	/* The UID column of any auxiliary table is implied and references
	 * the UID in the main summary table
	 */

	/* Normalized value column, for prefix and other regular searches */
	info = column_info_new (field, folderid, NULL, "TEXT", NULL,
				(field->index & INDEX_FLAG (PREFIX)) != 0 ? "INDEX" : NULL);
	columns = g_slist_prepend (columns, info);

	/* Suffix match column */
	if ((field->index & INDEX_FLAG (SUFFIX)) != 0) {

		info    = column_info_new (field, folderid, EBSQL_SUFFIX_REVERSE, "TEXT", NULL, "RINDEX");
		columns = g_slist_prepend (columns, info);
	}

	/* Phone match column */
	if ((field->index & INDEX_FLAG (PHONE)) != 0) {

		/* One indexed column for storing the national number */
		info    = column_info_new (field, folderid, EBSQL_SUFFIX_PHONE, "TEXT", NULL, "PINDEX");
		columns = g_slist_prepend (columns, info);

		/* One integer column for storing the country code */
		info    = column_info_new (field, folderid, EBSQL_SUFFIX_COUNTRY, "INTEGER", "DEFAULT 0", NULL);
		columns = g_slist_prepend (columns, info);
	}

	return g_slist_reverse (columns);
}

static void
summary_fields_array_free (SummaryField *fields,
			   gint n_fields)
{
	gint i;

	for (i = 0; i < n_fields; i++) {
		g_free (fields[i].aux_table);
		g_free (fields[i].aux_table_symbolic);
	}

	g_free (fields);
}

/******************************************************
 *        Sharing EBookSqlite instances        *
 ******************************************************/
static GHashTable *db_connections = NULL;
static GMutex dbcon_lock;

static EBookSqlite *
ebsql_ref_from_hash (const gchar *path)
{
	EBookSqlite *ebsql = NULL;

	if (db_connections != NULL) {
		ebsql = g_hash_table_lookup (db_connections, path);
	}

	if (ebsql)
		return g_object_ref (ebsql);

	return NULL;
}

static void
ebsql_register_to_hash (EBookSqlite *ebsql,
			const gchar *path)
{
	if (db_connections == NULL)
		db_connections = g_hash_table_new_full (
			(GHashFunc) g_str_hash,
			(GEqualFunc) g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) NULL);
	g_hash_table_insert (db_connections, g_strdup (path), ebsql);
}

static void
ebsql_unregister_from_hash (EBookSqlite *ebsql)
{
	EBookSqlitePrivate *priv = ebsql->priv;

	EBSQL_LOCK_MUTEX (&dbcon_lock);
	if (db_connections != NULL) {
		if (priv->path != NULL) {
			g_hash_table_remove (db_connections, priv->path);

			if (g_hash_table_size (db_connections) == 0) {
				g_hash_table_destroy (db_connections);
				db_connections = NULL;
			}

		}
	}
	EBSQL_UNLOCK_MUTEX (&dbcon_lock);
}

/************************************************************
 *                SQLite helper functions                   *
 ************************************************************/

/* For EBSQL_DEBUG_EXPLAIN */
static gint
ebsql_debug_query_plan_cb (gpointer ref,
			   gint n_cols,
			   gchar **cols,
			   gchar **name)
{
	gint i;

	for (i = 0; i < n_cols; i++) {
		if (strcmp (name[i], "detail") == 0) {
			g_printerr ("  PLAN: %s\n", cols[i]);
			break;
		}
	}

	return 0;
}

/* Collect a GList of column names in the main summary table */
static gint
get_columns_cb (gpointer ref,
		gint col,
		gchar **cols,
		gchar **name)
{
	GSList **columns = (GSList **) ref;
	gint i;

	for (i = 0; i < col; i++) {
		if (strcmp (name[i], "name") == 0) {

			/* Keep comparing for the legacy 'bdata' column */
			if (strcmp (cols[i], "vcard") != 0 &&
			    strcmp (cols[i], "bdata") != 0) {
				gchar *column = g_strdup (cols[i]);

				*columns = g_slist_prepend (*columns, column);
			}
			break;
		}
	}
	return 0;
}

/* Collect the first string result */
static gint
get_string_cb (gpointer ref,
               gint col,
               gchar **cols,
               gchar **name)
{
	gchar **ret = ref;

	*ret = g_strdup (cols [0]);

	return 0;
}

/* Collect the first integer result */
static gint
get_int_cb (gpointer ref,
	    gint col,
	    gchar **cols,
	    gchar **name)
{
	gint *ret = ref;

	*ret = cols [0] ? g_ascii_strtoll (cols[0], NULL, 10) : 0;

	return 0;
}

/* Collect the result of a SELECT count(*) statement */
static gint
get_count_cb (gpointer ref,
              gint n_cols,
              gchar **cols,
              gchar **name)
{
	gint64 count = 0;
	gint *ret = ref;
	gint i;

	for (i = 0; i < n_cols; i++) {
		if (name[i] && strncmp (name[i], "count", 5) == 0) {
			count = g_ascii_strtoll (cols[i], NULL, 10);

			break;
		}
	}

	*ret = count;

	return 0;
}

/* Report if there was at least one result */
static gint
get_exists_cb (gpointer ref,
	       gint col,
	       gchar **cols,
	       gchar **name)
{
	gboolean *exists = ref;

	*exists = TRUE;

	return 0;
}

static EbSqlSearchData *
search_data_from_results (gint ncol,
			  gchar **cols,
			  gchar **names)
{
	EbSqlSearchData *data = g_slice_new0 (EbSqlSearchData);
	gint i;

	for (i = 0; i < ncol; i++) {

		if (!names[i] || !cols[i])
			continue;

		/* XXX Do we need to check summary.uid, summary.vcard here ? */
		if (!g_ascii_strcasecmp (names[i], "uid")) {
			data->uid = g_strdup (cols[i]);
		} else if (!g_ascii_strcasecmp (names[i], "vcard")) {
			data->vcard = g_strdup (cols[i]);
		} else if (!g_ascii_strcasecmp (names[i], "bdata")) {
			data->extra = g_strdup (cols[i]);
		}
	}

	return data;
}

static gint
collect_full_results_cb (gpointer ref,
			 gint ncol,
			 gchar **cols,
			 gchar **names)
{
	EbSqlSearchData *data;
	GSList **vcard_data = ref;

	data = search_data_from_results (ncol, cols, names);

	*vcard_data = g_slist_prepend (*vcard_data, data);

	return 0;
}

static gint
collect_uid_results_cb (gpointer ref,
			gint ncol,
			gchar **cols,
			gchar **names)
{
	GSList **uids = ref;

	if (cols[0])
		*uids = g_slist_prepend (*uids, g_strdup (cols [0]));

	return 0;
}

static gint
collect_lean_results_cb (gpointer ref,
			 gint ncol,
			 gchar **cols,
			 gchar **names)
{
	GSList **vcard_data = ref;
	EbSqlSearchData *search_data = g_slice_new0 (EbSqlSearchData);
	EContact *contact = e_contact_new ();
	gchar *vcard;
	gint i;

	/* parse through cols, this will be useful if the api starts supporting field restrictions */
	for (i = 0; i < ncol; i++) {
		if (!names[i] || !cols[i])
			continue;

		/* Only UID & REV can be used to create contacts from the summary columns */
		if (!g_ascii_strcasecmp (names[i], "uid")) {
			e_contact_set (contact, E_CONTACT_UID, cols[i]);
			search_data->uid = g_strdup (cols[i]);
		} else if (!g_ascii_strcasecmp (names[i], "Rev")) {
			e_contact_set (contact, E_CONTACT_REV, cols[i]);
		} else if (!g_ascii_strcasecmp (names[i], "bdata")) {
			search_data->extra = g_strdup (cols[i]);
		}
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	search_data->vcard = vcard;
	*vcard_data = g_slist_prepend (*vcard_data, search_data);

	g_object_unref (contact);
	return 0;
}

static gint
collect_uids_and_rev_cb (gpointer user_data,
			 gint col,
			 gchar **cols,
			 gchar **name)
{
	GHashTable *uids_and_rev = user_data;

	if (col == 2 && cols[0])
		g_hash_table_insert (uids_and_rev, g_strdup (cols[0]), g_strdup (cols[1] ? cols[1] : ""));

	return 0;
}

static void
ebsql_string_append_vprintf (GString *string,
			     const gchar *fmt,
			     va_list args)
{
	gchar *stmt;

	/* Unfortunately, sqlite3_vsnprintf() doesnt tell us
	 * how many bytes it would have needed if it doesnt fit
	 * into the target buffer, so we can't avoid this
	 * really disgusting memory dup.
	 */
	stmt = sqlite3_vmprintf (fmt, args);
	g_string_append (string, stmt);
	sqlite3_free (stmt);
}

static void
ebsql_string_append_printf (GString *string,
			    const gchar *fmt,
			    ...)
{
	va_list args;

	va_start (args, fmt);
	ebsql_string_append_vprintf (string, fmt, args);
	va_end (args);
}

/* Appends an identifier suitable to identify the
 * column to test in the context of a query.
 *
 * The suffix is for special indexed columns (such as
 * reverse values, sort keys, phone numbers, etc).
 */
static void
ebsql_string_append_column (GString *string,
			    SummaryField *field,
			    const gchar *suffix)
{
	if (field->aux_table) {
		g_string_append (string, field->aux_table_symbolic);
		g_string_append (string, ".value");
	} else {
		g_string_append (string, "summary.");
		g_string_append (string, field->dbname);
	}

	if (suffix) {
		g_string_append_c (string, '_');
		g_string_append (string, suffix);
	}
}

static gboolean
ebsql_exec_vprintf (EBookSqlite *ebsql,
		    const gchar *fmt,
		    EbSqlRowFunc callback,
		    gpointer data,
		    GError **error,
		    va_list args)
{
	gboolean success;
	gchar *stmt;

	stmt = sqlite3_vmprintf (fmt, args);
	success = ebsql_exec (ebsql, stmt, callback, data, error);
	sqlite3_free (stmt);

	return success;
}

static gboolean
ebsql_exec_printf (EBookSqlite *ebsql,
		   const gchar *fmt,
		   EbSqlRowFunc callback,
		   gpointer data,
		   GError **error,
		   ...)
{
	gboolean success;
	va_list args;

	va_start (args, error);
	success = ebsql_exec_vprintf (ebsql, fmt, callback, data, error, args);
	va_end (args);

	return success;
}

static inline void
ebsql_exec_maybe_debug (EBookSqlite *ebsql,
			const gchar *stmt)
{
	if (ebsql_debug_flags & EBSQL_DEBUG_EXPLAIN &&
	    strncmp (stmt, "SELECT", 6) == 0) {
		    g_printerr ("EXPLAIN BEGIN\n  STMT: %s\n", stmt);
		    ebsql_exec_printf (ebsql, "EXPLAIN QUERY PLAN %s",
				       ebsql_debug_query_plan_cb,
				       NULL, NULL, stmt);
		    g_printerr ("EXPLAIN END\n");
	} else {
		EBSQL_NOTE (STATEMENTS, g_printerr ("STMT: %s\n", stmt));
	}
}

static gboolean
ebsql_exec (EBookSqlite *ebsql,
	    const gchar *stmt,
	    EbSqlRowFunc callback,
	    gpointer data,
	    GError **error)
{
	gchar *errmsg = NULL;
	gint ret = -1;

	/* Debug output for statements and query plans */
	ebsql_exec_maybe_debug (ebsql, stmt);

	ret = sqlite3_exec (ebsql->priv->db, stmt, callback, data, &errmsg);

	while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED || ret == -1) {
		if (errmsg) {
			sqlite3_free (errmsg);
			errmsg = NULL;
		}
		g_thread_yield ();
		ret = sqlite3_exec (ebsql->priv->db, stmt, callback, data, &errmsg);
	}

	if (ret != SQLITE_OK) {
		EBSQL_SET_ERROR_LITERAL (error,
					 ret == SQLITE_CONSTRAINT ?
					 E_BOOK_SQL_ERROR_CONSTRAINT :
					 E_BOOK_SQL_ERROR_OTHER,
					 errmsg);
		sqlite3_free (errmsg);
		return FALSE;
	}

	if (errmsg)
		sqlite3_free (errmsg);

	return TRUE;
}

static gboolean
ebsql_start_transaction (EBookSqlite *ebsql,
			 gboolean writer_lock,
			 GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (ebsql != NULL, FALSE);
	g_return_val_if_fail (ebsql->priv != NULL, FALSE);
	g_return_val_if_fail (ebsql->priv->db != NULL, FALSE);

	ebsql->priv->in_transaction++;
	g_return_val_if_fail (ebsql->priv->in_transaction > 0, FALSE);

	if (ebsql->priv->in_transaction == 1) {

		/* It's important to make the distinction between a
		 * transaction which will read or one which will write.
		 *
		 * While it's not well documented, when receiving the SQLITE_BUSY
		 * error status, one can only safely retry at the beginning of
		 * the transaction.
		 *
		 * If a transaction is 'upgraded' to require a writer lock
		 * half way through the transaction and SQLITE_BUSY is returned,
		 * the whole transaction would need to be retried from the beginning.
		 */
		ebsql->priv->writer_lock = writer_lock;

		success = ebsql_exec (ebsql, writer_lock ? "BEGIN IMMEDIATE" : "BEGIN",
				      NULL, NULL, error);
	} else {

		/* Warn about cases where where a read transaction might be upgraded */
		if (writer_lock && !ebsql->priv->writer_lock)
			g_warning ("A nested transaction wants to write, "
				   "but the outermost transaction was started "
				   "without a writer lock.");
	}

	return success;
}

static gboolean
ebsql_commit_transaction (EBookSqlite *ebsql,
			  GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (ebsql != NULL, FALSE);
	g_return_val_if_fail (ebsql->priv != NULL, FALSE);
	g_return_val_if_fail (ebsql->priv->db != NULL, FALSE);

	g_return_val_if_fail (ebsql->priv->in_transaction > 0, FALSE);

	ebsql->priv->in_transaction--;

	if (ebsql->priv->in_transaction == 0)
		success = ebsql_exec (ebsql, "COMMIT", NULL, NULL, error);

	return success;
}

static gboolean
ebsql_rollback_transaction (EBookSqlite *ebsql,
			    GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (ebsql != NULL, FALSE);
	g_return_val_if_fail (ebsql->priv != NULL, FALSE);
	g_return_val_if_fail (ebsql->priv->db != NULL, FALSE);

	g_return_val_if_fail (ebsql->priv->in_transaction > 0, FALSE);

	ebsql->priv->in_transaction--;

	if (ebsql->priv->in_transaction == 0)
		success = ebsql_exec (ebsql, "ROLLBACK", NULL, NULL, error);

	return success;
}

static sqlite3_stmt *
ebsql_prepare_statement (EBookSqlite *ebsql,
			 const gchar *stmt_str,
			 GError **error)
{
	sqlite3_stmt *stmt;
	const gchar *stmt_tail = NULL;
	gint ret;

	ret = sqlite3_prepare_v2 (ebsql->priv->db, stmt_str, strlen (stmt_str), &stmt, &stmt_tail);

	if (ret != SQLITE_OK) {
		const gchar *errmsg = sqlite3_errmsg (ebsql->priv->db);
		EBSQL_SET_ERROR_LITERAL (error,
					 E_BOOK_SQL_ERROR_OTHER,
					 errmsg);
	} else if (stmt == NULL) {
		EBSQL_SET_ERROR_LITERAL (error,
					 E_BOOK_SQL_ERROR_OTHER,
					 "Unknown error preparing SQL statement");
	}

	if (stmt_tail && stmt_tail[0])
		g_warning ("Part of this statement was not parsed: %s", stmt_tail);

	return stmt;
}

/* Convenience for running statements. After successfully
 * binding all parameters, just return with this.
 */
static gboolean
ebsql_complete_statement (EBookSqlite *ebsql,
			  sqlite3_stmt *stmt,
			  gint ret,
			  GError **error)
{
	if (ret == SQLITE_OK) {
		ret = sqlite3_step (stmt);

		if (ret == SQLITE_DONE)
			ret = SQLITE_OK;
	}

	if (ret != SQLITE_OK) {
		const gchar *errmsg = sqlite3_errmsg (ebsql->priv->db);
		EBSQL_SET_ERROR_LITERAL (error,
					 ret == SQLITE_CONSTRAINT ?
					 E_BOOK_SQL_ERROR_CONSTRAINT : E_BOOK_SQL_ERROR_OTHER,
					 errmsg);
	} 

	/* Reset / Clear at the end, regardless of error state */
	sqlite3_reset (stmt);
	sqlite3_clear_bindings (stmt);

	return (ret == SQLITE_OK);
}

/******************************************************
 *       Functions installed into the SQLite          *
 ******************************************************/

/* Implementation for REGEXP keyword */
static void
ebsql_regexp (sqlite3_context *context,
              gint argc,
              sqlite3_value **argv)
{
	GRegex *regex;
	const gchar *expression;
	const gchar *text;

	/* Reuse the same GRegex for all REGEXP queries with the same expression */
	regex = sqlite3_get_auxdata (context, 0);
	if (!regex) {
		GError *error = NULL;

		expression = (const gchar *) sqlite3_value_text (argv[0]);

		regex = g_regex_new (expression, 0, 0, &error);

		if (!regex) {
			sqlite3_result_error (context,
					      error ? error->message :
					      _("Error parsing regular expression"),
					      -1);
			g_clear_error (&error);
			return;
		}

		/* SQLite will take care of freeing the GRegex when we're done with the query */
		sqlite3_set_auxdata (context, 0, regex, (GDestroyNotify)g_regex_unref);
	}

	/* Now perform the comparison */
	text = (const gchar *) sqlite3_value_text (argv[1]);
	if (text != NULL) {
		gboolean match;

		match = g_regex_match (regex, text, 0, NULL);
		sqlite3_result_int (context, match ? 1 : 0);
	}
}

/* Implementation of EBSQL_FUNC_COMPARE_VCARD (fallback for non-summary queries) */
static void
ebsql_compare_vcard (sqlite3_context *context,
		     gint argc,
		     sqlite3_value **argv)
{
	EBookBackendSExp *sexp = NULL;
	const gchar *text;
	const gchar *vcard;

	/* Reuse the same sexp for all queries with the same search expression */
	sexp = sqlite3_get_auxdata (context, 0);
	if (!sexp) {

		/* The first argument will be reused for many rows */
		text = (const gchar *) sqlite3_value_text (argv[0]);
		if (text) {
			sexp = e_book_backend_sexp_new (text);
			sqlite3_set_auxdata (context, 0,
					     sexp,
					     g_object_unref);
		}

		/* This shouldn't happen, catch invalid sexp in preflight */
		if (!sexp) {
			sqlite3_result_int (context, 0);
			return;
		}

	}

	/* Reuse the same vcard as much as possible (it can be referred to more than
	 * once in the query, so it can be reused for multiple comparisons on the same row)
	 *
	 * This may look extensive, but as the vcard might be resolved by calling a
	 * EbSqlVCardCallback, it's important to reuse this string as much as possible.
	 *
	 * See ebsql_fetch_vcard() for details.
	 */
	vcard = sqlite3_get_auxdata (context, 1);
	if (!vcard) {
		vcard = (const gchar *) sqlite3_value_text (argv[1]);

		if (vcard)
			sqlite3_set_auxdata (context, 1, g_strdup (vcard), g_free);
	}

	/* A NULL vcard can never match */
	if (vcard == NULL || *vcard == '\0') {
		sqlite3_result_int (context, 0);
		return;
	}

	/* Compare this vcard */
	if (e_book_backend_sexp_match_vcard (sexp, vcard))
		sqlite3_result_int (context, 1);
	else
		sqlite3_result_int (context, 0);
}

static void
ebsql_eqphone (sqlite3_context *context,
	       gint argc,
	       sqlite3_value **argv,
	       EPhoneNumberMatch requested_match)
{
	EBookSqlite *ebsql = sqlite3_user_data (context);
	EPhoneNumber *input_phone = NULL, *row_phone = NULL;
	EPhoneNumberMatch match = E_PHONE_NUMBER_MATCH_NONE;
	const gchar *text;

	/* Reuse the same phone number for all queries with the same phone number argument */
	input_phone = sqlite3_get_auxdata (context, 0);
	if (!input_phone) {

		/* The first argument will be reused for many rows */
		text = (const gchar *) sqlite3_value_text (argv[0]);
		if (text) {

			/* Ignore errors, they are fine for phone numbers */
			input_phone = e_phone_number_from_string (text, ebsql->priv->region_code, NULL);

			/* SQLite will take care of freeing the EPhoneNumber when we're done with the expression */
			if (input_phone)
				sqlite3_set_auxdata (context, 0,
						     input_phone,
						     (GDestroyNotify)e_phone_number_free);
		}
	}

	/* This shouldn't happen, as we catch invalid phone number queries in preflight
	 */
	if (!input_phone) {
		sqlite3_result_int (context, 0);
		return;
	}

	/* Parse the phone number for this row */
	text = (const gchar *) sqlite3_value_text (argv[1]);
	if (text != NULL) {
		row_phone = e_phone_number_from_string (text, ebsql->priv->region_code, NULL);

		/* And perform the comparison */
		if (row_phone) {
			match = e_phone_number_compare (input_phone, row_phone);

			e_phone_number_free (row_phone);
		}
	}

	/* Now report the result */
	if (match != E_PHONE_NUMBER_MATCH_NONE &&
	    match <= requested_match)
		sqlite3_result_int (context, 1);
	else
		sqlite3_result_int (context, 0);
}

/* Exact phone number match function: EBSQL_FUNC_EQPHONE_EXACT */
static void
ebsql_eqphone_exact (sqlite3_context *context,
		     gint argc,
		     sqlite3_value **argv)
{
	ebsql_eqphone (context, argc, argv, E_PHONE_NUMBER_MATCH_EXACT);
}

/* National phone number match function: EBSQL_FUNC_EQPHONE_NATIONAL */
static void
ebsql_eqphone_national (sqlite3_context *context,
			gint argc,
			sqlite3_value **argv)
{
	ebsql_eqphone (context, argc, argv, E_PHONE_NUMBER_MATCH_NATIONAL);
}

/* Short phone number match function: EBSQL_FUNC_EQPHONE_SHORT */
static void
ebsql_eqphone_short (sqlite3_context *context,
		     gint argc,
		     sqlite3_value **argv)
{
	ebsql_eqphone (context, argc, argv, E_PHONE_NUMBER_MATCH_SHORT);
}

/* Implementation of EBSQL_FUNC_FETCH_VCARD (fallback for shallow addressbooks) */
static void
ebsql_fetch_vcard (sqlite3_context *context,
		   gint argc,
		   sqlite3_value **argv)
{
	EBookSqlite *ebsql = sqlite3_user_data (context);
	const gchar *uid;
	const gchar *extra;
	gchar *vcard = NULL;

	uid   = (const gchar *) sqlite3_value_text (argv[0]);
	extra = (const gchar *) sqlite3_value_text (argv[1]);

	/* Call our delegate to generate the vcard */
	if (ebsql->priv->vcard_callback)
		vcard = ebsql->priv->vcard_callback (uid,
						     extra,
						     ebsql->priv->vcard_user_data);

	sqlite3_result_text (context, vcard, -1, g_free);
}

typedef struct {
	const gchar     *name;
	EbSqlCustomFunc  func;
	gint             arguments;
} EbSqlCustomFuncTab;

static EbSqlCustomFuncTab ebsql_custom_functions[] = {
	{ "regexp",                    ebsql_regexp,           2 }, /* regexp (expression, column_data) */
	{ EBSQL_FUNC_COMPARE_VCARD,    ebsql_compare_vcard,    2 }, /* compare_vcard (sexp, vcard) */
	{ EBSQL_FUNC_FETCH_VCARD,      ebsql_fetch_vcard,      2 }, /* fetch_vcard (uid, extra) */
	{ EBSQL_FUNC_EQPHONE_EXACT,    ebsql_eqphone_exact,    2 }, /* eqphone_exact (search_input, column_data) */
	{ EBSQL_FUNC_EQPHONE_NATIONAL, ebsql_eqphone_national, 2 }, /* eqphone_national (search_input, column_data) */
	{ EBSQL_FUNC_EQPHONE_SHORT,    ebsql_eqphone_short,    2 }, /* eqphone_national (search_input, column_data) */
};

/******************************************************
 *            Fallback Collation Sequences            *
 ******************************************************
 *
 * The fallback simply compares vcards, vcards which have been
 * stored on the cursor will have a preencoded key (these
 * utilities encode & decode that key).
 */
static gchar *
ebsql_encode_vcard_sort_key (const gchar   *sort_key)
{
	EVCard *vcard = e_vcard_new ();
	gchar *base64;
	gchar *encoded;

	/* Encode this otherwise e-vcard messes it up */
	base64 = g_base64_encode ((const guchar *)sort_key, strlen (sort_key));
	e_vcard_append_attribute_with_value (vcard,
					     e_vcard_attribute_new (NULL, EBSQL_VCARD_SORT_KEY),
					     base64);
	encoded = e_vcard_to_string (vcard, EVC_FORMAT_VCARD_30);

	g_free (base64);
	g_object_unref (vcard);

	return encoded;
}

static gchar *
ebsql_decode_vcard_sort_key_from_vcard (EVCard *vcard)
{
	EVCardAttribute *attr;
	GList *values = NULL;
	gchar *sort_key = NULL;
	gchar *base64 = NULL;

	attr = e_vcard_get_attribute (vcard, EBSQL_VCARD_SORT_KEY);
	if (attr)
		values = e_vcard_attribute_get_values (attr);

	if (values && values->data) {
		gsize len;

		base64 = g_strdup (values->data);

		sort_key = (gchar *)g_base64_decode (base64, &len);
		g_free (base64);
	}

	return sort_key;
}

static gchar *
ebsql_decode_vcard_sort_key (const gchar *encoded)
{
	EVCard *vcard;
	gchar *sort_key;

	vcard = e_vcard_new_from_string (encoded);
	sort_key = ebsql_decode_vcard_sort_key_from_vcard (vcard);
	g_object_unref (vcard);

	return sort_key;
}

typedef struct {
	EBookSqlite *ebsql;
	EContactField field;
} EbSqlCollData;

static gint
ebsql_fallback_collator (gpointer         ref,
			 gint             len1,
			 const void      *data1,
			 gint             len2,
			 const void      *data2)
{
	EbSqlCollData *data = (EbSqlCollData *)ref;
	EBookSqlitePrivate *priv;
	EContact *contact1, *contact2;
	const gchar *str1, *str2;
	gchar *key1, *key2;
	gchar *tmp;
	gint result = 0;

	priv = data->ebsql->priv;

	str1 = (const gchar *)data1;
	str2 = (const gchar *)data2;

	/* Construct 2 contacts (we're comparing vcards) */
	contact1 = e_contact_new ();
	contact2 = e_contact_new ();
	e_vcard_construct_full (E_VCARD (contact1), str1, len1, NULL);
	e_vcard_construct_full (E_VCARD (contact2), str2, len2, NULL);

	/* Extract first key */
	key1 = ebsql_decode_vcard_sort_key_from_vcard (E_VCARD (contact1));
	if (!key1) {
		tmp = e_contact_get (contact1, data->field);
		if (tmp)
			key1 = e_collator_generate_key (priv->collator, tmp, NULL);
		g_free (tmp);
	}
	if (!key1)
		key1 = g_strdup ("");

	/* Extract second key */
	key2 = ebsql_decode_vcard_sort_key_from_vcard (E_VCARD (contact2));
	if (!key2) {
		tmp = e_contact_get (contact2, data->field);
		if (tmp)
			key2 = e_collator_generate_key (priv->collator, tmp, NULL);
		g_free (tmp);
	}
	if (!key2)
		key2 = g_strdup ("");

	result = strcmp (key1, key2);

	g_free (key1);
	g_free (key2);
	g_object_unref (contact1);
	g_object_unref (contact2);

	return result;
}

static EbSqlCollData *
ebsql_coll_data_new (EBookSqlite *ebsql,
		     EContactField       field)
{
	EbSqlCollData *data = g_slice_new (EbSqlCollData);

	data->ebsql = ebsql;
	data->field = field;

	return data;
}

static void
ebsql_coll_data_free (EbSqlCollData *data)
{
	if (data)
		g_slice_free (EbSqlCollData, data);
}

/* COLLATE functions are generated on demand only */
static void
ebsql_generate_collator (gpointer         ref,
			 sqlite3         *db,
			 gint             eTextRep,
			 const gchar     *coll_name)
{
	EBookSqlite *ebsql = (EBookSqlite *)ref;
	EbSqlCollData *data;
	EContactField field;
	const gchar *field_name;

	field_name = coll_name + strlen (EBSQL_COLLATE_PREFIX);
	field      = e_contact_field_id (field_name);

	/* This should be caught before reaching here, just an extra check */
	if (field == 0 || field >= E_CONTACT_FIELD_LAST ||
	    e_contact_field_type (field) != G_TYPE_STRING) {
		g_warning ("Specified collation on invalid contact field");
		return;
	}

	data  = ebsql_coll_data_new (ebsql, field);
	sqlite3_create_collation_v2 (db, coll_name, SQLITE_UTF8,
				     data, ebsql_fallback_collator,
				     (GDestroyNotify)ebsql_coll_data_free);
}

/**********************************************************
 *                  Database Initialization               *
 **********************************************************/
static inline gint
main_table_index_by_name (const gchar *name)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (main_table_columns); i++) {
		if (g_strcmp0 (name, main_table_columns[i].name) == 0)
			return i;
	}

	return -1;
}

static gint
check_main_table_columns (gpointer data,
			  gint n_cols,
			  gchar **cols,
			  gchar **name)
{
	guint *columns_mask = (guint *)data;
	gint i;

	for (i = 0; i < n_cols; i++) {

		if (g_strcmp0 (name[i], "name") == 0) {
			gint idx = main_table_index_by_name (cols[i]);

			if (idx >= 0)
				*columns_mask |= (1 << idx);

			break;
		}
	}

	return 0;
}

static gboolean
ebsql_init_sqlite (EBookSqlite *ebsql,
		   const gchar *filename,
		   GError **error)
{
	gint ret, i;

	e_sqlite3_vfs_init ();

	ret = sqlite3_open (filename, &ebsql->priv->db);

	/* Install our custom functions */
	for (i = 0; ret == SQLITE_OK && i < G_N_ELEMENTS (ebsql_custom_functions); i++)
		ret = sqlite3_create_function (
			ebsql->priv->db,
			ebsql_custom_functions[i].name,
			ebsql_custom_functions[i].arguments,
			SQLITE_UTF8, ebsql,
			ebsql_custom_functions[i].func,
			NULL, NULL);

	/* Fallback COLLATE implementations generated on demand */
	if (ret == SQLITE_OK)
		ret = sqlite3_collation_needed (
			ebsql->priv->db, ebsql, ebsql_generate_collator);

	if (ret != SQLITE_OK) {
		if (!ebsql->priv->db) {
			EBSQL_SET_ERROR_LITERAL (error,
						 E_BOOK_SQL_ERROR_OTHER,
						 _("Insufficient memory"));
		} else {
			const gchar *errmsg = sqlite3_errmsg (ebsql->priv->db);

			EBSQL_SET_ERROR (error,
					 E_BOOK_SQL_ERROR_OTHER,
					 "Can't open database %s: %s\n",
					 filename, errmsg);
			sqlite3_close (ebsql->priv->db);
		}
		return FALSE;
	}

	ebsql_exec (ebsql, "ATTACH DATABASE ':memory:' AS mem", NULL, NULL, NULL);
	ebsql_exec (ebsql, "PRAGMA foreign_keys = ON",          NULL, NULL, NULL);
	ebsql_exec (ebsql, "PRAGMA case_sensitive_like = ON",   NULL, NULL, NULL);

	return TRUE;
}

static inline void
format_column_declaration (GString *string,
			   ColumnInfo *info)
{
	g_string_append (string, info->name);
	g_string_append_c (string, ' ');

	g_string_append (string, info->type);

	if (info->extra) {
		g_string_append_c (string, ' ');
		g_string_append (string, info->extra);
	}
}

static inline gboolean
ensure_column_index (EBookSqlite *ebsql,
		     const gchar        *table,
		     ColumnInfo         *info,
		     GError            **error)
{
	if (!info->index)
		return TRUE;

	return ebsql_exec_printf (ebsql,
				  "CREATE INDEX IF NOT EXISTS %Q ON %Q (%s)",
				  NULL, NULL, error,
				  info->index, table, info->name);
}

/* Called with the lock held and inside a transaction */
static gboolean
ebsql_init_folders (EBookSqlite *ebsql,
		    gint *previous_schema,
		    GError **error)
{
	GString *string;
	gint version = 0, i;
	guint existing_columns_mask = 0;
	gboolean success;

	string = g_string_sized_new (COLUMN_DEFINITION_BYTES * G_N_ELEMENTS (main_table_columns));
	g_string_append (string, "CREATE TABLE IF NOT EXISTS folders (");
	for (i = 0; i < G_N_ELEMENTS (main_table_columns); i++) {

		if (i > 0)
			g_string_append (string, ", ");

		format_column_declaration (string, &(main_table_columns[i]));
	}
	g_string_append_c (string, ')');

	/* Create main folders table */
	success = ebsql_exec (ebsql, string->str, NULL, NULL, error);
	g_string_free (string, TRUE);

	/* Fetch the version, it should be the same for all folders (hence the LIMIT). */
	if (success)
		success = ebsql_exec (ebsql, "SELECT version FROM folders LIMIT 1",
				      get_int_cb, &version, error);

	/* Check which columns in the main table already exist */
	if (success)
		success = ebsql_exec (ebsql, "PRAGMA table_info (folders)",
				      check_main_table_columns, &existing_columns_mask, error);

	/* Add columns which may be missing */
	for (i = 0; success && i < G_N_ELEMENTS (main_table_columns); i++) {
		ColumnInfo *info = &(main_table_columns[i]);

		if ((existing_columns_mask & (1 << i)) != 0)
			continue;

		success = ebsql_exec_printf (ebsql, "ALTER TABLE folders ADD COLUMN %s %s %s",
					     NULL, NULL, error, info->name, info->type,
					     info->extra ? info->extra : "");
	}

	/* Special case upgrade for schema versions 3 & 4.
	 * 
	 * Drops the reverse_multivalues column.
	 */
	if (success && version >= 3 && version < 5) {

		success = ebsql_exec (
			ebsql, 
			"UPDATE folders SET "
				"multivalues = REPLACE(RTRIM(REPLACE("
					"multivalues || ':', ':', "
					"CASE reverse_multivalues "
						"WHEN 0 THEN ';prefix ' "
						"ELSE ';prefix;suffix ' "
					"END)), ' ', ':'), "
			        "reverse_multivalues = NULL",
			NULL, NULL, error);
	}

	/* Finish the eventual upgrade by storing the current schema version.
	 */
	if (success && version >= 1 && version < FOLDER_VERSION)
		success = ebsql_exec_printf (ebsql, "UPDATE folders SET version = %d",
					     NULL, NULL, error, FOLDER_VERSION);

	if (success)
		*previous_schema = version;
	else
		*previous_schema = 0;

	return success;
}

/* Called with the lock held and inside a transaction */
static gboolean
ebsql_init_keys (EBookSqlite *ebsql,
		 GError **error)
{
	gboolean success;

	/* Create a child table to store key/value pairs for a folder. */
	success = ebsql_exec (ebsql, 
			      "CREATE TABLE IF NOT EXISTS keys ("
			      " key TEXT PRIMARY KEY,"
			      " value TEXT,"
			      " folder_id TEXT REFERENCES folders)",
			      NULL, NULL, error);

	/* Add an index on the keys */
	if (success)
		success = ebsql_exec (ebsql, 
				      "CREATE INDEX IF NOT EXISTS keysindex ON keys (folder_id)",
				      NULL, NULL, error);

	return success;
}

static gchar *
format_multivalues (EBookSqlite *ebsql)
{
	gint i;
	GString *string;
	gboolean first = TRUE;

	string = g_string_new (NULL);

	for (i = 0; i < ebsql->priv->n_summary_fields; i++) {
		if (ebsql->priv->summary_fields[i].type == E_TYPE_CONTACT_ATTR_LIST) {
			if (first)
				first = FALSE;
			else
				g_string_append_c (string, ':');

			g_string_append (string, ebsql->priv->summary_fields[i].dbname);

			/* E_BOOK_INDEX_SORT_KEY is not supported in the multivalue fields */
			if ((ebsql->priv->summary_fields[i].index & INDEX_FLAG (PREFIX)) != 0)
				g_string_append (string, ";prefix");
			if ((ebsql->priv->summary_fields[i].index & INDEX_FLAG (SUFFIX)) != 0)
				g_string_append (string, ";suffix");
			if ((ebsql->priv->summary_fields[i].index & INDEX_FLAG (PHONE)) != 0)
				g_string_append (string, ";phone");
		}
	}

	return g_string_free (string, FALSE);
}

/* Called with the lock held and inside a transaction */
static gboolean
ebsql_add_folder (EBookSqlite *ebsql,
		  gboolean *already_exists,
		  GError **error)
{
	gboolean success;
	gchar *multivalues;
	gint count = 0;

	/* Check if this folder is already declared in the main folders table */
	success = ebsql_exec_printf (ebsql, 
				     "SELECT count(*) FROM sqlite_master "
				     "WHERE type='table' AND name=%Q;",
				     get_count_cb, &count, error,
				     ebsql->priv->folderid);

	if (success && count == 0) {
		const gchar *lc_collate;

		multivalues = format_multivalues (ebsql);
		lc_collate = setlocale (LC_COLLATE, NULL);

		success = ebsql_exec_printf (
			ebsql,
			"INSERT OR IGNORE INTO folders"
			" ( folder_id, version, multivalues, lc_collate ) "
			"VALUES ( %Q, %d, %Q, %Q ) ",
			NULL, NULL, error,
			ebsql->priv->folderid, FOLDER_VERSION, multivalues, lc_collate);

		g_free (multivalues);
	}

	if (success && already_exists)
		*already_exists = (count > 0);

	return success;
}

/* Called with the lock held and inside a transaction */
static gboolean
ebsql_introspect_summary (EBookSqlite *ebsql,
			  gint previous_schema,
			  GSList **introspected_columns,
			  GError **error)
{
	gboolean success;
	GSList *summary_columns = NULL, *l;
	GArray *summary_fields = NULL;
	gchar *multivalues = NULL;
	gint i, j;

	success = ebsql_exec_printf (
		ebsql, "PRAGMA table_info (%Q);",
		get_columns_cb, &summary_columns, error, ebsql->priv->folderid);

	if (!success)
		goto introspect_summary_finish;

	summary_columns = g_slist_reverse (summary_columns);
	summary_fields = g_array_new (FALSE, FALSE, sizeof (SummaryField));

	/* Introspect the normal summary fields */
	for (l = summary_columns; l; l = l->next) {
		EContactField field_id;
		const gchar *col = l->data;
		gchar *p;
		gint computed = 0;
		gchar *freeme = NULL;

		/* Note that we don't have any way to introspect
		 * E_BOOK_INDEX_PREFIX, this is not important because if
		 * the prefix index is specified, it will be created
		 * the first time the SQLite tables are created, so
		 * it's not important to ensure prefix indexes after
		 * introspecting the summary.
		 */

		/* Check if we're parsing a reverse field */
		if ((p = strstr (col, "_" EBSQL_SUFFIX_REVERSE)) != NULL) {
			computed = INDEX_FLAG (SUFFIX);
			freeme   = g_strndup (col, p - col);
			col      = freeme;
		} else if ((p = strstr (col, "_" EBSQL_SUFFIX_PHONE)) != NULL) {
			computed = INDEX_FLAG (PHONE);
			freeme   = g_strndup (col, p - col);
			col      = freeme;
		} else if ((p = strstr (col, "_" EBSQL_SUFFIX_COUNTRY)) != NULL) {
			computed = INDEX_FLAG (PHONE);
			freeme   = g_strndup (col, p - col);
			col      = freeme;
		} else if ((p = strstr (col, "_" EBSQL_SUFFIX_SORT_KEY)) != NULL) {
			computed = INDEX_FLAG (SORT_KEY);
			freeme   = g_strndup (col, p - col);
			col      = freeme;
		}

		/* First check exception fields */
		if (g_ascii_strcasecmp (col, "uid") == 0)
			field_id = E_CONTACT_UID;
		else if (g_ascii_strcasecmp (col, "is_list") == 0)
			field_id = E_CONTACT_IS_LIST;
		else
			field_id = e_contact_field_id (col);

		/* Check for parse error */
		if (field_id == 0) {
			EBSQL_SET_ERROR (error,
					 E_BOOK_SQL_ERROR_OTHER,
					 _("Error introspecting unknown summary field '%s'"),
					 col);
			success = FALSE;
			g_free (freeme);
			break;
		}

		/* Computed columns are always declared after the normal columns,
		 * if a reverse field is encountered we need to set the suffix
		 * index on the coresponding summary field
		 */
		if (computed) {
			gint field_idx;
			SummaryField *iter;

			field_idx = summary_field_array_index (summary_fields, field_id);
			if (field_idx >= 0) {
				iter = &g_array_index (summary_fields, SummaryField, field_idx);
				iter->index |= computed;
			}

		} else {
			summary_field_append (summary_fields, ebsql->priv->folderid,
					      field_id, NULL);
		}

		g_free (freeme);
	}

	if (!success)
		goto introspect_summary_finish;

	/* Introspect the multivalied summary fields */
	success = ebsql_exec_printf (ebsql,
				     "SELECT multivalues FROM folders "
				     "WHERE folder_id = %Q",
				     get_string_cb, &multivalues, error,
				     ebsql->priv->folderid);

	if (!success)
		goto introspect_summary_finish;


	if (multivalues) {
		gchar **fields = g_strsplit (multivalues, ":", 0);

		for (i = 0; fields[i] != NULL; i++) {
			EContactField field_id;
			SummaryField *iter;
			gchar **params;

			params   = g_strsplit (fields[i], ";", 0);
			field_id = e_contact_field_id (params[0]);
			iter     = summary_field_append (summary_fields,
							 ebsql->priv->folderid,
							 field_id, NULL);

			if (iter) {
				for (j = 1; params[j]; ++j) {
					/* Sort keys not supported for multivalued fields */
					if (strcmp (params[j], "prefix") == 0) {
						iter->index |= INDEX_FLAG (PREFIX);
					} else if (strcmp (params[j], "suffix") == 0) {
						iter->index |= INDEX_FLAG (SUFFIX);
					} else if (strcmp (params[j], "phone") == 0) {
						iter->index |= INDEX_FLAG (PHONE);
					}
				}
			}

			g_strfreev (params);
		}

		g_strfreev (fields);
	}

	/* HARD CODE UP AHEAD
	 *
	 * Now we're finished introspecting, if the summary is from a previous version,
	 * we need to add any summary fields which we're added to the default summary
	 * since the schema version which was introduced here
	 */
	if (previous_schema >= 1) {
		SummaryField *summary_field;

		if (previous_schema < 8) {

			/* We used to keep 4 email fields in the summary, before we supported
			 * the multivaliued E_CONTACT_EMAIL... convert the old summary to use
			 * the multivaliued field instead.
			 */
			if (summary_field_array_index (summary_fields, E_CONTACT_EMAIL_1) >= 0 &&
			    summary_field_array_index (summary_fields, E_CONTACT_EMAIL_2) >= 0 &&
			    summary_field_array_index (summary_fields, E_CONTACT_EMAIL_3) >= 0 &&
			    summary_field_array_index (summary_fields, E_CONTACT_EMAIL_4) >= 0) {

				summary_field_remove (summary_fields, E_CONTACT_EMAIL_1);
				summary_field_remove (summary_fields, E_CONTACT_EMAIL_2);
				summary_field_remove (summary_fields, E_CONTACT_EMAIL_3);
				summary_field_remove (summary_fields, E_CONTACT_EMAIL_4);

				summary_field = summary_field_append (summary_fields,
								      ebsql->priv->folderid,
								      E_CONTACT_EMAIL, NULL);
				summary_field->index |= INDEX_FLAG (PREFIX);
			}

			/* Regardless of whether it was a default summary or not, add the sort
			 * keys to anything less than Schema 8 (as long as those fields are at least
			 * in the summary)
			 */
			if ((i = summary_field_array_index (summary_fields, E_CONTACT_FILE_AS)) >= 0) {
				summary_field = &g_array_index (summary_fields, SummaryField, i);
				summary_field->index |= INDEX_FLAG (SORT_KEY);
			}

			if ((i = summary_field_array_index (summary_fields, E_CONTACT_GIVEN_NAME)) >= 0) {
				summary_field = &g_array_index (summary_fields, SummaryField, i);
				summary_field->index |= INDEX_FLAG (SORT_KEY);
			}

			if ((i = summary_field_array_index (summary_fields, E_CONTACT_FAMILY_NAME)) >= 0) {
				summary_field = &g_array_index (summary_fields, SummaryField, i);
				summary_field->index |= INDEX_FLAG (SORT_KEY);
			}
		}
	}

 introspect_summary_finish:

	/* Apply the introspected summary fields */
	if (success) {
		summary_fields_array_free (ebsql->priv->summary_fields, 
					   ebsql->priv->n_summary_fields);

		ebsql->priv->n_summary_fields = summary_fields->len;
		ebsql->priv->summary_fields = (SummaryField *) g_array_free (summary_fields, FALSE);

		*introspected_columns = summary_columns;
	} else if (summary_fields) {
		gint n_fields;
		SummaryField *fields;

		/* Properly free the array */
		n_fields = summary_fields->len;
		fields   = (SummaryField *)g_array_free (summary_fields, FALSE);
		summary_fields_array_free (fields, n_fields);

		g_slist_free_full (summary_columns, (GDestroyNotify) g_free);
	}

	g_free (multivalues);

	return success;
}

/* Called with the lock held and inside a transaction */
static gboolean
ebsql_init_contacts (EBookSqlite *ebsql,
		     GSList *introspected_columns,
		     GError **error)
{
	gint i;
	gboolean success = TRUE;
	GString *string;
	GSList *summary_columns = NULL, *l;

	/* Get a list of all columns and indexes which should be present
	 * in the main summary table */
	for (i = 0; i < ebsql->priv->n_summary_fields; i++) {
		SummaryField *field = &(ebsql->priv->summary_fields[i]);

		l = summary_field_list_main_columns (field, ebsql->priv->folderid);
		summary_columns = g_slist_concat (summary_columns, l);
	}

	/* Create the main contacts table for this folder
	 */
	string = g_string_sized_new (32 * g_slist_length (summary_columns));
	g_string_append (string, "CREATE TABLE IF NOT EXISTS %Q (");

	for (l = summary_columns; l; l = l->next) {
		ColumnInfo *info = l->data;

		if (l != summary_columns)
			g_string_append (string, ", ");

		format_column_declaration (string, info);
	}
	g_string_append (string, ", vcard TEXT, bdata TEXT)");

	success = ebsql_exec_printf (ebsql, string->str,
				     NULL, NULL, error,
				     ebsql->priv->folderid);

	g_string_free (string, TRUE);

	/* If we introspected something, let's first adjust the contacts table
	 * so that it includes the right columns */
	if (introspected_columns) {

		/* Add any missing columns which are in the summary fields but
		 * not found in the contacts table
		 */
		for (l = summary_columns; success && l; l = l->next) {
			ColumnInfo *info = l->data;

			if (g_slist_find_custom (introspected_columns,
						 info->name, (GCompareFunc)g_ascii_strcasecmp))
				continue;

			success = ebsql_exec_printf (ebsql,
						     "ALTER TABLE %Q ADD COLUMN %s %s %s",
						     NULL, NULL, error,
						     ebsql->priv->folderid,
						     info->name, info->type,
						     info->extra ? info->extra : "");
		}
	}

	/* Add indexes to columns in the main contacts table
	 */
	for (l = summary_columns; success && l; l = l->next) {
		ColumnInfo *info = l->data;

		success = ensure_column_index (ebsql, ebsql->priv->folderid, info, error);
	}

	g_slist_free_full (summary_columns, (GDestroyNotify)column_info_free);

	return success;
}

/* Called with the lock held and inside a transaction */
static gboolean
ebsql_init_aux_tables (EBookSqlite *ebsql,
		       gint previous_schema,
		       GError **error)
{
	GString *string;
	gboolean success = TRUE;
	GSList *aux_columns = NULL, *l;
	gchar *tmp;
	gint i;

	/* Drop the general 'folder_id_lists' table which was used prior to
	 * version 8 of the schema
	 */
	if (previous_schema >= 1 && previous_schema < 8) {
		tmp = g_strconcat (ebsql->priv->folderid, "_lists", NULL);
		success = ebsql_exec_printf (ebsql, "DROP TABLE IF EXISTS %Q",
					     NULL, NULL, error, tmp);
		g_free (tmp);
	}

	for (i = 0; success && i < ebsql->priv->n_summary_fields; i++) {
		SummaryField *field = &(ebsql->priv->summary_fields[i]);

		if (field->type != E_TYPE_CONTACT_ATTR_LIST)
			continue;

		aux_columns = summary_field_list_aux_columns (field, ebsql->priv->folderid);

		/* Create the auxiliary table for this multi valued field */
		string = g_string_sized_new (COLUMN_DEFINITION_BYTES * 3 + 
					     COLUMN_DEFINITION_BYTES * g_slist_length (aux_columns));

		g_string_append (string, "CREATE TABLE IF NOT EXISTS %Q (uid TEXT NOT NULL REFERENCES %Q (uid)");
		for (l = aux_columns; l; l = l->next) {
			ColumnInfo *info = l->data;

			g_string_append (string, ", ");
			format_column_declaration (string, info);
		}
		g_string_append_c (string, ')');

		success = ebsql_exec_printf (ebsql, string->str, NULL, NULL, error,
					     field->aux_table, ebsql->priv->folderid);
		g_string_free (string, TRUE);

		/* Add indexes to columns in this auxiliary table
		 */
		for (l = aux_columns; success && l; l = l->next) {
			ColumnInfo *info = l->data;

			success = ensure_column_index (ebsql, field->aux_table, info, error);
		}

		g_slist_free_full (aux_columns, (GDestroyNotify)column_info_free);
	}

	return success;
}

static gboolean
ebsql_upgrade_one (EBookSqlite          *ebsql,
		   EbSqlSearchData      *result,
		   EbSqlChangeCallback   callback,
		   gpointer              user_data,
		   GError              **error)
{
	EContact *contact = NULL;
	gboolean changed = FALSE;
	gboolean success;

	/* It can be we're opening a light summary which was created without
	 * storing the vcards, such as was used in EDS versions 3.2 to 3.6.
	 *
	 * In this case we just want to skip the contacts we can't load
	 * and leave them as is in the SQLite, they will be added from
	 * the old BDB in the case of a migration anyway.
	 */
	if (result->vcard)
		contact = e_contact_new_from_vcard_with_uid (result->vcard, result->uid);

	if (contact == NULL)
		return TRUE;

	success = ebsql_insert_contact (ebsql, contact, result->extra, TRUE, &changed, error);

	/* Notify caller that this contact changed during relocalization */
	if (changed && callback)
		callback (result->uid, result->vcard, user_data);

	g_object_unref (contact);

	return success;
}

/* Called with the lock held and inside a transaction */
static gboolean
ebsql_upgrade (EBookSqlite          *ebsql,
	       EbSqlChangeCallback   callback,
	       gpointer              user_data,
	       GError              **error)
{
	gchar *uid = NULL;
	gint n_results;
	gboolean success = TRUE;

	do {
		GSList *batch = NULL, *l;
		EbSqlSearchData *result = NULL;

		if (uid == NULL) {
			success = ebsql_exec_printf (
				ebsql,
				"SELECT summary.uid, %s, summary.bdata FROM %Q AS summary "
				"ORDER BY summary.uid ASC LIMIT %d",
				collect_full_results_cb, &batch, error,
				EBSQL_VCARD_FRAGMENT (ebsql),
				ebsql->priv->folderid, EBSQL_UPGRADE_BATCH_SIZE);
		} else {
			success = ebsql_exec_printf (
				ebsql,
				"SELECT summary.uid, %s, summary.bdata FROM %Q AS summary "
				"WHERE summary.uid > %Q "
				"ORDER BY summary.uid ASC LIMIT %d",
				collect_full_results_cb, &batch, error,
				EBSQL_VCARD_FRAGMENT (ebsql),
				ebsql->priv->folderid, uid, EBSQL_UPGRADE_BATCH_SIZE);
		}

		/* Reverse the list, we want to walk through it forwards */
		batch = g_slist_reverse (batch);
		for (l = batch; success && l; l = l->next) {
			result = l->data;
			success = ebsql_upgrade_one (ebsql, 
						     result,
						     callback,
						     user_data,
						     error);
		}

		/* result is now the last one in the list */
		if (result) {
			g_free (uid);
			uid = result->uid;
			result->uid = NULL;
		}

		n_results = g_slist_length (batch);
 		g_slist_free_full (batch, (GDestroyNotify)e_book_sqlite_search_data_free);

	} while (success && n_results == EBSQL_UPGRADE_BATCH_SIZE);

	g_free (uid);

	/* Store the new locale & country code */
	if (success)
		success = ebsql_exec_printf (
			ebsql, "UPDATE folders SET countrycode = %Q WHERE folder_id = %Q",
			NULL, NULL, error,
			ebsql->priv->region_code, ebsql->priv->folderid);

	if (success)
		success = ebsql_exec_printf (
			ebsql, "UPDATE folders SET lc_collate = %Q WHERE folder_id = %Q",
			NULL, NULL, error,
			ebsql->priv->locale, ebsql->priv->folderid);

	return success;
}

static gboolean
ebsql_set_locale_internal (EBookSqlite   *ebsql,
			   const gchar          *locale,
			   GError              **error)
{
	EBookSqlitePrivate *priv = ebsql->priv;
	ECollator *collator;

	g_return_val_if_fail (locale && locale[0], FALSE);

	if (g_strcmp0 (priv->locale, locale) != 0) {
		gchar *country_code = NULL;

		collator = e_collator_new_interpret_country (locale,
							     &country_code,
							     error);
		if (!collator)
			return FALSE;

		/* Assign region code parsed from the locale by ICU */
		g_free (priv->region_code);
		priv->region_code = country_code;

		/* Assign locale */
		g_free (priv->locale);
		priv->locale = g_strdup (locale);

		/* Assign collator */
		if (ebsql->priv->collator)
			e_collator_unref (ebsql->priv->collator);
		ebsql->priv->collator = collator;
	}

	return TRUE;
}

/* Called with the lock held and inside a transaction */
static gboolean
ebsql_init_is_populated (EBookSqlite *ebsql,
			 gint previous_schema,
			 GError **error)
{
	gboolean success = TRUE;

	/* Schema 8 is when we moved from EBookSqliteDB */
	if (previous_schema >= 1 && previous_schema < 8) {
		gint is_populated = 0;

		/* We need to hold on to the value of any previously set 'is_populated' flag */
		success = ebsql_exec_printf (
			ebsql, "SELECT is_populated FROM folders WHERE folder_id = %Q",
			get_int_cb, &is_populated, error, ebsql->priv->folderid);

		if (success) {
			/* We can't use e_book_sqlite_set_key_value_int() at this
			 * point as that would hold the access locks
			 */
			success = ebsql_exec_printf (
				ebsql, "INSERT or REPLACE INTO keys (key, value, folder_id) values (%Q, %Q, %Q)",
				NULL, NULL, error,
				E_BOOK_SQL_IS_POPULATED_KEY,
				is_populated ? "1" : "0",
				ebsql->priv->folderid);
		}
	}

	return success;
}

/* Called with the lock held and inside a transaction */
static gboolean
ebsql_init_locale (EBookSqlite *ebsql,
		   gint previous_schema,
		   gboolean already_exists,
		   GError **error)
{
	gchar *stored_lc_collate = NULL;
	gchar *stored_region_code = NULL;
	const gchar *lc_collate = NULL;
	gboolean success = TRUE;
	gboolean relocalize_needed = FALSE;

	/* Get the locale setting for this addressbook */
	if (already_exists) {
		success = ebsql_exec_printf (
			ebsql, "SELECT lc_collate FROM folders WHERE folder_id = %Q",
			get_string_cb, &stored_lc_collate, error, ebsql->priv->folderid);

		if (success)
			success = ebsql_exec_printf (
				ebsql, "SELECT countrycode FROM folders WHERE folder_id = %Q",
				get_string_cb, &stored_region_code, error, ebsql->priv->folderid);

		lc_collate = stored_lc_collate;
	}

	/* When creating a new addressbook, or upgrading from a version
	 * where we did not have any locale setting; default to system locale,
	 * we must absolutely always have a locale set.
	 */
	if (!lc_collate || !lc_collate[0])
		lc_collate = setlocale (LC_COLLATE, NULL);
	if (!lc_collate || !lc_collate[0])
		lc_collate = setlocale (LC_ALL, NULL);
	if (!lc_collate || !lc_collate[0])
		lc_collate = "en_US.utf8";

	/* Before touching any data, make sure we have a valid ECollator,
	 * this will also resolve our region code
	 */
	if (success)
		success = ebsql_set_locale_internal (ebsql, lc_collate, error);

	/* Check if we need to relocalize */
	if (success) {
		/* Need to relocalize the whole thing if the schema has been upgraded to version 7 */
		if (previous_schema >= 1 && previous_schema < 7)
			relocalize_needed = TRUE;

		/* We may need to relocalize for a country code change */
		else if (g_strcmp0 (ebsql->priv->region_code, stored_region_code) != 0)
			relocalize_needed = TRUE;
	}

	/* Reinsert all contacts with new locale & country code */
	if (success && relocalize_needed)
		success = ebsql_upgrade (ebsql, NULL, NULL, error);

	g_free (stored_region_code);
	g_free (stored_lc_collate);

	return success;
}

static EBookSqlite *
ebsql_new_internal (const gchar *path,
		    const gchar *folderid,
		    EbSqlVCardCallback callback,
		    gpointer user_data,
		    SummaryField *fields,
		    gint n_fields,
		    GError **error)
{
	EBookSqlite *ebsql;
	gchar *dirname = NULL;
	gint previous_schema = 0;
	gboolean already_exists = FALSE;
	gboolean success = TRUE;
	GSList *introspected_columns = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	if (folderid == NULL)
		folderid = DEFAULT_FOLDER_ID;

	EBSQL_LOCK_MUTEX (&dbcon_lock);

	ebsql = ebsql_ref_from_hash (path);
	if (ebsql)
		goto exit;

	ebsql = g_object_new (E_TYPE_BOOK_SQLITE, NULL);
	ebsql->priv->path = g_strdup (path);
	ebsql->priv->folderid = g_strdup (folderid);
	ebsql->priv->summary_fields = fields;
	ebsql->priv->n_summary_fields = n_fields;
	ebsql->priv->vcard_callback = callback;
	ebsql->priv->vcard_user_data = user_data;

	/* Ensure existance of the directories leading up to 'path' */
	dirname = g_path_get_dirname (path);
	if (g_mkdir_with_parents (dirname, 0777) < 0) {
		EBSQL_SET_ERROR (error,
				 E_BOOK_SQL_ERROR_OTHER,
				 "Can not make parent directory: %s",
			     g_strerror (errno));
		success = FALSE;
		goto exit;
	}

	/* The additional instance lock is unneccesarry because of the global
	 * lock held here, but let's keep it locked because we hold it while
	 * executing any SQLite code throughout this code
	 */
	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);

	/* Initialize the SQLite (set some parameters and add some custom hooks) */
	if (!ebsql_init_sqlite (ebsql, path, error)) {
		EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
		success = FALSE;
		goto exit;
	}

	/* Lets do it all atomically inside a single transaction */
	if (!ebsql_start_transaction (ebsql, TRUE, error)) {
		EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
		success = FALSE;
		goto exit;
	}

	/* Initialize main folders table, also retrieve the current
	 * schema version if the table already exists
	 */
	if (success)
		success = ebsql_init_folders (ebsql, &previous_schema, error);

	/* Initialize the key/value table */
	if (success)
		success = ebsql_init_keys (ebsql, error);

	/* Determine if the addressbook already existed, and fill out
	 * some information in the main folder table
	 */
	if (success)
		success = ebsql_add_folder (ebsql, &already_exists, error);

	/* If the addressbook did exist, then check how it's configured.
	 *
	 * Let the existing summary information override the current
	 * one asked for by our callers.
	 *
	 * Some summary fields are also adjusted for schema upgrades
	 */
	if (success && already_exists)
		success = ebsql_introspect_summary (ebsql,
						    previous_schema,
						    &introspected_columns,
						    error);

	/* Add the contacts table, ensure the right columns are defined
	 * to handle our summary configuration
	 */
	if (success)
		success = ebsql_init_contacts (ebsql,
					       introspected_columns,
					       error);

	/* Add any auxiliary tables which we might need to support our
	 * summary configuration.
	 *
	 * Any fields which represent a 'list-of-strings' require an
	 * auxiliary table to store them in.
	 */
	if (success)
		success = ebsql_init_aux_tables (ebsql, previous_schema, error);

	/* At this point we have resolved our schema, let's build our
	 * precompiled statements, we might use them to re-insert contacts
	 * in the next step
	 */
	if (success)
		success = ebsql_init_statements (ebsql, error);


	/* When porting from older schemas, we need to port the old 'is-populated' flag */
	if (success)
		success = ebsql_init_is_populated (ebsql, previous_schema, error);

	/* Load / resolve the current locale setting
	 *
	 * Also perform the overall upgrade in this step
	 * in the case that an upgrade happened, or a locale
	 * change is detected... all rows need to be renormalized
	 * for this.
	 */
	if (success)
		success = ebsql_init_locale (ebsql, previous_schema,
					     already_exists, error);

	if (success)
		success = ebsql_commit_transaction (ebsql, error);
	else
		/* The GError is already set. */
		ebsql_rollback_transaction (ebsql, NULL);

	/* Release the instance lock and register to the global hash */
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	if (success)
		ebsql_register_to_hash (ebsql, path);

 exit:

	/* Cleanup and exit */
	EBSQL_UNLOCK_MUTEX (&dbcon_lock);

	/* If we failed somewhere, give up on creating the 'ebsql',
	 * otherwise add it to the hash table
	 */
	if (!success)
		g_clear_object (&ebsql);

	g_slist_free_full (introspected_columns, (GDestroyNotify) g_free);
	g_free (dirname);

	return ebsql;
}

/**********************************************************
 *                   Inserting Contacts                   *
 **********************************************************/
static gchar *
convert_phone (const gchar *normal,
               const gchar *region_code,
	       gint *out_country_code)
{
	EPhoneNumber *number = NULL;
	gchar *national_number = NULL;
	gint country_code = 0;

	/* Don't warn about erronous phone number strings, it's a perfectly normal
	 * use case for users to enter notes instead of phone numbers in the phone
	 * number contact fields, such as "Ask Jenny for Lisa's phone number"
	 */
	if (normal && e_phone_number_is_supported ())
		number = e_phone_number_from_string (normal, region_code, NULL);

	if (number) {
		EPhoneNumberCountrySource source;

		national_number = e_phone_number_get_national_number (number);
		country_code = e_phone_number_get_country_code (number, &source);
		e_phone_number_free (number);

		if (source == E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT)
			country_code = 0;
	}

	if (out_country_code)
		*out_country_code = country_code;

	return national_number;
}

typedef struct {
	gint country_code;
	gchar *national;
} E164Number;

static E164Number *
ebsql_e164_number_new (gint country_code,
		       gchar *national)
{
	E164Number *number = g_slice_new (E164Number);

	number->country_code = country_code;
	number->national = g_strdup (national);

	return number;
}

static void
ebsql_e164_number_free (E164Number *number)
{
	if (number) {
		g_free (number->national);
		g_slice_free (E164Number, number);
	}
}

static gint
ebsql_e164_number_find (E164Number *number_a,
			E164Number *number_b)
{
	gint ret;

	ret = number_a->country_code - number_b->country_code;

	if (ret == 0)
		ret = g_strcmp0 (number_a->national,
				 number_b->national);

	return ret;
}

static GList *
extract_e164_attribute_params (EVCard *vcard)
{
	GList *extracted = NULL;
	GList *attr_list;

	for (attr_list = e_vcard_get_attributes (vcard); attr_list; attr_list = attr_list->next) {
		EVCardAttribute *const attr = attr_list->data;
		EVCardAttributeParam *param = NULL;
		GList *param_list, *values, *l;
		gchar *this_national = NULL;
		gint this_country = 0;

		/* We only attach E164 parameters to TEL attributes. */
		if (strcmp (e_vcard_attribute_get_name (attr), EVC_TEL) != 0)
			continue;

		/* Find already exisiting parameter, so that we can reuse it. */
		for (param_list = e_vcard_attribute_get_params (attr); param_list; param_list = param_list->next) {
			if (strcmp (e_vcard_attribute_param_get_name (param_list->data), EVC_X_E164) == 0) {
				param = param_list->data;
				break;
			}
		}

		if (!param)
			continue;

		values = e_vcard_attribute_param_get_values (param);
		for (l = values; l; l = l->next) {
			const gchar *value = l->data;

			if (value[0] == '+')
				this_country = g_ascii_strtoll (&value[1], NULL, 10);
			else if (this_national == NULL)
				this_national = g_strdup (value);
		}

		if (this_national) {
			E164Number *number;

			number = ebsql_e164_number_new (this_country,
							this_national);
			extracted = g_list_prepend (extracted, number);
		}

		g_free (this_national);

		/* Clear the values, we'll insert new ones */
		e_vcard_attribute_param_remove_values (param);
		e_vcard_attribute_remove_param (attr, EVC_X_E164);
	}

	return extracted;
}

static gboolean
update_e164_attribute_params (EBookSqlite *ebsql,
			      EVCard *vcard,
                              const gchar *default_region)
{
	GList *original_numbers = NULL;
	GList *attr_list;
	gboolean changed = FALSE;
	gint n_numbers = 0;

	original_numbers = extract_e164_attribute_params (vcard);

	for (attr_list = e_vcard_get_attributes (vcard); attr_list; attr_list = attr_list->next) {
		EVCardAttribute *const attr = attr_list->data;
		EVCardAttributeParam *param = NULL;
		gchar *country_string;
		GList *values;
		E164Number number = { 0, NULL };

		/* We only attach E164 parameters to TEL attributes. */
		if (strcmp (e_vcard_attribute_get_name (attr), EVC_TEL) != 0)
			continue;

		/* Fetch the TEL value */
		values = e_vcard_attribute_get_values (attr);

		/* Compute E164 number based on the TEL value */
		if (values && values->data)
			number.national = convert_phone (values->data,
							 ebsql->priv->region_code,
							 &(number.country_code));

		if (number.national == NULL)
			continue;

		/* Count how many we successfully parsed in this region code */
		n_numbers++;

		/* Check if we have a differing e164 number, if there is no match
		 * in the old existing values then the vcard changed
		 */
		if (!g_list_find_custom (original_numbers, &number, 
					 (GCompareFunc)ebsql_e164_number_find))
			changed = TRUE;

		if (number.country_code != 0)
			country_string = g_strdup_printf ("+%d", number.country_code);
		else
			country_string = g_strdup ("");

		param = e_vcard_attribute_param_new (EVC_X_E164);
		e_vcard_attribute_add_param (attr, param);

		/* Assign the parameter values. It seems odd that we revert
		 * the order of NN and CC, but at least EVCard's parser doesn't
		 * permit an empty first param value. Which of course could be
		 * fixed - in order to create a nice potential IOP problem with
		 ** other vCard parsers. */
		e_vcard_attribute_param_add_values (param, number.national, country_string, NULL);

		g_free (number.national);
		g_free (country_string);
	}

	if (!changed &&
	    n_numbers != g_list_length (original_numbers))
		changed = TRUE;

	g_list_free_full (original_numbers, (GDestroyNotify)ebsql_e164_number_free);

	return changed;
}

static sqlite3_stmt *
ebsql_prepare_multi_delete (EBookSqlite *ebsql,
			    SummaryField *field,
			    GError **error)
{
	sqlite3_stmt *stmt = NULL;
	gchar *stmt_str;

	stmt_str = sqlite3_mprintf ("DELETE FROM %Q WHERE uid = :uid", field->aux_table);
	stmt = ebsql_prepare_statement (ebsql, stmt_str, error);
	sqlite3_free (stmt_str);

	return stmt;
}

static gboolean
ebsql_run_multi_delete (EBookSqlite *ebsql,
			SummaryField *field,
			const gchar *uid,
			GError **error)
{
	sqlite3_stmt *stmt;
	gint ret;

	stmt = g_hash_table_lookup (ebsql->priv->multi_deletes, GUINT_TO_POINTER (field->field_id));

	/* This can return an error if a previous call to sqlite3_step() had errors,
	 * so let's just ignore any error in this case
	 */
	sqlite3_reset (stmt);

	/* Clear all previously set values */
	ret = sqlite3_clear_bindings (stmt);

	/* Set the UID host parameter statically */
	if (ret == SQLITE_OK)
		ret = sqlite3_bind_text (stmt, 1, uid, -1, SQLITE_STATIC);

	/* Run the statement */
	return ebsql_complete_statement (ebsql, stmt, ret, error);
}

static sqlite3_stmt *
ebsql_prepare_multi_insert (EBookSqlite *ebsql,
			    SummaryField *field,
			    GError **error)
{
	sqlite3_stmt *stmt = NULL;
	GString *string;

	string = g_string_sized_new (INSERT_MULTI_STMT_BYTES);
	ebsql_string_append_printf (string, "INSERT INTO %Q (uid, value", field->aux_table);

	if ((field->index & INDEX_FLAG (SUFFIX)) != 0)
		g_string_append (string, ", value_" EBSQL_SUFFIX_REVERSE);

	if ((field->index & INDEX_FLAG (PHONE)) != 0) {
		g_string_append (string, ", value_" EBSQL_SUFFIX_PHONE);
		g_string_append (string, ", value_" EBSQL_SUFFIX_COUNTRY);
	}

	g_string_append (string, ") VALUES (:uid, :value");

	if ((field->index & INDEX_FLAG (SUFFIX)) != 0)
		g_string_append (string, ", :value_" EBSQL_SUFFIX_REVERSE);

	if ((field->index & INDEX_FLAG (PHONE)) != 0) {
		g_string_append (string, ", :value_" EBSQL_SUFFIX_PHONE);
		g_string_append (string, ", :value_" EBSQL_SUFFIX_COUNTRY);
	}

	g_string_append_c (string, ')');

	stmt = ebsql_prepare_statement (ebsql, string->str, error);
	g_string_free (string, TRUE);

	return stmt;
}

static gboolean
ebsql_run_multi_insert_one (EBookSqlite *ebsql,
			    sqlite3_stmt *stmt,
			    SummaryField *field,
			    const gchar *uid,
			    const gchar *value,
			    GError **error)
{
	gchar *normal = e_util_utf8_normalize (value);
	gchar *str;
	gint ret, param_idx = 1;

	/* :uid */
	ret = sqlite3_bind_text (stmt, param_idx++, uid, -1, SQLITE_STATIC);

	if (ret == SQLITE_OK)  /* :value */
		ret = sqlite3_bind_text (stmt, param_idx++, normal, -1, g_free);

	if (ret == SQLITE_OK && (field->index & INDEX_FLAG (SUFFIX)) != 0) {
		if (normal)
			str = g_utf8_strreverse (normal, -1);
		else
			str = NULL;

		/* :value_reverse */
		ret = sqlite3_bind_text (stmt, param_idx++, str, -1, g_free);
	}

	if (ret == SQLITE_OK && (field->index & INDEX_FLAG (PHONE)) != 0) {
		gint country_code;

		str = convert_phone (normal, ebsql->priv->region_code,
				     &country_code);

		/* :value_phone */
		ret = sqlite3_bind_text (stmt, param_idx++, str, -1, g_free);

		/* :value_country */
		if (ret == SQLITE_OK)
			sqlite3_bind_int (stmt, param_idx++, country_code);

	}

	/* Run the statement */
	return ebsql_complete_statement (ebsql, stmt, ret, error);
}

static gboolean
ebsql_run_multi_insert (EBookSqlite *ebsql,
			SummaryField *field,
			const gchar *uid,
			EContact *contact,
			GError **error)
{
	sqlite3_stmt *stmt;
	GList *values, *l;
	gboolean success = TRUE;

	stmt = g_hash_table_lookup (ebsql->priv->multi_inserts, GUINT_TO_POINTER (field->field_id));
	values = e_contact_get (contact, field->field_id);

	for (l = values; success && l != NULL; l = l->next) {
		gchar *value = (gchar *) l->data;

		success = ebsql_run_multi_insert_one (
			ebsql, stmt, field, uid, value, error);
	}

	/* Free the list of allocated strings */
	e_contact_attr_list_free (values);

	return success;
}

static sqlite3_stmt *
ebsql_prepare_insert (EBookSqlite *ebsql,
		      gboolean replace_existing,
		      GError **error)
{
	sqlite3_stmt *stmt;
	GString *string;
	gint i;

	string = g_string_new ("");
	if (replace_existing)
		ebsql_string_append_printf (string, "INSERT or REPLACE INTO %Q (",
					    ebsql->priv->folderid);
	else
		ebsql_string_append_printf (string, "INSERT or FAIL INTO %Q (",
					    ebsql->priv->folderid);

	/*
	 * First specify the column names for the insert, since it's possible we
	 * upgraded the DB and cannot be sure the order of the columns are ordered
	 * just how we like them to be.
	 */
	for (i = 0; i < ebsql->priv->n_summary_fields; i++) {
		SummaryField *field = &(ebsql->priv->summary_fields[i]);

		/* Multi values go into a separate table/statement */
		if (field->type != E_TYPE_CONTACT_ATTR_LIST) {

			/* Only add a ", " before every field except the first,
			 * this will not break because the first 2 fields (UID & REV)
			 * are string fields.
			 */
			if (i > 0)
				g_string_append (string, ", ");

			g_string_append (string, field->dbname);
		}

		if (field->type == G_TYPE_STRING) {

			if ((field->index & INDEX_FLAG (SORT_KEY)) != 0) {
				g_string_append (string, ", ");
				g_string_append (string, field->dbname);
				g_string_append (string, "_" EBSQL_SUFFIX_SORT_KEY);
			}

			if ((field->index & INDEX_FLAG (SUFFIX)) != 0) {
				g_string_append (string, ", ");
				g_string_append (string, field->dbname);
				g_string_append (string, "_" EBSQL_SUFFIX_REVERSE);
			}

			if ((field->index & INDEX_FLAG (PHONE)) != 0) {

				g_string_append (string, ", ");
				g_string_append (string, field->dbname);
				g_string_append (string, "_" EBSQL_SUFFIX_PHONE);

				g_string_append (string, ", ");
				g_string_append (string, field->dbname);
				g_string_append (string, "_" EBSQL_SUFFIX_COUNTRY);
			}
		}
	}
	g_string_append (string, ", vcard, bdata)");

	/*
	 * Now specify values for all of the column names we specified.
	 */
	g_string_append (string, " VALUES (");
	for (i = 0; i < ebsql->priv->n_summary_fields; i++) {
		SummaryField *field = &(ebsql->priv->summary_fields[i]);

		if (field->type != E_TYPE_CONTACT_ATTR_LIST) {
			/* Only add a ", " before every field except the first,
			 * this will not break because the first 2 fields (UID & REV)
			 * are string fields.
			 */
			if (i > 0)
				g_string_append (string, ", ");
		}

		if (field->type == G_TYPE_STRING || field->type == G_TYPE_BOOLEAN) {

			g_string_append_c (string, ':');
			g_string_append (string, field->dbname);

			if ((field->index & INDEX_FLAG (SORT_KEY)) != 0)
				g_string_append_printf (string, ", :%s_" EBSQL_SUFFIX_SORT_KEY, field->dbname);

			if ((field->index & INDEX_FLAG (SUFFIX)) != 0)
				g_string_append_printf (string, ", :%s_" EBSQL_SUFFIX_REVERSE, field->dbname);

			if ((field->index & INDEX_FLAG (PHONE)) != 0) {
				g_string_append_printf (string, ", :%s_" EBSQL_SUFFIX_PHONE, field->dbname);
				g_string_append_printf (string, ", :%s_" EBSQL_SUFFIX_COUNTRY, field->dbname);
			}

		} else if (field->type != E_TYPE_CONTACT_ATTR_LIST)
			g_warn_if_reached ();
	}

	g_string_append (string, ", :vcard, :bdata)");

	stmt = ebsql_prepare_statement (ebsql, string->str, error);
	g_string_free (string, TRUE);

	return stmt;
}

static gboolean
ebsql_init_statements (EBookSqlite *ebsql,
		       GError **error)
{
	sqlite3_stmt *stmt;
	gint i;

	ebsql->priv->insert_stmt = ebsql_prepare_insert (ebsql, FALSE, error);
	if (!ebsql->priv->insert_stmt)
		goto preparation_failed;

	ebsql->priv->replace_stmt = ebsql_prepare_insert (ebsql, TRUE, error);
	if (!ebsql->priv->replace_stmt)
		goto preparation_failed;

	ebsql->priv->multi_deletes =
		g_hash_table_new_full (g_direct_hash, g_direct_equal,
				       NULL,
				       (GDestroyNotify)sqlite3_finalize);
	ebsql->priv->multi_inserts =
		g_hash_table_new_full (g_direct_hash, g_direct_equal,
				       NULL,
				       (GDestroyNotify)sqlite3_finalize);

	for (i = 0; i < ebsql->priv->n_summary_fields; i++) {
		SummaryField *field = &(ebsql->priv->summary_fields[i]);

		if (field->type != E_TYPE_CONTACT_ATTR_LIST)
			continue;

		stmt = ebsql_prepare_multi_insert (ebsql, field, error);
		if (!stmt)
			goto preparation_failed;

		g_hash_table_insert (ebsql->priv->multi_inserts,
				     GUINT_TO_POINTER (field->field_id),
				     stmt);

		stmt = ebsql_prepare_multi_delete (ebsql, field, error);
		if (!stmt)
			goto preparation_failed;

		g_hash_table_insert (ebsql->priv->multi_deletes,
				     GUINT_TO_POINTER (field->field_id),
				     stmt);
	}

	return TRUE;

 preparation_failed:

	return FALSE;
}

static gboolean
ebsql_run_insert (EBookSqlite *ebsql,
		  gboolean replace,
		  EContact *contact,
		  const gchar *extra,
		  GError **error)
{
	EBookSqlitePrivate *priv;
	sqlite3_stmt *stmt;
	gint i, param_idx;
	gint ret;

	priv = ebsql->priv;

	if (replace)
		stmt = ebsql->priv->replace_stmt;
	else
		stmt = ebsql->priv->insert_stmt;

	/* This can return an error if a previous call to sqlite3_step() had errors,
	 * so let's just ignore any error in this case
	 */
	sqlite3_reset (stmt);

	/* Clear all previously set values */
	ret = sqlite3_clear_bindings (stmt);

	for (i = 0, param_idx = 1; ret == SQLITE_OK && i < ebsql->priv->n_summary_fields; i++) {
		SummaryField *field = &(ebsql->priv->summary_fields[i]);

		if (field->type == G_TYPE_STRING) {
			gchar *val;
			gchar *normal;
			gchar *str;

			val = e_contact_get (contact, field->field_id);

			/* Special exception, never normalize/localize the UID or REV string */
			if (field->field_id != E_CONTACT_UID &&
			    field->field_id != E_CONTACT_REV) {
				normal = e_util_utf8_normalize (val);
			} else
				normal = g_strdup (val);

			/* Takes ownership of 'normal' */
			ret = sqlite3_bind_text (stmt, param_idx++, normal, -1, g_free);

			if (ret == SQLITE_OK &&
			    (field->index & INDEX_FLAG (SORT_KEY)) != 0) {
				if (val)
					str = e_collator_generate_key (ebsql->priv->collator, val, NULL);
				else
					str = g_strdup ("");

				ret = sqlite3_bind_text (stmt, param_idx++, str, -1, g_free);
			}

			if (ret == SQLITE_OK &&
			    (field->index & INDEX_FLAG (SUFFIX)) != 0) {
				if (normal)
					str = g_utf8_strreverse (normal, -1);
				else
					str = NULL;

				ret = sqlite3_bind_text (stmt, param_idx++, str, -1, g_free);
			}

			if (ret == SQLITE_OK &&
			    (field->index & INDEX_FLAG (PHONE)) != 0) {
				gint country_code;

				str = convert_phone (normal, ebsql->priv->region_code,
						     &country_code);

				ret = sqlite3_bind_text (stmt, param_idx++, str, -1, g_free);
				if (ret == SQLITE_OK)
					sqlite3_bind_int (stmt, param_idx++, country_code);
			}

			g_free (val);
		} else if (field->type == G_TYPE_BOOLEAN) {
			gboolean val;

			val = e_contact_get (contact, field->field_id) ? TRUE : FALSE;

			ret = sqlite3_bind_int (stmt, param_idx++, val ? 1 : 0);
		} else if (field->type != E_TYPE_CONTACT_ATTR_LIST)
			g_warn_if_reached ();
	}

	if (ret == SQLITE_OK) {
		gchar *vcard = NULL;

		/* If we have a priv->vcard_callback, then it's a shallow addressbook
		 * and we don't populate the vcard column
		 */
		if (priv->vcard_callback == NULL)
			vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

		ret = sqlite3_bind_text (stmt, param_idx++, vcard, -1, g_free);
	}

	/* The extra data */
	if (ret == SQLITE_OK)
		ret = sqlite3_bind_text (stmt, param_idx++, g_strdup (extra), -1, g_free);

	/* Run the statement */
	return ebsql_complete_statement (ebsql, stmt, ret, error);
}

static gboolean
ebsql_insert_contact (EBookSqlite *ebsql,
		      EContact *contact,
		      const gchar *extra,
		      gboolean replace,
		      gboolean *e164_changed,
		      GError **error)
{
	EBookSqlitePrivate *priv;
	gboolean e164_changed_local;
	gboolean success;

	priv = ebsql->priv;

	/* Update E.164 parameters in vcard if needed */
	if (priv->vcard_callback == NULL) {
		e164_changed_local =
			update_e164_attribute_params (ebsql, E_VCARD (contact),
						      priv->region_code);

		if (e164_changed)
			*e164_changed = e164_changed_local;
	}

	success = ebsql_run_insert (ebsql, replace, contact, extra, error);

	/* Update attribute list table */
	if (success) {
		gchar *uid = e_contact_get (contact, E_CONTACT_UID);
		gint i;

		for (i = 0; success && i < priv->n_summary_fields; i++) {
			SummaryField *field = &(ebsql->priv->summary_fields[i]);

			if (field->type != E_TYPE_CONTACT_ATTR_LIST)
				continue;

			success = ebsql_run_multi_delete (
				ebsql, field, uid, error);

			if (success)
				success = ebsql_run_multi_insert (
					ebsql, field, uid, contact, error);
		}

		g_free (uid);
	}

	return success;
}

/***************************************************************
 * Structures and utilities for preflight and query generation *
 ***************************************************************/

/* Internal extension of the EBookQueryTest enumeration */
enum {
	/* 'exists' is a supported query on a field, but not part of EBookQueryTest */
	BOOK_QUERY_EXISTS = E_BOOK_QUERY_LAST,

	/* From here the compound types start */
	BOOK_QUERY_SUB_AND,
	BOOK_QUERY_SUB_OR,
	BOOK_QUERY_SUB_NOT,
	BOOK_QUERY_SUB_END,

	BOOK_QUERY_SUB_FIRST = BOOK_QUERY_SUB_AND,
};

#define IS_QUERY_PHONE(query)						\
	((query) == E_BOOK_QUERY_EQUALS_PHONE_NUMBER          ||	\
	 (query) == E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER ||	\
	 (query) == E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER)

typedef struct {
	guint          query; /* EBookQueryTest (extended) */
} QueryElement;

typedef struct {
	guint          query; /* EBookQueryTest (extended) */
} QueryDelimiter;

typedef struct {
	guint          query;          /* EBookQueryTest (extended) */

	EContactField  field_id;       /* The EContactField to compare */
	SummaryField  *field;          /* The summary field for 'field' */
	gchar         *value;          /* The value to compare with */

	/* For preflighting without collecting strings */
	guint          has_value : 1;
	guint          has_extra : 1;
} QueryFieldTest;

typedef struct {
	guint          query;          /* EBookQueryTest (extended) */

	/* Common fields from QueryFieldTest */
	EContactField  field_id;       /* The EContactField to compare */
	SummaryField  *field;          /* The summary field for 'field' */
	gchar         *value;          /* The value to compare with */
	guint          has_value : 1;
	guint          has_extra : 1;

	/* Extension */
	gchar         *region;   /* Region code from the query input */
	gchar         *national; /* Parsed national number */
	gint           country;  /* Parsed country code */
} QueryPhoneTest;

/* This enumeration is ordered by severity, higher values
 * of PreflightStatus take precedence in error reporting.
 */
typedef enum {
	PREFLIGHT_OK = 0,
	PREFLIGHT_NOT_SUMMARIZED,
	PREFLIGHT_INVALID,
	PREFLIGHT_UNSUPPORTED,
} PreflightStatus;

typedef enum {
	PREFLIGHT_FLAG_STR_COLLECT = (1 << 0),
	PREFLIGHT_FLAG_AUX_COLLECT = (1 << 1)
} PreflightFlags;

typedef struct {
	EContactField  field_id;    /* multi string field id */
	GPtrArray     *constraints; /* segmented query, if applicable */
} PreflightAuxData;

/* Stack initializer for the PreflightContext struct below */
#define PREFLIGHT_CONTEXT_INIT { 0, 0, NULL, FALSE, NULL }

typedef struct {
	PreflightFlags   flags;

	PreflightStatus  status;       /* result status */
	GPtrArray       *constraints;  /* main query */
	gboolean         list_all;     /* TRUE if all results should be returned */

	GSList          *aux_fields;   /* List of PreflightAuxData */
} PreflightContext;

static QueryElement *
query_delimiter_new (guint query)
{
	QueryDelimiter *delim;

	g_return_val_if_fail (query >= BOOK_QUERY_SUB_FIRST, NULL);

	delim        = g_slice_new (QueryDelimiter);
	delim->query = query;

	return (QueryElement *)delim;
}

static QueryFieldTest *
query_field_test_new (guint          query,
		      EContactField  field)
{
	QueryFieldTest *test;

	g_return_val_if_fail (query < BOOK_QUERY_SUB_FIRST, NULL);
	g_return_val_if_fail (IS_QUERY_PHONE (query) == FALSE, NULL);

	test            = g_slice_new (QueryFieldTest);
	test->query     = query;
	test->field_id  = field;

	/* Instead of g_slice_new0, NULL them out manually */
	test->field     = NULL;
	test->value     = NULL;
	test->has_value = FALSE;
	test->has_extra = FALSE;

	return test;
}

static QueryPhoneTest *
query_phone_test_new (guint          query,
		      EContactField  field)
{
	QueryPhoneTest *test;

	g_return_val_if_fail (IS_QUERY_PHONE (query), NULL);

	test            = g_slice_new (QueryPhoneTest);
	test->query     = query;
	test->field_id  = field;

	/* Instead of g_slice_new0, NULL them out manually */
	test->field     = NULL;
	test->value     = NULL;
	test->has_value = FALSE;
	test->has_extra = FALSE;

	/* Extra QueryPhoneTest fields */
	test->region    = NULL;
	test->national  = NULL;
	test->country   = 0;

	return test;
}

static void
query_element_free (QueryElement *element)
{
	if (element) {

		if (element->query >= BOOK_QUERY_SUB_FIRST) {
			QueryDelimiter *delim = (QueryDelimiter *)element;

			g_slice_free (QueryDelimiter, delim);
		} else if (IS_QUERY_PHONE (element->query)) {
			QueryPhoneTest *test = (QueryPhoneTest *)element;

			g_free (test->value);
			g_free (test->region);
			g_free (test->national);
			g_slice_free (QueryPhoneTest, test);
		} else {
			QueryFieldTest *test = (QueryFieldTest *)element;

			g_free (test->value);
			g_slice_free (QueryFieldTest, test);
		}
	}
}

/* We use ptr arrays for the QueryElement vectors */
static inline void
constraints_insert (GPtrArray *array,
		    gint       idx,
		    gpointer   data)
{

#if 0
	g_ptr_array_insert (array, idx, data);
#else
	g_return_if_fail ((idx >= -1) && (idx < (gint)array->len + 1));

	if (idx < 0)
		idx = array->len;

	g_ptr_array_add (array, NULL);

	if (idx != (array->len - 1))
		memmove (&(array->pdata[idx + 1]),
			 &(array->pdata[idx]),
			 ((array->len - 1) - idx) * sizeof (gpointer));

	array->pdata[idx] = data;
#endif
}

static inline QueryElement *
constraints_take (GPtrArray *array,
		  gint       idx)
{
	QueryElement *element;

	g_return_val_if_fail (idx >= 0 && idx < (gint)array->len, NULL);

	element = array->pdata[idx];
	array->pdata[idx] = NULL;
	g_ptr_array_remove_index (array, idx);

	return element;
}

static inline void
constraints_insert_delimiter (GPtrArray *array,
			      gint       idx,
			      guint      query)
{
	QueryElement *delim;

	delim = query_delimiter_new (query);
	constraints_insert (array, idx, delim);
}

static inline void
constraints_insert_field_test (GPtrArray      *array,
			       gint            idx,
			       SummaryField   *field,
			       guint           query,
			       const gchar    *value)
{
	QueryFieldTest *test;

	test            = query_field_test_new (query, field->field_id);
	test->field     = field;
	test->value     = g_strdup (value);
	test->has_value = (value && value[0]);

	constraints_insert (array, idx, test);
}

static PreflightAuxData *
preflight_aux_data_new (EContactField field_id)
{
	PreflightAuxData *aux_data = g_slice_new (PreflightAuxData);

	aux_data->field_id = field_id;
	aux_data->constraints = NULL;

	return aux_data;
}

static void
preflight_aux_data_free (PreflightAuxData *aux_data)
{
	if (aux_data) {
		if (aux_data->constraints)
			g_ptr_array_free (aux_data->constraints, TRUE);

		g_slice_free (PreflightAuxData, aux_data);
	}
}

static gint
preflight_aux_data_find (PreflightAuxData *aux_data,
			 gpointer          data)
{
	EContactField  field_id = GPOINTER_TO_UINT (data);

	/* Unsigned comparison, just to be safe, 
	 * let's not return something like:
	 *   'aux_data->field_id - field_id'
	 */
	if (aux_data->field_id > field_id)
		return 1;
	else if (aux_data->field_id < field_id)
		return -1;

	return 0;
}

static PreflightAuxData *
preflight_context_search_aux (PreflightContext *context,
			      EContactField     field_id)
{
	PreflightAuxData *aux_data = NULL;
	GSList *link;

	link = g_slist_find_custom (context->aux_fields,
				    GUINT_TO_POINTER (field_id),
				    (GCompareFunc)preflight_aux_data_find);
	if (link)
		aux_data = link->data;

	return aux_data;
}

static void
preflight_context_clear (PreflightContext *context)
{
	if (context) {
		/* Free any allocated data, but leave the context values in place */
		if (context->constraints)
			g_ptr_array_free (context->constraints, TRUE);

		g_slist_free_full (context->aux_fields,
				   (GDestroyNotify)preflight_aux_data_free);

		context->aux_fields = NULL;
		context->constraints = NULL;
	}
}

/* A small API to track the current sub-query context.
 *
 * I.e. sub contexts can be OR, AND, or NOT, in which
 * field tests or other sub contexts are nested.
 */
typedef GQueue SubQueryContext;

typedef struct {
	guint sub_type; /* The type of this sub context */
	guint count;    /* The number of field tests so far in this context */
} SubQueryData;

#define sub_query_context_new g_queue_new
#define sub_query_context_free(ctx) g_queue_free (ctx)

static inline void
sub_query_context_push (SubQueryContext *ctx,
			guint            sub_type)
{
	SubQueryData *data;

	data           = g_slice_new (SubQueryData);
	data->sub_type = sub_type;
	data->count    = 0;

	g_queue_push_tail (ctx, data);
}

static inline void
sub_query_context_pop (SubQueryContext *ctx)
{
	SubQueryData *data;

	data = g_queue_pop_tail (ctx);
	g_slice_free (SubQueryData, data);
}

static inline guint
sub_query_context_peek_type (SubQueryContext *ctx)
{
	SubQueryData *data;

	data = g_queue_peek_tail (ctx);

	return data->sub_type;
}

/* Returns the context field test count before incrementing */
static inline guint
sub_query_context_increment (SubQueryContext *ctx)
{
	SubQueryData *data;

	data = g_queue_peek_tail (ctx);

	if (data) {
		data->count++;

		return (data->count - 1);
	}

	/* If we're not in a sub context, just return 0 */
	return 0;
}

/**********************************************************
 *                  Querying preflighting                 *
 **********************************************************
 *
 * The preflight checks are performed before a query might
 * take place in order to evaluate whether the given query
 * can be performed with the current summary configuration.
 *
 * After preflighting, all relevant data has been extracted
 * from the search expression and the search expression need
 * not be parsed again.
 */

/* The PreflightSubCallback is expected to return TRUE
 * to keep iterating and FALSE to abort iteration.
 *
 * The sub_level is the counter of how deep the 'element'
 * is nested in sub elements, the offset is the real offset
 * of 'element' in the array passed to query_preflight_foreach_sub().
 */
typedef gboolean (* PreflightSubCallback) (QueryElement *element,
					   gint          sub_level,
					   gint          offset,
					   gpointer      user_data);

static void
query_preflight_foreach_sub (QueryElement        **elements,
			     gint                  n_elements,
			     gint                  offset,
			     gboolean              include_delim,
			     PreflightSubCallback  callback,
			     gpointer              user_data)
{
	gint sub_counter = 1, i;

	g_return_if_fail (offset >= 0 && offset < n_elements);
	g_return_if_fail (elements[offset]->query >= BOOK_QUERY_SUB_FIRST);
	g_return_if_fail (callback != NULL);

	if (include_delim && !callback (elements[offset], 0, offset, user_data))
		return;

	for (i = (offset + 1); sub_counter > 0 && i < n_elements; i++) {

		if (elements[i]->query >= BOOK_QUERY_SUB_FIRST) {

			if (elements[i]->query == BOOK_QUERY_SUB_END)
				sub_counter--;
			else
				sub_counter++;

			if (include_delim &&
			    !callback (elements[i], sub_counter, i, user_data))
				break;
		} else {

			if (!callback (elements[i], sub_counter, i, user_data))
				break;
		}
	}
}

/* Table used in ESExp parsing below */
static const struct {
	const gchar *name;    /* Name of the symbol to match for this parse phase */
	gboolean     subset;  /* TRUE for the subset ESExpIFunc, otherwise the field check ESExpFunc */
	guint        test;    /* Extended EBookQueryTest value */
} check_symbols[] = {
	{ "and",              TRUE, BOOK_QUERY_SUB_AND },
	{ "or",               TRUE, BOOK_QUERY_SUB_OR },
	{ "not",              TRUE, BOOK_QUERY_SUB_NOT },

	{ "contains",         FALSE, E_BOOK_QUERY_CONTAINS },
	{ "is",               FALSE, E_BOOK_QUERY_IS },
	{ "beginswith",       FALSE, E_BOOK_QUERY_BEGINS_WITH },
	{ "endswith",         FALSE, E_BOOK_QUERY_ENDS_WITH },
	{ "eqphone",          FALSE, E_BOOK_QUERY_EQUALS_PHONE_NUMBER },
	{ "eqphone_national", FALSE, E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER },
	{ "eqphone_short",    FALSE, E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER },
	{ "regex_normal",     FALSE, E_BOOK_QUERY_REGEX_NORMAL },
	{ "regex_raw",        FALSE, E_BOOK_QUERY_REGEX_RAW },
	{ "exists",           FALSE, BOOK_QUERY_EXISTS },
};


/* Cheat our way into passing mode data to these funcs */
typedef struct {
	guint query_type : 16;
	guint flags      : 16;
} CheckFuncData;

static ESExpResult *
func_check_subset (ESExp *f,
                   gint argc,
                   struct _ESExpTerm **argv,
                   gpointer data)
{
	ESExpResult *result, *sub_result;
	GPtrArray *result_array;
	QueryElement *element, **sub_elements;
	gint i, j, len;
	guint uint_data;
	CheckFuncData check_data;

	uint_data = GPOINTER_TO_UINT (data);
	memcpy (&check_data, &uint_data, sizeof (guint));

	/* The compound query delimiter is the first element in this return array */
	result_array = g_ptr_array_new_with_free_func ((GDestroyNotify)query_element_free);
	element      = query_delimiter_new (check_data.query_type);
	g_ptr_array_add (result_array, element);

	for (i = 0; i < argc; i++) {
		sub_result = e_sexp_term_eval (f, argv[i]);

		if (sub_result->type == ESEXP_RES_ARRAY_PTR) {
			/* Steal the elements directly from the sub result */
			sub_elements = (QueryElement **)sub_result->value.ptrarray->pdata;
			len = sub_result->value.ptrarray->len;

			for (j = 0; j < len; j++) {
				element = sub_elements[j];
				sub_elements[j] = NULL;

				g_ptr_array_add (result_array, element);
			}
		}
		e_sexp_result_free (f, sub_result);
	}

	/* The last element in this return array is the sub end delimiter */
	element = query_delimiter_new (BOOK_QUERY_SUB_END);
	g_ptr_array_add (result_array, element);

	result = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
	result->value.ptrarray = result_array;

	return result;
}

static ESExpResult *
func_check (struct _ESExp *f,
	    gint argc,
	    struct _ESExpResult **argv,
	    gpointer data)
{
	ESExpResult *result;
	GPtrArray *result_array;
	QueryElement *element = NULL;
	EContactField field = 0;
	const gchar *query_name = NULL;
	const gchar *query_value = NULL;
	const gchar *query_extra = NULL;
	guint uint_data;
	CheckFuncData check_data;

	uint_data = GPOINTER_TO_UINT (data);
	memcpy (&check_data, &uint_data, sizeof (guint));

	if (argc == 2 &&
	    argv[0]->type == ESEXP_RES_STRING &&
	    argv[1]->type == ESEXP_RES_STRING) {

		query_name  = argv[0]->value.string;
		query_value = argv[1]->value.string;

		/* We use E_CONTACT_FIELD_LAST to hold the special case of "x-evolution-any-field" */
		if (g_strcmp0 (query_name, "x-evolution-any-field") == 0)
			field = E_CONTACT_FIELD_LAST;
		else 
			field = e_contact_field_id (query_name);

	} else if (argc == 3 &&
		   argv[0]->type == ESEXP_RES_STRING &&
		   argv[1]->type == ESEXP_RES_STRING &&
		   argv[2]->type == ESEXP_RES_STRING) {
		query_name  = argv[0]->value.string;
		query_value = argv[1]->value.string;
		query_extra = argv[2]->value.string;

		field = e_contact_field_id (query_name);
	}

	if (IS_QUERY_PHONE (check_data.query_type)) {
		QueryPhoneTest *test;

		/* Collect data from this field test */
		test = query_phone_test_new (check_data.query_type, field);
		test->has_value = (query_value && query_value[0]);
		test->has_extra = (query_extra && query_extra[0]);

		/* For phone numbers, we need to collect the strings regardless,
		 * just to pass the preflight check and validate the query
		 */
		test->value = g_strdup (query_value);
		test->region = g_strdup (query_extra);

		element = (QueryElement *)test;
	} else {
		QueryFieldTest *test;

		/* Collect data from this field test */
		test = query_field_test_new (check_data.query_type, field);
		test->has_value = (query_value && query_value[0]);
		test->has_extra = (query_extra && query_extra[0]);

		/* We avoid collecting strings unless we're going to use them */
		if ((check_data.flags & PREFLIGHT_FLAG_STR_COLLECT) != 0) {
			test->value = g_strdup (query_value);
		}

		element = (QueryElement *)test;
	}

	/* Return an array with only one element, for lack of a pointer type ESExpResult */
	result_array = g_ptr_array_new_with_free_func ((GDestroyNotify)query_element_free);
	g_ptr_array_add (result_array, element);

	result = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
	result->value.ptrarray = result_array;

	return result;
}

/* Initial stage of preflighting, parse the search
 * expression and generate our array of QueryElements
 */
static void
query_preflight_initialize (PreflightContext *context,
			    const gchar *sexp,
			    PreflightFlags flags)
{
	ESExp *sexp_parser;
	ESExpResult *result;
	gint esexp_error, i;

	context->flags = flags;

	if (sexp == NULL || *sexp == '\0') {
		context->list_all = TRUE;
		return;
	}

	sexp_parser = e_sexp_new ();

	for (i = 0; i < G_N_ELEMENTS (check_symbols); i++) {
		CheckFuncData check_data;
		guint uint_data;

		check_data.query_type = check_symbols[i].test;
		check_data.flags      = context->flags;
		memcpy (&uint_data, &check_data, sizeof (guint));

		if (check_symbols[i].subset) {
			e_sexp_add_ifunction (
				sexp_parser, 0, check_symbols[i].name,
				func_check_subset,
				GUINT_TO_POINTER (uint_data));
		} else {
			e_sexp_add_function (
				sexp_parser, 0, check_symbols[i].name,
				func_check,
				GUINT_TO_POINTER (uint_data));
		}
	}

	e_sexp_input_text (sexp_parser, sexp, strlen (sexp));
	esexp_error = e_sexp_parse (sexp_parser);

	if (esexp_error == -1) {
		context->status = PREFLIGHT_INVALID;
	} else {

		result = e_sexp_eval (sexp_parser);
		if (result) {

			if (result->type == ESEXP_RES_ARRAY_PTR) {

				/* Just steal the array away from the ESexpResult */
				context->constraints   = result->value.ptrarray;
				result->value.ptrarray = NULL;

			} else {
				context->status = PREFLIGHT_INVALID;
			}
		}

		e_sexp_result_free (sexp_parser, result);
	}

	e_sexp_unref (sexp_parser);
}

typedef struct {
	EBookSqlite   *ebsql;
	gboolean              has_attr_list;
} AttrListCheckData;

static gboolean
check_has_attr_list_cb (QueryElement *element,
			gint          sub_level,
			gint          offset,
			gpointer      user_data)
{
	QueryFieldTest *test = (QueryFieldTest *)element;
	AttrListCheckData *data = (AttrListCheckData *)user_data;

	/* We havent resolved all the fields at this stage yet */
	if (!test->field)
		test->field = summary_field_get (data->ebsql, test->field_id);

	if (test->field && test->field->type == E_TYPE_CONTACT_ATTR_LIST)
		data->has_attr_list = TRUE;

	/* Keep looping until we find one */
	return (data->has_attr_list == FALSE);
}

/* This pass resolves values on the QueryElements useful
 * for actually performing the query, furthermore it resolves
 * whether the query can be performed on the SQLite tables or not.
 */
static void
query_preflight_check (PreflightContext     *context,
		       EBookSqlite   *ebsql)
{
	gint i, n_elements;
	QueryElement **elements;

	context->status = PREFLIGHT_OK;

	elements   = (QueryElement **)context->constraints->pdata;
	n_elements = context->constraints->len;

	for (i = 0; i < n_elements; i++) {
		QueryFieldTest *test;
		guint           field_test;

		/* We don't care about the subquery delimiters at this point */
		if (elements[i]->query >= BOOK_QUERY_SUB_FIRST) {

			/* It's too complicated to properly perform
			 * the unary NOT operator on a constraint which
			 * accesses attribute lists.
			 *
			 * Hint, if the contact has a "%.com" email address
			 * and a "%.org" email address, what do we return
			 * for (not (endswith "email" ".com") ?
			 *
			 * Currently we rely on DISTINCT to sort out
			 * muliple results from the attribute list tables,
			 * this breaks down with NOT.
			 */
			if (elements[i]->query == BOOK_QUERY_SUB_NOT) {
				AttrListCheckData data = { ebsql, FALSE };

				query_preflight_foreach_sub (elements,
							     n_elements,
							     i, FALSE,
							     check_has_attr_list_cb,
							     &data);

				if (data.has_attr_list)
					context->status = MAX (context->status,
							       PREFLIGHT_NOT_SUMMARIZED);
			}

			continue;
		}

		test = (QueryFieldTest *) elements[i];
		field_test = (EBookQueryTest)test->query;

		if (!test->field)
			test->field = summary_field_get (ebsql, test->field_id);

		/* Even if the field is not in the summary, we need to 
		 * retport unsupported errors if phone number queries are
		 * issued while libphonenumber is unavailable
		 */
		if (!test->field) {

			/* Special case for e_book_query_any_field_contains().
			 *
			 * We interpret 'x-evolution-any-field' as E_CONTACT_FIELD_LAST
			 */
			if (test->field_id == E_CONTACT_FIELD_LAST) {

				/* If we search for a NULL or zero length string, it 
				 * means 'get all contacts', that is considered a summary
				 * query but is handled differently (i.e. we just drop the
				 * field tests and run a regular query).
				 *
				 * This is only true if the 'any field contains' query is
				 * the only test in the constraints, however.
				 */
				if (!test->has_value && n_elements == 1) {

					context->list_all = TRUE;

				} else {

					/* Searching for a value with 'x-evolution-any-field' is
					 * not a summary query.
					 */
					context->status = MAX (context->status, PREFLIGHT_NOT_SUMMARIZED);
				}

			} else {

				/* Couldnt resolve the field, it's not a summary query */
				context->status = MAX (context->status, PREFLIGHT_NOT_SUMMARIZED);
			}
		}

		switch (field_test) {
		case BOOK_QUERY_EXISTS:
		case E_BOOK_QUERY_IS:
		case E_BOOK_QUERY_CONTAINS:
		case E_BOOK_QUERY_BEGINS_WITH:
		case E_BOOK_QUERY_ENDS_WITH:
		case E_BOOK_QUERY_REGEX_NORMAL:

			/* All of these queries can only apply to string fields,
			 * or fields which hold multiple strings 
			 */
			if (test->field) {

				if (test->field->type != G_TYPE_STRING &&
				    test->field->type != E_TYPE_CONTACT_ATTR_LIST)
					context->status = MAX (context->status, PREFLIGHT_INVALID);
			}

			break;

		case E_BOOK_QUERY_REGEX_RAW:
			/* Raw regex queries only supported in the fallback */
			context->status = MAX (context->status, PREFLIGHT_NOT_SUMMARIZED);
			break;

		case E_BOOK_QUERY_EQUALS_PHONE_NUMBER:
		case E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER:
		case E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER:

			/* Phone number queries are supported so long as they are in the summary,
			 * libphonenumber is available, and the phone number string is a valid one
			 */
			if (!e_phone_number_is_supported ()) {

				context->status = MAX (context->status, PREFLIGHT_UNSUPPORTED);

			} else {
				QueryPhoneTest *phone_test = (QueryPhoneTest *)test;
				EPhoneNumberCountrySource source;
				EPhoneNumber *number;
				const gchar *region_code;

				if (phone_test->region)
					region_code = phone_test->region;
				else
					region_code = ebsql->priv->region_code;

				number = e_phone_number_from_string (phone_test->value,
								     region_code, NULL);

				if (number == NULL) {

					context->status = MAX (context->status, PREFLIGHT_INVALID);

				} else {
					/* Collect values we'll need later while generating field
					 * tests, no need to parse the phone number more than once
					 */
					if ((context->flags & PREFLIGHT_FLAG_STR_COLLECT) != 0) {

						phone_test->national = e_phone_number_get_national_number (number);
						phone_test->country = e_phone_number_get_country_code (number, &source);

						if (source == E_PHONE_NUMBER_COUNTRY_FROM_DEFAULT)
							phone_test->country = 0;
					}

					e_phone_number_free (number);
				}
			}
			break;
		}

		if ((context->flags & PREFLIGHT_FLAG_AUX_COLLECT) != 0 &&
		    test->field &&
		    test->field->type == E_TYPE_CONTACT_ATTR_LIST) {

			PreflightAuxData *aux_data;

			aux_data = preflight_context_search_aux (context, test->field_id);
			if (!aux_data) {
				aux_data = preflight_aux_data_new (test->field_id);
				context->aux_fields = 
					g_slist_prepend (context->aux_fields, aux_data);
			}
		}
	}

	/* If we cannot satisfy this query with the summary, there is no point
	 * to return the allocated list */
	if (context->status > PREFLIGHT_OK) {
		g_slist_free_full (context->aux_fields,
				   (GDestroyNotify)preflight_aux_data_free);
		context->aux_fields = NULL;
	}
}

/* Handle special case of E_CONTACT_FULL_NAME
 *
 * For any query which accesses the full name field,
 * we need to also OR it with any of the related name
 * fields, IF those are found in the summary as well.
 */
static void
query_preflight_substitute_full_name (PreflightContext     *context,
				      EBookSqlite   *ebsql)
{
	gint i, j;

	for (i = 0; i < context->constraints->len; i++) {
		SummaryField *family_name, *given_name, *nickname;
		QueryElement *element;
		QueryFieldTest *test;

		element = g_ptr_array_index (context->constraints, i);

		if (element->query >= BOOK_QUERY_SUB_FIRST)
			continue;

		test = (QueryFieldTest *) element;
		if (test->field_id != E_CONTACT_FULL_NAME)
			continue;

		family_name = summary_field_get (ebsql, E_CONTACT_FAMILY_NAME);
		given_name  = summary_field_get (ebsql, E_CONTACT_GIVEN_NAME);
		nickname    = summary_field_get (ebsql, E_CONTACT_NICKNAME);

		/* If any of these are in the summary, then we'll construct
		 * a grouped OR statment for this E_CONTACT_FULL_NAME test */
		if (family_name || given_name || nickname) {
			/* Add the OR directly before the E_CONTACT_FULL_NAME test */
			constraints_insert_delimiter (context->constraints, i, BOOK_QUERY_SUB_OR);


			j = i + 2;

			if (family_name)
				constraints_insert_field_test (context->constraints, j++,
							       family_name, test->query,
							       test->value);

			if (given_name)
				constraints_insert_field_test (context->constraints, j++,
							       given_name, test->query,
							       test->value);

			if (nickname)
				constraints_insert_field_test (context->constraints, j++,
							       nickname, test->query,
							       test->value);

			constraints_insert_delimiter (context->constraints, j, BOOK_QUERY_SUB_END);

			i = j;
		}
	}
}

/* Migrates the chunk of the constraints at 'offset' into one of the
 * PreflightAuxData indicated by aux_field.
 *
 * Returns the number of QueryElements which have been removed from
 * the main constraints
 */
static gint
query_preflight_migrate_offset (PreflightContext     *context,
				EContactField         aux_field,
				gint                  offset)
{
	PreflightAuxData *aux_data;
	QueryElement **elements;
	gint sub_counter = 0;
	gint dest_offset = 0;
	gint n_migrated = 0;

	aux_data = preflight_context_search_aux (context, aux_field);
	g_return_val_if_fail (aux_data != NULL, 0);

	if (!aux_data->constraints) {

		/* We created a new batch for 'aux_field',
		 * we'll be adding this batch directly to the beginning
		 */
		aux_data->constraints = g_ptr_array_new_with_free_func ((GDestroyNotify)query_element_free);

	} else {
		elements = (QueryElement **)aux_data->constraints->pdata;

		/* If we're migrating a second or third constraint, we must ensure that
		 * it's encapsulated with an AND
		 */
		if (elements[0]->query != BOOK_QUERY_SUB_AND) {
			constraints_insert_delimiter (aux_data->constraints,  0, BOOK_QUERY_SUB_AND);
			constraints_insert_delimiter (aux_data->constraints, -1, BOOK_QUERY_SUB_END);
		}

		/* We're going to insert this starting at position 1 (directly after opening the AND)
		 * The order of AND statements in the query is inconsequential.
		 */
		dest_offset = 1;
	}

	elements = (QueryElement **)context->constraints->pdata;
	do {
		QueryElement *element;

		/* Migrate one element */
		element = constraints_take (context->constraints, offset);
		constraints_insert (aux_data->constraints, dest_offset++, element);

		n_migrated++;

		/* If we migrated a group... migrate the whole group */
		if (element->query == BOOK_QUERY_SUB_END)
			sub_counter--;
		else if (element->query >= BOOK_QUERY_SUB_FIRST)
			sub_counter++;

	} while (context->constraints->len > offset && sub_counter > 0);


	/* Return the number of elements removed from the main constraints */
	return n_migrated;
}

/* Will set the EContactField to 0 if it's completely isolated
 * to the summary table, E_CONTACT_FIELD_LAST if it's not isolated,
 * or another attribute list type EContactField if it's isolated
 * to that field.
 *
 * Expects the initial value to be 'E_CONTACT_FIELD_LAST + 1'
 */
static gboolean
check_isolated_cb (QueryElement *element,
		   gint          sub_level,
		   gint          offset,
		   gpointer      user_data)
{
	EContactField *field_id = (EContactField *)user_data;
	QueryFieldTest *test = (QueryFieldTest *)element;

	if (*field_id > E_CONTACT_FIELD_LAST) {

		/* First field encountered, let's see what it is... */
		if (test->field->type == E_TYPE_CONTACT_ATTR_LIST)
			*field_id = test->field_id;
		else
			*field_id = 0;

		return TRUE;

	} else if (*field_id == 0) {

		if (test->field->type == E_TYPE_CONTACT_ATTR_LIST) {

			/* Oops, summary and auxiliary encountered */
			*field_id = E_CONTACT_FIELD_LAST;
			return FALSE;
		}

	} else if (test->field_id != *field_id) {
		/* Auxiliary and something else encountered */
		*field_id = E_CONTACT_FIELD_LAST;
		return FALSE;
	}

	return TRUE;
}

/* fetch_sub_groups_cb will list the index of each component of a sub,
 * unless not every subgroup was isolated, in which case the
 * PreflightAndData->isolated will be set to FALSE.
 */
typedef struct {
	QueryElement **elements;
	gint           n_elements;

	gboolean       isolated;
	gboolean       checked;

	GSList        *offsets;
	GSList        *fields;
} PreflightAndData;

static gboolean
fetch_sub_groups_cb (QueryElement *element,
		     gint          sub_level,
		     gint          offset,
		     gpointer      user_data)
{
	PreflightAndData *data = (PreflightAndData *)user_data;

	data->checked = TRUE;

	if (sub_level == 1 && element->query < BOOK_QUERY_SUB_FIRST) {

		QueryFieldTest *test = (QueryFieldTest *)element;

		data->offsets =
			g_slist_prepend (data->offsets,
					 GINT_TO_POINTER (offset));
		data->fields =
			g_slist_prepend (data->fields,
					 GUINT_TO_POINTER (test->field_id));

	} else if (sub_level == 2 &&
		   element->query >= BOOK_QUERY_SUB_FIRST &&
		   element->query != BOOK_QUERY_SUB_END) {

		EContactField field_id = E_CONTACT_FIELD_LAST + 1;

		query_preflight_foreach_sub (data->elements,
					     data->n_elements,
					     offset, FALSE,
					     check_isolated_cb,
					     &field_id);

		if (field_id == E_CONTACT_FIELD_LAST) {
			data->isolated = FALSE;
		} else {
			data->offsets =
				g_slist_prepend (data->offsets,
						 GINT_TO_POINTER (offset));
			data->fields =
				g_slist_prepend (data->fields,
						 GUINT_TO_POINTER (field_id));
		}
	}

	return (data->isolated != FALSE);
}

static void
query_preflight_optimize_and (PreflightContext     *context,
			      EBookSqlite   *ebsql,
			      QueryElement        **elements,
			      gint                  n_elements)
{
	PreflightAndData data = { elements, n_elements, TRUE, FALSE, NULL, NULL };

	/* First, find the indexes to the various toplevel elements */
	query_preflight_foreach_sub (elements,
				     n_elements,
				     0, TRUE,
				     fetch_sub_groups_cb,
				     &data);

	if (data.checked && data.isolated) {
		GSList *l, *ll;
		gint n_migrated = 0;
		gint remaining;

		/* Lists are created in reverse, with higher offsets
		 * comming first, let's keep it this way.
		 *
		 * We can now migrate them one by one and the later
		 * offsets (i.e. lower offsets) in the list will not
		 * be invalid. This order should also reduce calls
		 * to memmove().
		 */
		for (l = data.offsets, ll = data.fields;
		     l && ll;
		     l = l->next, ll = ll->next) {
			gint          offset   = GPOINTER_TO_INT (l->data);
			EContactField field_id = GPOINTER_TO_UINT (ll->data);
			SummaryField *field;

			field = summary_field_get (ebsql, field_id);

			if (field && field->type == E_TYPE_CONTACT_ATTR_LIST) {
				n_migrated++;
				query_preflight_migrate_offset (context, field_id, offset);
			}
		}

		/* If there is only one statement left inside the AND clause
		 * in context->constraints, we need to remove the encapsulating
		 * AND statement.
		 */
		remaining = g_slist_length (data.offsets) - n_migrated;
		if (remaining < 2) {
			g_ptr_array_remove_index (context->constraints, 0);
			g_ptr_array_remove_index (context->constraints,
						  context->constraints->len - 1);
		}
	}

	g_slist_free (data.offsets);
	g_slist_free (data.fields);
}

static void
query_preflight_optimize_toplevel (PreflightContext     *context,
				   EBookSqlite   *ebsql,
				   QueryElement        **elements,
				   gint                  n_elements)
{
	EContactField field_id;

	if (elements[0]->query >= BOOK_QUERY_SUB_FIRST) {

		switch (elements[0]->query) {
		case BOOK_QUERY_SUB_AND:

			/* AND components at the toplevel can be migrated, so long
			 * as each component is isolated
			 */
			query_preflight_optimize_and (context, ebsql, elements, n_elements);
			break;

		case BOOK_QUERY_SUB_OR:

			/* OR at the toplevel can be migrated if limited to one table */
			field_id = E_CONTACT_FIELD_LAST + 1;
			query_preflight_foreach_sub (elements,
						     n_elements,
						     0, FALSE,
						     check_isolated_cb,
						     &field_id);

			if (field_id != 0 &&
			    field_id != E_CONTACT_FIELD_LAST) {

				/* Isolated to an auxiliary table, let's migrate it */
				query_preflight_migrate_offset (context, field_id, 0);
			}
			break;

		case BOOK_QUERY_SUB_NOT:

			/* We dont support NOT operations on attribute lists as
			 * summarized queries, so there can not be any optimization
			 * made here.
			 */
			break;

		case BOOK_QUERY_SUB_END:
		default:
			g_warn_if_reached ();
			break;
		}

	} else {

		QueryFieldTest *test = (QueryFieldTest *)elements[0];

		/* Toplevel field test should stand alone at the first position */

		/* Special case of 'x-evolution-any-field' will have no SummaryField
		 * resolved in test->field
		 */
		if (test->field && test->field->type == E_TYPE_CONTACT_ATTR_LIST)
			query_preflight_migrate_offset (context, test->field_id, 0);
	}
}

/* In this phase, we attempt to pull out field tests from the main constraints array
 * which touch auxiliary tables and place them instead into their PreflightAuxData
 * constraint arrays respectively.
 *
 * This will result in queries being generated using nested select statements before joining,
 * allowing us to leverage the indexes we created in those.
 *
 * A query which would normally generate like this:
 * ================================================
 * SELECT DISTINCT summary.uid, summary.vcard FROM 'folder_id' AS summary
 * LEFT OUTER JOIN 'folder_id_phone_list' AS phone_list ON phone_list.uid = summary.uid
 * LEFT OUTER JOIN 'folder_id_email_list' AS email_list ON email_list.uid = summary.uid
 *    WHERE (phone_list.value_reverse IS NOT NULL AND phone_list.value_reverse LIKE '0505%')
 *    AND (email_list.value IS NOT NULL AND email_list.value LIKE 'eddie%')
 *
 * After optimization, will be generated instead like so:
 * ================================================
 * SELECT DISTINCT summary.uid, summary.vcard FROM (
 *      SELECT DISTINCT phone_list.uid FROM 'folder_id_phone_list' AS phone_list
 *      WHERE (phone_list.value_reverse IS NOT NULL AND phone_list.value_reverse LIKE '0505%') 
 *    ) AS phone_list_results
 * LEFT OUTER JOIN (
 *      SELECT DISTINCT email_list.uid FROM 'folder_id_email_list' AS email_list
 *      WHERE (email_list.value IS NOT NULL AND email_list.value LIKE 'eddie%') 
 *    ) AS email_list_results ON email_list_results.uid = phone_list_results.uid 
 * LEFT OUTER JOIN 'folder_id' AS summary ON summary.uid = email_list_results.uid
 *     WHERE summary.uid IS NOT NULL
 *
 * Currently we make the following assumptions when optimizing the query:
 *
 *   o Any shallow query with only one auxiliary table constraint can have
 *     the auxiliary constraint migrated into the nested select
 *
 *   o Any grouped query which contains constraints which access the same
 *     table can be considered an atomic constraint and is a suitable target
 *     for migration.
 *
 *   o Any toplevel AND query which contains one or more summary table constraints
 *     and one or more auxiliary table constraints, can have the auxiliary
 *     table constraints migrated into the nested select.
 *
 */
static void
query_preflight_optimize (PreflightContext     *context,
			  EBookSqlite   *ebsql)
{
	QueryElement **elements;
	gint n_elements;

	if (context->constraints &&
	    context->constraints->len > 0) {

		elements   = (QueryElement **)context->constraints->pdata;
		n_elements = context->constraints->len;

		query_preflight_optimize_toplevel (context, ebsql, elements, n_elements);
	}


	/* In any case that we did have constraints, add an (exists "uid") constraint
	 * to the end, this is because it's possible for the optimization above to return
	 * some NULL rows
	 */
	if (context->constraints &&
	    context->constraints->len == 0) {
		constraints_insert_field_test (context->constraints, 0,
					       summary_field_get (ebsql, E_CONTACT_UID),
					       BOOK_QUERY_EXISTS, NULL);
	} else {
		/* AND it with the remaining constraints */
		constraints_insert_delimiter (context->constraints,  0, BOOK_QUERY_SUB_AND);
		constraints_insert_field_test (context->constraints, -1,
					       summary_field_get (ebsql, E_CONTACT_UID),
					       BOOK_QUERY_EXISTS, NULL);
		constraints_insert_delimiter (context->constraints, -1, BOOK_QUERY_SUB_END);
	}
}

static void
query_preflight_for_sql_query (PreflightContext   *context,
			       EBookSqlite *ebsql,
			       const gchar        *sexp)
{
	query_preflight_initialize (context, sexp, 
				    PREFLIGHT_FLAG_STR_COLLECT |
				    PREFLIGHT_FLAG_AUX_COLLECT);

	if (context->list_all == FALSE &&
	    context->status == PREFLIGHT_OK) {

		query_preflight_check (context, ebsql);

		/* No need to change the constraints if we're not
		 * going to generate statements with it
		 */
		if (context->status == PREFLIGHT_OK) {

			/* Handle E_CONTACT_FULL_NAME substitutions */
			query_preflight_substitute_full_name (context, ebsql);

			/* Optimize queries which touch auxiliary columns */
			query_preflight_optimize (context, ebsql);
		} else {

			/* We might use this context to perform a fallback query,
			 * so let's clear out all the constraints now
			 */
			preflight_context_clear (context);
		}
	}

	if (context->status > PREFLIGHT_NOT_SUMMARIZED)
		context->list_all = FALSE;
}

/**********************************************************
 *                 Field Test Generators                  *
 **********************************************************
 *
 * This section contains the field test generators for
 * various EBookQueryTest types. When implementing new
 * query types, a new GenerateFieldTest needs to be created
 * and added to the table below.
 */

typedef void (* GenerateFieldTest) (EBookSqlite *ebsql,
				    GString            *string,
				    QueryFieldTest     *test);

/* This function escapes characters which need escaping
 * for LIKE statements as well as the single quotes.
 *
 * The return value is not suitable to be formatted
 * with %Q or %q
 */
static gchar *
ebsql_normalize_for_like (QueryFieldTest *test,
			  gboolean reverse_string,
			  gboolean *escape_needed)
{
	GString *str;
	size_t len;
	gchar c;
	gboolean escape_modifier_needed = FALSE;
	const gchar *normal = NULL;
	const gchar *ptr;
	const gchar *str_to_escape;
	gchar *reverse = NULL;
	gchar *freeme = NULL;

	if (test->field_id == E_CONTACT_UID ||
	    test->field_id == E_CONTACT_REV) {
		normal = test->value;
	} else {
		freeme = e_util_utf8_normalize (test->value);
		normal = freeme;
	}

	if (reverse_string) {
		reverse = g_utf8_strreverse (normal, -1);
		str_to_escape = reverse;
	} else
		str_to_escape = normal;

	/* Just assume each character must be escaped. The result of this function
	 * is discarded shortly after calling this function. Therefore it's
	 * acceptable to possibly allocate twice the memory needed.
	 */
	len = strlen (str_to_escape);
	str = g_string_sized_new (2 * len + 4 + strlen (EBSQL_ESCAPE_SEQUENCE) - 1);

	ptr = str_to_escape;
	while ((c = *ptr++)) {
		if (c == '\'') {
			g_string_append_c (str, '\'');
		} else if (c == '%' || c == '_' || c == '^') {
			g_string_append_c (str, '^');
			escape_modifier_needed = TRUE;
		}

		g_string_append_c (str, c);
	}

	if (escape_needed)
		*escape_needed = escape_modifier_needed;

	g_free (freeme);
	g_free (reverse);

	return g_string_free (str, FALSE);
}

static void
field_test_query_is (EBookSqlite        *ebsql,
		     GString            *string,
		     QueryFieldTest     *test)
{
	SummaryField *field = test->field;
	gchar *normal;

	ebsql_string_append_column (string, field, NULL);

	if (test->field_id == E_CONTACT_UID ||
	    test->field_id == E_CONTACT_REV) {
		/* UID & REV fields are not normalized in the summary */
		ebsql_string_append_printf (string, " = %Q", test->value);
	} else {
		normal = e_util_utf8_normalize (test->value);
		ebsql_string_append_printf (string, " = %Q", normal);
		g_free (normal);
	}
}

static void
field_test_query_contains (EBookSqlite        *ebsql,
			   GString            *string,
			   QueryFieldTest     *test)
{
	SummaryField *field = test->field;
	gboolean need_escape;
	gchar *escaped;

	escaped = ebsql_normalize_for_like (test, FALSE, &need_escape);

	g_string_append_c (string, '(');

	ebsql_string_append_column (string, field, NULL);
	g_string_append (string, " IS NOT NULL AND ");
	ebsql_string_append_column (string, field, NULL);
	g_string_append (string, " LIKE '%");
	g_string_append (string, escaped);
	g_string_append (string, "%'");

	if (need_escape)
		g_string_append (string, EBSQL_ESCAPE_SEQUENCE);

	g_string_append_c (string, ')');

	g_free (escaped);
}

static void
field_test_query_begins_with (EBookSqlite        *ebsql,
			      GString            *string,
			      QueryFieldTest     *test)
{
	SummaryField *field = test->field;
	gboolean need_escape;
	gchar *escaped;

	escaped = ebsql_normalize_for_like (test, FALSE, &need_escape);

	g_string_append_c (string, '(');
	ebsql_string_append_column (string, field, NULL);
	g_string_append (string, " IS NOT NULL AND ");

	ebsql_string_append_column (string, field, NULL);
	g_string_append (string, " LIKE \'");
	g_string_append (string, escaped);
	g_string_append (string, "%\'");

	if (need_escape)
		g_string_append (string, EBSQL_ESCAPE_SEQUENCE);
	g_string_append_c (string, ')');

	g_free (escaped);
}

static void
field_test_query_ends_with (EBookSqlite        *ebsql,
			    GString            *string,
			    QueryFieldTest     *test)
{
	SummaryField *field = test->field;
	gboolean need_escape;
	gchar *escaped;

	if ((field->index & INDEX_FLAG (SUFFIX)) != 0) {

		escaped = ebsql_normalize_for_like (test,
						    TRUE, &need_escape);

		g_string_append_c (string, '(');
		ebsql_string_append_column (string, field, EBSQL_SUFFIX_REVERSE);
		g_string_append (string, " IS NOT NULL AND ");

		ebsql_string_append_column (string, field, EBSQL_SUFFIX_REVERSE);
		g_string_append (string, " LIKE \'");
		g_string_append (string, escaped);
		g_string_append (string, "%\'");

	} else {

		escaped = ebsql_normalize_for_like (test,
						    FALSE, &need_escape);
		g_string_append_c (string, '(');

		ebsql_string_append_column (string, field, NULL);
		g_string_append (string, " IS NOT NULL AND ");

		ebsql_string_append_column (string, field, NULL);
		g_string_append (string, " LIKE \'%");
		g_string_append (string, escaped);
		g_string_append (string, "\'");
	}

	if (need_escape)
		g_string_append (string, EBSQL_ESCAPE_SEQUENCE);

	g_string_append_c (string, ')');
	g_free (escaped);
}

static void
field_test_query_eqphone (EBookSqlite        *ebsql,
			  GString            *string,
			  QueryFieldTest     *test)
{
	SummaryField *field = test->field;
	QueryPhoneTest *phone_test = (QueryPhoneTest *)test;

	if ((field->index & INDEX_FLAG (PHONE)) != 0) {

		g_string_append_c (string, '(');
		ebsql_string_append_column (string, field, EBSQL_SUFFIX_PHONE);
		ebsql_string_append_printf (string, " = %Q AND ", phone_test->national);

		/* For exact matches, a country code qualifier is required by both
		 * query input and row input
		 */
		ebsql_string_append_column (string, field, EBSQL_SUFFIX_COUNTRY);
		g_string_append (string, " != 0 AND ");

		ebsql_string_append_column (string, field, EBSQL_SUFFIX_COUNTRY);
		ebsql_string_append_printf (string, " = %d", phone_test->country);
		g_string_append_c (string, ')');

	} else {

		/* No indexed columns available, perform the fallback */
		g_string_append (string, EBSQL_FUNC_EQPHONE_EXACT " (");
		ebsql_string_append_column (string, field, NULL);
		ebsql_string_append_printf (string, ", %Q)", test->value);
	}
}

static void
field_test_query_eqphone_national (EBookSqlite        *ebsql,
				   GString            *string,
				   QueryFieldTest     *test)
{

	SummaryField *field = test->field;
	QueryPhoneTest *phone_test = (QueryPhoneTest *)test;

	if ((field->index & INDEX_FLAG (PHONE)) != 0) {

		/* Only a compound expression if there is a country code */
		if (phone_test->country)
			g_string_append_c (string, '(');

		/* Generate: phone = %Q */
		ebsql_string_append_column (string, field, EBSQL_SUFFIX_PHONE);
		ebsql_string_append_printf (string, " = %Q", phone_test->national);

		/* When doing a national search, no need to check country
		 * code unless the query number also has a country code
		 */
		if (phone_test->country) {
			/* Generate: (phone = %Q AND (country = 0 OR country = %d)) */
			g_string_append (string, " AND (");
			ebsql_string_append_column (string, field, EBSQL_SUFFIX_COUNTRY);
			g_string_append (string, " = 0 OR ");
			ebsql_string_append_column (string, field, EBSQL_SUFFIX_COUNTRY);
			ebsql_string_append_printf (string, " = %d))", phone_test->country);

		}

	} else {

		/* No indexed columns available, perform the fallback */
		g_string_append (string, EBSQL_FUNC_EQPHONE_NATIONAL " (");
		ebsql_string_append_column (string, field, NULL);
		ebsql_string_append_printf (string, ", %Q)", test->value);
	}
}

static void
field_test_query_eqphone_short (EBookSqlite        *ebsql,
				GString            *string,
				QueryFieldTest     *test)
{
	SummaryField *field = test->field;

	/* No quick way to do the short match */
	g_string_append (string, EBSQL_FUNC_EQPHONE_SHORT " (");
	ebsql_string_append_column (string, field, NULL);
	ebsql_string_append_printf (string, ", %Q)", test->value);
}

static void
field_test_query_regex_normal (EBookSqlite        *ebsql,
			       GString            *string,
			       QueryFieldTest     *test)
{
	SummaryField *field = test->field;
	gchar *normal;

	normal = e_util_utf8_normalize (test->value);

	if (field->aux_table)
		ebsql_string_append_printf (string, "%s.value REGEXP %Q",
					    field->aux_table_symbolic,
					    normal);
	else
		ebsql_string_append_printf (string, "summary.%s REGEXP %Q",
					    field->dbname,
					    normal);

	g_free (normal);
}

static void
field_test_query_exists (EBookSqlite        *ebsql,
			 GString            *string,
			 QueryFieldTest     *test)
{
	SummaryField *field = test->field;

	ebsql_string_append_column (string, field, NULL);
	ebsql_string_append_printf (string, " IS NOT NULL");
}

/* Lookup table for field test generators per EBookQueryTest,
 *
 * WARNING: This must stay in line with the EBookQueryTest definition.
 */
static const GenerateFieldTest field_test_func_table[] = {
	field_test_query_is,               /* E_BOOK_QUERY_IS */
	field_test_query_contains,         /* E_BOOK_QUERY_CONTAINS */
	field_test_query_begins_with,      /* E_BOOK_QUERY_BEGINS_WITH */
	field_test_query_ends_with,        /* E_BOOK_QUERY_ENDS_WITH */
	field_test_query_eqphone,          /* E_BOOK_QUERY_EQUALS_PHONE_NUMBER */
	field_test_query_eqphone_national, /* E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER */
	field_test_query_eqphone_short,    /* E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER */
	field_test_query_regex_normal,     /* E_BOOK_QUERY_REGEX_NORMAL */
	NULL /* Requires fallback */,      /* E_BOOK_QUERY_REGEX_RAW  */
	field_test_query_exists,           /* BOOK_QUERY_EXISTS */
};

/**********************************************************
 *                   Querying Contacts                    *
 **********************************************************/

/* The various search types indicate what should be fetched
 */
typedef enum {
	SEARCH_FULL,          /* Get a list of EbSqlSearchData */
	SEARCH_UID_AND_REV,   /* Get a list of EbSqlSearchData, with shallow vcards only containing UID & REV */
	SEARCH_UID,           /* Get a list of UID strings */
	SEARCH_COUNT,         /* Get the number of matching rows */
} SearchType;

static void
ebsql_generate_constraints (EBookSqlite *ebsql,
			    GString *string,
			    GPtrArray *constraints,
			    const gchar *sexp)
{
	SubQueryContext *ctx;
	QueryDelimiter *delim;
	QueryFieldTest *test;
	QueryElement **elements;
	gint n_elements, i;

	/* If there are no constraints, we generate the fallback constraint for 'sexp' */
	if (constraints == NULL) {
		ebsql_string_append_printf (
			string, 
			EBSQL_FUNC_COMPARE_VCARD " (%Q, %s)",
			sexp, EBSQL_VCARD_FRAGMENT (ebsql));
		return;
	}

	elements   = (QueryElement **)constraints->pdata;
	n_elements = constraints->len;

	ctx = sub_query_context_new();

	for (i = 0; i < n_elements; i++) {
		GenerateFieldTest generate_test_func = NULL;

		/* Seperate field tests with the appropriate grouping */
		if (elements[i]->query != BOOK_QUERY_SUB_END &&
		    sub_query_context_increment (ctx) > 0) {
			guint delim_type = sub_query_context_peek_type (ctx);

			switch (delim_type) {
			case BOOK_QUERY_SUB_AND:

				g_string_append (string, " AND ");
				break;

			case BOOK_QUERY_SUB_OR:

				g_string_append (string, " OR ");
				break;

			case BOOK_QUERY_SUB_NOT:

				/* Nothing to do between children of NOT,
				 * there should only ever be one child of NOT anyway 
				 */
				break;

			case BOOK_QUERY_SUB_END:
			default:
				g_warn_if_reached ();
			}
		}

		if (elements[i]->query >= BOOK_QUERY_SUB_FIRST) {
			delim = (QueryDelimiter *)elements[i];

			switch (delim->query) {

			case BOOK_QUERY_SUB_NOT:

				/* NOT is a unary operator and as such 
				 * comes before the opening parenthesis
				 */
				g_string_append (string, "NOT ");

				/* Fall through */

			case BOOK_QUERY_SUB_AND:
			case BOOK_QUERY_SUB_OR:

				/* Open a grouped statement and push the context */
				sub_query_context_push (ctx, delim->query);
				g_string_append_c (string, '(');
				break;

			case BOOK_QUERY_SUB_END:
				/* Close a grouped statement and pop the context */
				g_string_append_c (string, ')');
				sub_query_context_pop (ctx);
				break;
			default:
				g_warn_if_reached ();
			}

			continue;
		}

		/* Find the appropriate field test generator */
		test = (QueryFieldTest *)elements[i];
		if (test->query < G_N_ELEMENTS (field_test_func_table))
			generate_test_func = field_test_func_table[test->query];

		/* These should never happen, if it does it should be
		 * fixed in the preflight checks
		 */
		g_warn_if_fail (generate_test_func != NULL);
		g_warn_if_fail (test->field != NULL);

		/* Generate the field test */
		generate_test_func (ebsql, string, test);
	}

	sub_query_context_free (ctx);
}

/* Generates the SELECT portion of the query, this will possibly add some
 * of the constraints into nested selects. Constraints that could not be
 * nested will have their symbolic table names in context.
 *
 * This also handles getting the correct callback and asking for the
 * right data depending on the 'search_type'
 */
static EbSqlRowFunc
ebsql_generate_select (EBookSqlite *ebsql,
		       GString *string,
		       SearchType search_type,
		       PreflightContext *context,
		       GError **error)
{
	GSList *l;
	EbSqlRowFunc callback = NULL;
	gchar *previous_field = NULL;

	g_string_append (string, "SELECT ");
	if (context->aux_fields)
		g_string_append (string, "DISTINCT ");
 
	switch (search_type) {
	case SEARCH_FULL:
		callback = collect_full_results_cb;
		g_string_append (string, "summary.uid, ");
		g_string_append (string, EBSQL_VCARD_FRAGMENT (ebsql)); 
		g_string_append (string, ", summary.bdata ");
		break;
	case SEARCH_UID_AND_REV:
		callback = collect_lean_results_cb;
		g_string_append (string, "summary.uid, summary.Rev, summary.bdata ");
		break;
	case SEARCH_UID:
		callback = collect_uid_results_cb;
		g_string_append (string, "summary.uid ");
		break;
	case SEARCH_COUNT:
		callback = get_count_cb;
		if (context->aux_fields)
			g_string_append (string, "count (DISTINCT summary.uid) ");
		else
			g_string_append (string, "count (*) ");
		break;
	}

	g_string_append (string, "FROM ");

	for (l = context->aux_fields; l; l = l->next) {
		PreflightAuxData *aux_data = (PreflightAuxData *)l->data;
		SummaryField     *field    = summary_field_get (ebsql, aux_data->field_id);

		/* For every other query, start with the JOIN */
		if (previous_field)
			g_string_append (string, "LEFT OUTER JOIN ");

		if (aux_data->constraints) {
			/* Each nested select must be outer left joined on to
			 * the previous one, in this way the collection of joined
			 * tables is equal to a logical AND.
			 *
			 * See query_preflight_optimize() for more details.
			 */
			ebsql_string_append_printf (string,
						    "( SELECT DISTINCT %s.uid FROM %Q AS %s WHERE ",
						    field->aux_table_symbolic,
						    field->aux_table,
						    field->aux_table_symbolic);
			ebsql_generate_constraints (ebsql, string,
								  aux_data->constraints,
								  NULL);
			ebsql_string_append_printf (string, " ) AS %s_results ",
						    field->aux_table_symbolic);

			if (previous_field)
				ebsql_string_append_printf (string, "ON %s_results.uid = %s ",
							    field->aux_table_symbolic,
							    previous_field);

			g_free (previous_field);
			previous_field = g_strconcat (field->aux_table_symbolic,
						      "_results.uid", NULL);

		} else {
			/* Join the table in the normal way and leave the constraints for later */
			ebsql_string_append_printf (string, "%Q AS %s ",
						    field->aux_table,
						    field->aux_table_symbolic);

			if (previous_field)
				ebsql_string_append_printf (string, "ON %s.uid = %s ",
							    field->aux_table_symbolic,
							    previous_field);

			g_free (previous_field);
			previous_field = g_strconcat (field->aux_table_symbolic,
						      ".uid", NULL);
		}
	}

	if (previous_field)
		g_string_append (string, "LEFT OUTER JOIN ");

	ebsql_string_append_printf (string, "%Q AS summary ", ebsql->priv->folderid);
	if (previous_field)
		ebsql_string_append_printf (string, "ON summary.uid = %s ", previous_field);

	g_free (previous_field);

	return callback;
}

static gboolean
ebsql_do_search_query (EBookSqlite *ebsql,
		       PreflightContext *context,
		       const gchar *sexp,
		       SearchType search_type,
		       GSList **return_data,
		       GError **error)
{
	GString *string;
	EbSqlRowFunc callback = NULL;
	gboolean success = FALSE;

	/* We might calculate a reasonable estimation of bytes
	 * during the preflight checks */
	string = g_string_sized_new (GENERATED_QUERY_BYTES);

	/* Generate the leading SELECT statement */
	callback = ebsql_generate_select (ebsql,
					  string,
					  search_type,
					  context,
					  error);

	if (callback &&
	    context->list_all == FALSE) {
		/*
		 * Now generate the search expression on the main contacts table
		 */
		g_string_append (string, "WHERE ");
		ebsql_generate_constraints (ebsql,
					    string,
					    context->constraints,
					    sexp);
	}

	if (callback)
		success = ebsql_exec (ebsql, string->str,
				      callback, return_data,
				      error);

	g_string_free (string, TRUE);

	return success;
}

/* ebsql_search_query:
 * @ebsql: An EBookSqlite
 * @sexp: The search expression, or NULL for all contacts
 * @search_type: Indicates what kind of data should be returned
 * @return_data: A list of data fetched from the DB, as specified by 'search_type'
 * @error: Location to store any error which may have occurred
 *
 * This is the main common entry point for querying contacts.
 *
 * If the query cannot be satisfied with the summary, then
 * a fallback will automatically be used.
 */
static gboolean
ebsql_search_query (EBookSqlite *ebsql,
		    const gchar *sexp,
		    SearchType search_type,
		    GSList **return_data,
		    GError **error)
{
	PreflightContext context = PREFLIGHT_CONTEXT_INIT;
	gboolean success = FALSE;

	/* Now start with the query preflighting */
	query_preflight_for_sql_query (&context, ebsql, sexp);

	switch (context.status) {
	case PREFLIGHT_OK:
	case PREFLIGHT_NOT_SUMMARIZED:
		/* No errors, let's really search */
		success = ebsql_do_search_query (ebsql,
						 &context,
						 sexp,
						 search_type,
						 return_data,
						 error);
		break;

	case PREFLIGHT_INVALID:
		EBSQL_SET_ERROR (error,
				 E_BOOK_SQL_ERROR_INVALID_QUERY,
				 _("Invalid query: %s"), sexp);
		break;

	case PREFLIGHT_UNSUPPORTED:
		EBSQL_SET_ERROR_LITERAL (error,
					 E_BOOK_SQL_ERROR_NOT_SUPPORTED,
					 _("Query contained unsupported elements"));
		break;
	}

	preflight_context_clear (&context);

	return success;
}

/******************************************************************
 *                    EbSqlCursor Implementation                  *
 ******************************************************************/
typedef struct _CursorState CursorState;

struct _CursorState {
	gchar            **values;    /* The current cursor position, results will be returned after this position */
	gchar             *last_uid;  /* The current cursor contact UID position, used as a tie breaker */
	EbSqlCursorOrigin  position;  /* The position is updated with the cursor state and is used to distinguish
				       * between the beginning and the ending of the cursor's contact list.
				       * While the cursor is in a non-null state, the position will be 
				       * EBSQL_CURSOR_ORIGIN_CURRENT.
				       */
};

struct _EbSqlCursor {
	EBookBackendSExp *sexp;       /* An EBookBackendSExp based on the query, used by e_book_sqlite_cursor_compare() */
	gchar         *select_vcards; /* The first fragment when querying results */
	gchar         *select_count;  /* The first fragment when querying contact counts */
	gchar         *query;         /* The SQL query expression derived from the passed search expression */
	gchar         *order;         /* The normal order SQL query fragment to append at the end, containing ORDER BY etc */
	gchar         *reverse_order; /* The reverse order SQL query fragment to append at the end, containing ORDER BY etc */

	EContactField       *sort_fields;   /* The fields to sort in a query in the order or sort priority */
	EBookCursorSortType *sort_types;    /* The sort method to use for each field */
	gint                 n_sort_fields; /* The amound of sort fields */

	CursorState          state;
};

static CursorState *cursor_state_copy             (EbSqlCursor          *cursor,
						   CursorState          *state);
static void         cursor_state_free             (EbSqlCursor          *cursor,
						   CursorState          *state);
static void         cursor_state_clear            (EbSqlCursor          *cursor,
						   CursorState          *state,
						   EbSqlCursorOrigin     position);
static void         cursor_state_set_from_contact (EBookSqlite *ebsql,
						   EbSqlCursor          *cursor,
						   CursorState          *state,
						   EContact             *contact);
static void         cursor_state_set_from_vcard   (EBookSqlite *ebsql,
						   EbSqlCursor          *cursor,
						   CursorState          *state,
						   const gchar          *vcard);

static CursorState *
cursor_state_copy (EbSqlCursor        *cursor,
		   CursorState        *state)
{
	CursorState *copy;
	gint i;

	copy = g_slice_new0 (CursorState);
	copy->values = g_new0 (gchar *, cursor->n_sort_fields);

	for (i = 0; i < cursor->n_sort_fields; i++)
		copy->values[i] = g_strdup (state->values[i]);

	copy->last_uid = g_strdup (state->last_uid);
	copy->position = state->position;

	return copy;
}

static void
cursor_state_free (EbSqlCursor  *cursor,
		   CursorState  *state)
{
	if (state) {
		cursor_state_clear (cursor, state, EBSQL_CURSOR_ORIGIN_BEGIN);
		g_free (state->values);
		g_slice_free (CursorState, state);
	}
}

static void
cursor_state_clear (EbSqlCursor        *cursor,
		    CursorState        *state,
		    EbSqlCursorOrigin   position)
{
	gint i;

	for (i = 0; i < cursor->n_sort_fields; i++) {
		g_free (state->values[i]);
		state->values[i] = NULL;
	}

	g_free (state->last_uid);
	state->last_uid = NULL;
	state->position = position;
}

static void
cursor_state_set_from_contact (EBookSqlite *ebsql,
			       EbSqlCursor        *cursor,
			       CursorState        *state,
			       EContact           *contact)
{
	gint i;

	cursor_state_clear (cursor, state, EBSQL_CURSOR_ORIGIN_BEGIN);

	for (i = 0; i < cursor->n_sort_fields; i++) {
		const gchar *string = e_contact_get_const (contact, cursor->sort_fields[i]);
		SummaryField *field;
		gchar *sort_key;

		if (string)
			sort_key = e_collator_generate_key (ebsql->priv->collator,
							    string, NULL);
		else
			sort_key = g_strdup ("");

		field = summary_field_get (ebsql, cursor->sort_fields[i]);

		if (field && (field->index & INDEX_FLAG (SORT_KEY)) != 0) {
			state->values[i] = sort_key;
		} else {
			state->values[i] = ebsql_encode_vcard_sort_key (sort_key);
			g_free (sort_key);
		}
	}

	state->last_uid = e_contact_get (contact, E_CONTACT_UID);
	state->position = EBSQL_CURSOR_ORIGIN_CURRENT;
}

static void
cursor_state_set_from_vcard (EBookSqlite *ebsql,
			     EbSqlCursor        *cursor,
			     CursorState        *state,
			     const gchar        *vcard)
{
	EContact *contact;

	contact = e_contact_new_from_vcard (vcard);
	cursor_state_set_from_contact (ebsql, cursor, state, contact);
	g_object_unref (contact);
}

static gboolean
ebsql_cursor_setup_query (EBookSqlite *ebsql,
			  EbSqlCursor        *cursor,
			  const gchar        *sexp,
			  GError            **error)
{
	PreflightContext context = PREFLIGHT_CONTEXT_INIT;
	GString *string;

	/* Preflighting and error checking */
	if (sexp) {
		query_preflight_for_sql_query (&context, ebsql, sexp);

		if (context.status > PREFLIGHT_NOT_SUMMARIZED) {
			EBSQL_SET_ERROR_LITERAL (error,
						 E_BOOK_SQL_ERROR_INVALID_QUERY,
						 _("Invalid query for EbSqlCursor"));

			preflight_context_clear (&context);
			return FALSE;

		}
	}

	/* Now we caught the errors, let's generate our queries and get out of here ... */
	g_free (cursor->select_vcards);
	g_free (cursor->select_count);
	g_free (cursor->query);
	g_clear_object (&(cursor->sexp));

	/* Generate the leading SELECT portions that we need */
	string = g_string_new ("");
	ebsql_generate_select (ebsql, string, SEARCH_FULL, &context, NULL);
	cursor->select_vcards = g_string_free (string, FALSE);

	string = g_string_new ("");
	ebsql_generate_select (ebsql, string, SEARCH_COUNT, &context, NULL);
	cursor->select_count = g_string_free (string, FALSE);

	if (sexp == NULL || context.list_all) {
		cursor->query = NULL;
		cursor->sexp  = NULL;
	} else {
		/* Generate the constraints for our queries
		 *
		 * It can be that they are optimized into the select segment
		 */
		string = g_string_new (NULL);
		ebsql_generate_constraints (ebsql,
					    string,
					    context.constraints,
					    sexp);
		cursor->query = g_string_free (string, FALSE);
		cursor->sexp  = e_book_backend_sexp_new (sexp);
	}

	preflight_context_clear (&context);

	return TRUE;
}

static gchar *
ebsql_cursor_order_by_fragment (EBookSqlite        *ebsql,
				const EContactField       *sort_fields,
				const EBookCursorSortType *sort_types,
				guint                      n_sort_fields,
				gboolean                   reverse)
{
	GString *string;
	gint i;

	string = g_string_new ("ORDER BY ");

	for (i = 0; i < n_sort_fields; i++) {
		SummaryField *field = summary_field_get (ebsql, sort_fields[i]);

		if (i > 0)
			g_string_append (string, ", ");

		if (field &&
		    (field->index & INDEX_FLAG (SORT_KEY)) != 0) {
			g_string_append (string, "summary.");
			g_string_append (string, field->dbname);
			g_string_append (string, "_" EBSQL_SUFFIX_SORT_KEY " ");
		} else {
			g_string_append (string, EBSQL_VCARD_FRAGMENT (ebsql));
			g_string_append (string, " COLLATE ");
			g_string_append (string, EBSQL_COLLATE_PREFIX);
			g_string_append (string, e_contact_field_name (sort_fields[i]));
			g_string_append_c (string, ' ');
		}

		if (reverse)
			g_string_append (string, (sort_types[i] == E_BOOK_CURSOR_SORT_ASCENDING ? "DESC" : "ASC"));
		else
			g_string_append (string, (sort_types[i] == E_BOOK_CURSOR_SORT_ASCENDING ? "ASC"  : "DESC"));
	}

	/* Also order the UID, since it's our tie breaker */
	if (n_sort_fields > 0)
		g_string_append (string, ", ");

	g_string_append (string, "summary.uid ");
	g_string_append (string, reverse ? "DESC" : "ASC");

	return g_string_free (string, FALSE);
}

static EbSqlCursor *
ebsql_cursor_new (EBookSqlite        *ebsql,
		  const gchar               *sexp,
		  const EContactField       *sort_fields,
		  const EBookCursorSortType *sort_types,
		  guint                      n_sort_fields)
{
	EbSqlCursor *cursor = g_slice_new0 (EbSqlCursor);

	cursor->order = ebsql_cursor_order_by_fragment (ebsql,
							sort_fields,
							sort_types,
							n_sort_fields,
							FALSE);
	cursor->reverse_order = ebsql_cursor_order_by_fragment (ebsql,
								sort_fields,
								sort_types,
								n_sort_fields,
								TRUE);

	/* Sort parameters */
	cursor->n_sort_fields  = n_sort_fields;
	cursor->sort_fields    = g_memdup (sort_fields, sizeof (EContactField) * n_sort_fields);
	cursor->sort_types     = g_memdup (sort_types,  sizeof (EBookCursorSortType) * n_sort_fields);

	/* Cursor state */
	cursor->state.values   = g_new0 (gchar *, n_sort_fields);
	cursor->state.last_uid = NULL;
	cursor->state.position = EBSQL_CURSOR_ORIGIN_BEGIN;

	return cursor;
}

static void
ebsql_cursor_free (EbSqlCursor *cursor)
{
	if (cursor) {
		cursor_state_clear (cursor, &(cursor->state), EBSQL_CURSOR_ORIGIN_BEGIN);
		g_free (cursor->state.values);

		g_clear_object (&(cursor->sexp));
		g_free (cursor->select_vcards);
		g_free (cursor->select_count);
		g_free (cursor->query);
		g_free (cursor->order);
		g_free (cursor->reverse_order);
		g_free (cursor->sort_fields);
		g_free (cursor->sort_types);

		g_slice_free (EbSqlCursor, cursor);
	}
}

#define GREATER_OR_LESS(cursor, idx, reverse)				\
	(reverse ?							\
	 (((EbSqlCursor *)cursor)->sort_types[idx] == E_BOOK_CURSOR_SORT_ASCENDING ? '<' : '>') : \
	 (((EbSqlCursor *)cursor)->sort_types[idx] == E_BOOK_CURSOR_SORT_ASCENDING ? '>' : '<'))

static inline void
ebsql_cursor_format_equality (EBookSqlite *ebsql,
			      GString            *string,
			      EContactField       field_id,
			      const gchar        *value,
			      gchar               equality)
{
	SummaryField *field = summary_field_get (ebsql, field_id);

	if (field &&
	    (field->index & INDEX_FLAG (SORT_KEY)) != 0) {

		g_string_append (string, "summary.");
		g_string_append (string, field->dbname);
		g_string_append (string, "_" EBSQL_SUFFIX_SORT_KEY " ");

		ebsql_string_append_printf (string, "%c %Q", equality, value);

	} else {
		ebsql_string_append_printf (string, "(%s %c %Q ",
					    EBSQL_VCARD_FRAGMENT (ebsql),
					    equality, value);

		g_string_append (string, "COLLATE " EBSQL_COLLATE_PREFIX);
		g_string_append (string, e_contact_field_name (field_id));
		g_string_append_c (string, ')');
	}
}

static gchar *
ebsql_cursor_constraints (EBookSqlite *ebsql,
			  EbSqlCursor        *cursor,
			  CursorState        *state,
			  gboolean            reverse,
			  gboolean            include_current_uid)
{
	GString *string;
	gint i, j;

	/* Example for:
	 *    ORDER BY family_name ASC, given_name DESC
	 *
	 * Where current cursor values are:
	 *    family_name = Jackson
	 *    given_name  = Micheal
	 *
	 * With reverse = FALSE
	 *
	 *    (summary.family_name > 'Jackson') OR
	 *    (summary.family_name = 'Jackson' AND summary.given_name < 'Micheal') OR
	 *    (summary.family_name = 'Jackson' AND summary.given_name = 'Micheal' AND summary.uid > 'last-uid')
	 *
	 * With reverse = TRUE (needed for moving the cursor backwards through results)
	 *
	 *    (summary.family_name < 'Jackson') OR
	 *    (summary.family_name = 'Jackson' AND summary.given_name > 'Micheal') OR
	 *    (summary.family_name = 'Jackson' AND summary.given_name = 'Micheal' AND summary.uid < 'last-uid')
	 *
	 */
	string = g_string_new (NULL);

	for (i = 0; i <= cursor->n_sort_fields; i++) {

		/* Break once we hit a NULL value */
		if ((i  < cursor->n_sort_fields && state->values[i] == NULL) ||
		    (i == cursor->n_sort_fields && state->last_uid  == NULL))
			break;

		/* Between each qualifier, add an 'OR' */
		if (i > 0)
			g_string_append (string, " OR ");

		/* Begin qualifier */
		g_string_append_c (string, '(');

		/* Create the '=' statements leading up to the current tie breaker */
		for (j = 0; j < i; j++) {
			ebsql_cursor_format_equality (ebsql, string,
						      cursor->sort_fields[j],
						      state->values[j], '=');
			g_string_append (string, " AND ");
		}

		if (i == cursor->n_sort_fields) {

			/* The 'include_current_uid' clause is used for calculating
			 * the current position of the cursor, inclusive of the
			 * current position.
			 */
			if (include_current_uid)
				g_string_append_c (string, '(');

			/* Append the UID tie breaker */
			ebsql_string_append_printf (string,
						    "summary.uid %c %Q",
						    reverse ? '<' : '>',
						    state->last_uid);

			if (include_current_uid)
				ebsql_string_append_printf (string,
							    " OR summary.uid = %Q)",
							    state->last_uid);

		} else {

			/* SPECIAL CASE: If we have a parially set cursor state, then we must
			 * report next results that are inclusive of the final qualifier.
			 *
			 * This allows one to set the cursor with the family name set to 'J'
			 * and include the results for contact's Mr & Miss 'J'.
			 */
			gboolean include_exact_match =
				(reverse == FALSE &&
				 ((i + 1 < cursor->n_sort_fields && state->values[i + 1] == NULL) ||
				  (i + 1 == cursor->n_sort_fields && state->last_uid == NULL)));

			if (include_exact_match)
				g_string_append_c (string, '(');

			/* Append the final qualifier for this field */
			ebsql_cursor_format_equality (ebsql, string,
						      cursor->sort_fields[i],
						      state->values[i],
						      GREATER_OR_LESS (cursor, i, reverse));

			if (include_exact_match) {
				g_string_append (string, " OR ");
				ebsql_cursor_format_equality (ebsql, string,
							      cursor->sort_fields[i],
							      state->values[i], '=');
				g_string_append_c (string, ')');
			}
		}

		/* End qualifier */
		g_string_append_c (string, ')');
	}

	return g_string_free (string, FALSE);
}

static gboolean
cursor_count_total_locked (EBookSqlite *ebsql,
			   EbSqlCursor        *cursor,
			   gint               *total,
			   GError            **error)
{
	GString *query;
	gboolean success;

	query = g_string_new (cursor->select_count);

	/* Add the filter constraints (if any) */
	if (cursor->query) {
		g_string_append (query, " WHERE ");

		g_string_append_c (query, '(');
		g_string_append (query, cursor->query);
		g_string_append_c (query, ')');
	}

	/* Execute the query */
	success = ebsql_exec (ebsql, query->str, get_count_cb, total, error);

	g_string_free (query, TRUE);

	return success;
}

static gboolean
cursor_count_position_locked (EBookSqlite *ebsql,
			      EbSqlCursor        *cursor,
			      gint               *position,
			      GError            **error)
{
	GString *query;
	gboolean success;

	query = g_string_new (cursor->select_count);

	/* Add the filter constraints (if any) */
	if (cursor->query) {
		g_string_append (query, " WHERE ");

		g_string_append_c (query, '(');
		g_string_append (query, cursor->query);
		g_string_append_c (query, ')');
	}

	/* Add the cursor constraints (if any) */
	if (cursor->state.values[0] != NULL) {
		gchar *constraints = NULL;

		if (!cursor->query)
			g_string_append (query, " WHERE ");
		else
			g_string_append (query, " AND ");

		/* Here we do a reverse query, we're looking for all the
		 * results leading up to the current cursor value, including
		 * the cursor value
		 */
		constraints = ebsql_cursor_constraints (ebsql, cursor,
							&(cursor->state),
							TRUE, TRUE);

		g_string_append_c (query, '(');
		g_string_append (query, constraints);
		g_string_append_c (query, ')');

		g_free (constraints);
	}

	/* Execute the query */
	success = ebsql_exec (ebsql, query->str, get_count_cb, position, error);

	g_string_free (query, TRUE);

	return success;
}

/**********************************************************
 *                     GObjectClass                       *
 **********************************************************/
static void
e_book_sqlite_dispose (GObject *object)
{
	EBookSqlite *ebsql = E_BOOK_SQLITE (object);

	ebsql_unregister_from_hash (ebsql);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_sqlite_parent_class)->dispose (object);
}

static void
e_book_sqlite_finalize (GObject *object)
{
	EBookSqlite *ebsql = E_BOOK_SQLITE (object);
	EBookSqlitePrivate *priv = ebsql->priv;

	sqlite3_close (priv->db);

	summary_fields_array_free (priv->summary_fields, 
				   priv->n_summary_fields);

	g_free (priv->folderid);
	g_free (priv->path);
	g_free (priv->locale);
	g_free (priv->region_code);

	if (priv->collator)
		e_collator_unref (priv->collator);

	g_mutex_clear (&priv->lock);
	g_mutex_clear (&priv->updates_lock);

	sqlite3_finalize (priv->insert_stmt);
	sqlite3_finalize (priv->replace_stmt);

	if (priv->multi_deletes)
		g_hash_table_destroy (priv->multi_deletes);

	if (priv->multi_inserts)
		g_hash_table_destroy (priv->multi_inserts);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_sqlite_parent_class)->finalize (object);
}

static void
e_book_sqlite_class_init (EBookSqliteClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EBookSqlitePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = e_book_sqlite_dispose;
	object_class->finalize = e_book_sqlite_finalize;

	/* Parse the EBSQL_DEBUG environment variable */
	ebsql_init_debug ();
}

static void
e_book_sqlite_init (EBookSqlite *ebsql)
{
	ebsql->priv = 
		G_TYPE_INSTANCE_GET_PRIVATE (ebsql,
					     E_TYPE_BOOK_SQLITE,
					     EBookSqlitePrivate);
	g_mutex_init (&ebsql->priv->lock);
	g_mutex_init (&ebsql->priv->updates_lock);
}

/**********************************************************
 *                          API                           *
 **********************************************************/
static EBookSqlite *
ebsql_new_with_folderid (const gchar *path,
			 const gchar *folderid,
			 EbSqlVCardCallback callback,
			 gpointer user_data,
			 GError **error)
{
	EBookSqlite *ebsql;
	GArray *summary_fields;
	gint i;

	if (folderid == NULL)
		folderid = DEFAULT_FOLDER_ID;

	/* Create the default summary structs */
	summary_fields = g_array_new (FALSE, FALSE, sizeof (SummaryField));
	for (i = 0; i < G_N_ELEMENTS (default_summary_fields); i++)
		summary_field_append (summary_fields, folderid, default_summary_fields[i], NULL);

	/* Add the default index flags */
	summary_fields_add_indexes (
		summary_fields,
		default_indexed_fields,
		default_index_types,
		G_N_ELEMENTS (default_indexed_fields));

	ebsql = ebsql_new_internal (
		path, folderid,
		callback, user_data,
		(SummaryField *) summary_fields->data,
		summary_fields->len,
		error);

	g_array_free (summary_fields, FALSE);

	return ebsql;
}

/**
 * e_book_sqlite_new:
 * @path: location where the db would be created
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * If the path for multiple addressbooks are same, the contacts from all addressbooks
 * would be stored in same db in different tables.
 *
 * Returns: (transfer full): A reference to a #EBookSqlite
 *
 * Since: 3.12
 **/
EBookSqlite *
e_book_sqlite_new (const gchar *path,
		   GError **error)
{
	return ebsql_new_with_folderid (path, NULL, NULL, NULL, error);
}

/**
 * e_book_sqlite_new_shallow:
 * @path: location where the db would be created
 * @callback: (scope async) (closure user_data): A function to resolve vcards
 * @user_data: callback user data
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Creates or opens a shallow addressbook. Shallow addressbooks do not store
 * the vcards for contacts passed to e_book_sqlitedb_add_contact() but instead
 * rely on the passed @callback to resolve vcards from an external source.
 *
 * It is recommended to store all contact vcards in the #EBookSqlite addressbook
 * if at all possible, however in some cases the vcards must be stored in some
 * other storage.
 *
 * Returns: (transfer full): A reference to a #EBookSqlite
 *
 * Since: 3.12
 */
EBookSqlite *
e_book_sqlite_new_shallow (const gchar *path,
			   EbSqlVCardCallback callback,
			   gpointer user_data,
			   GError **error)
{
	return ebsql_new_with_folderid (path, NULL, callback, user_data, error);
}

/**
 * e_book_sqlite_new_full:
 * @path: location where the db would be created
 * @folderid: folder id of the address-book
 * @callback: (allow-none) (scope async) (closure user_data): A function to resolve vcards
 * @user_data: callback user data
 * @setup: an #ESourceBackendSummarySetup describing how the summary should be setup
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Like e_book_sqlite_new(), but allows configuration of which contact fields
 * will be stored for quick reference in the summary. The configuration indicated by
 * @setup will only be taken into account when initially creating the underlying table,
 * further configurations will be ignored.
 *
 * If @callback is specified, then a shallow addressbook will be opened, see
 * e_book_sqlite_shallow_new() for details on shallow addressbooks.
 *
 * The fields %E_CONTACT_UID and %E_CONTACT_REV are not optional,
 * they will be stored in the summary regardless of this function's parameters
 *
 * <note><para>Only #EContactFields with the type #G_TYPE_STRING, #G_TYPE_BOOLEAN or
 * #E_TYPE_CONTACT_ATTR_LIST are currently supported.</para></note>
 *
 * Returns: (transfer full): The newly created #EBookSqlite
 *
 * Since: 3.12
 **/
EBookSqlite *
e_book_sqlite_new_full (const gchar *path,
			const gchar *folderid,
			EbSqlVCardCallback callback,
			gpointer user_data,
			ESourceBackendSummarySetup *setup,
			GError **error)
{
	EBookSqlite *ebsql = NULL;
	EContactField *fields;
	EContactField *indexed_fields;
	EBookIndexType *index_types = NULL;
	gboolean had_error = FALSE;
	GArray *summary_fields;
	gint n_fields = 0, n_indexed_fields = 0, i;

	if (folderid == NULL)
		folderid = DEFAULT_FOLDER_ID;

	fields         = e_source_backend_summary_setup_get_summary_fields (setup, &n_fields);
	indexed_fields = e_source_backend_summary_setup_get_indexed_fields (setup, &index_types, &n_indexed_fields);

	/* No specified summary fields indicates the default summary configuration should be used */
	if (n_fields <= 0) {
		ebsql = ebsql_new_with_folderid (path, folderid, callback, user_data, error);
		g_free (fields);
		g_free (index_types);
		g_free (indexed_fields);

		return ebsql;
	}

	summary_fields = g_array_new (FALSE, FALSE, sizeof (SummaryField));

	/* Ensure the non-optional fields first */
	summary_field_append (summary_fields, folderid, E_CONTACT_UID, error);
	summary_field_append (summary_fields, folderid, E_CONTACT_REV, error);

	for (i = 0; i < n_fields; i++) {
		if (!summary_field_append (summary_fields, folderid, fields[i], error)) {
			had_error = TRUE;
			break;
		}
	}

	if (had_error) {
		gint n_sfields;
		SummaryField *sfields;

		/* Properly free the array */
		n_sfields = summary_fields->len;
		sfields   = (SummaryField *)g_array_free (summary_fields, FALSE);
		summary_fields_array_free (sfields, n_sfields);

		g_free (fields);
		g_free (index_types);
		g_free (indexed_fields);
		return NULL;
	}

	/* Add the 'indexed' flag to the SummaryField structs */
	summary_fields_add_indexes (
		summary_fields, indexed_fields, index_types, n_indexed_fields);

	ebsql = ebsql_new_internal (
		path, folderid,
		callback, user_data,
		(SummaryField *) summary_fields->data,
		summary_fields->len,
		error);

	g_free (fields);
	g_free (index_types);
	g_free (indexed_fields);
	g_array_free (summary_fields, FALSE);

	return ebsql;
}

/**
 * e_book_sqlite_lock_updates:
 * @ebsql: An #EBookSqlite
 * @writer_lock: Whether this transaction will include write operations
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Obtains an exclusive lock on @ebsql and starts a transaction.
 *
 * This should be called if you need to access @ebsql multiple times while
 * ensuring an atomic transaction. End this transaction with
 * e_book_sqlite_unlock_updates().
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_lock_updates (EBookSqlite *ebsql,
			    gboolean writer_lock,
			    GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->updates_lock);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);
	success = ebsql_start_transaction (ebsql, writer_lock, error);
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return success;
}

/**
 * e_book_sqlite_lock_updates:
 * @ebsql: An #EBookSqlite
 * @do_commit: Whether the transaction should be committed or rolled back
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Releases an exclusive on @ebsql and finishes a transaction previously
 * started with e_book_sqlite_lock_updates().
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_unlock_updates (EBookSqlite *ebsql,
			      gboolean do_commit,
			      GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);
	success = do_commit ?
		ebsql_commit_transaction (ebsql, error) :
		ebsql_rollback_transaction (ebsql, error);
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	EBSQL_UNLOCK_MUTEX (&ebsql->priv->updates_lock);

	return success;
}

/**
 * e_book_sqlite_ref_collator:
 * @ebsql: An #EBookSqlite
 *
 * References the currently active #ECollator for @ebsql,
 * use e_collator_unref() when finished using the returned collator.
 *
 * Note that the active collator will change with the active locale setting.
 *
 * Returns: (transfer full): A reference to the active collator.
 *
 * Since: 3.12
 */
ECollator *
e_book_sqlite_ref_collator (EBookSqlite *ebsql)
{
	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), NULL);

	return e_collator_ref (ebsql->priv->collator);
}

/**
 * e_book_sqlitedb_add_contact:
 * @ebsql: An #EBookSqlite
 * @contact: EContact to be added
 * @extra: Extra data to store in association with this contact
 * @replace: Whether this contact should replace another contact with the same UID.
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * This is a convenience wrapper for e_book_sqlite_add_contacts(),
 * which is the preferred means to add or modify multiple contacts when possible.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_add_contact (EBookSqlite *ebsql,
			   EContact *contact,
			   const gchar *extra,
			   gboolean replace,
			   GError **error)
{
	GSList l;
	GSList el;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	l.data = contact;
	l.next = NULL;

	el.data = (gpointer)extra;
	el.next = NULL;

	return e_book_sqlite_add_contacts (ebsql, &l, &el, replace, error);
}

/**
 * e_book_sqlite_new_contacts:
 * @ebsql: An #EBookSqlite
 * @contacts: (element-type EContact): A list of contacts to add to @ebsql
 * @extra: (allow-none) (element-type utf8): A list of extra data to store in association with this contact
 * @replace: Whether this contact should replace another contact with the same UID.
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Adds or replaces contacts in @ebsql. If @replace_existing is specified then existing
 * contacts with the same UID will be replaced, otherwise adding an existing contact
 * will return an error.
 *
 * If @extra is specified, it must have an equal length as the @contacts list. Each element
 * from the @extra list will be stored in association with it's corresponding contact
 * in the @contacts list.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_add_contacts (EBookSqlite *ebsql,
			    GSList *contacts,
			    GSList *extra,
			    gboolean replace,
			    GError **error)
{
	GSList *l, *ll;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (contacts != NULL, FALSE);
	g_return_val_if_fail (extra == NULL ||
			      g_slist_length (extra) == g_slist_length (contacts), FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);

	if (!ebsql_start_transaction (ebsql, TRUE, error)) {
		EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
		return FALSE;
	}

	for (l = contacts, ll = extra;
	     success && l != NULL;
	     l = l->next, ll = ll ? ll->next : NULL) {
		EContact *contact = (EContact *) l->data;
		const gchar *extra_data = NULL;

		if (ll)
			extra_data = (const gchar *)ll->data;

		success = ebsql_insert_contact (ebsql, contact, extra_data, replace,
						NULL, error);
	}

	if (success)
		success = ebsql_commit_transaction (ebsql, error);
	else
		/* The GError is already set. */
		ebsql_rollback_transaction (ebsql, NULL);

	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return success;
}

/**
 * e_book_sqlite_remove_contact:
 * @ebsql: An #EBookSqlite
 * @uid: the uid of the contact to remove
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Removes the contact indicated by @uid from @ebsql.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_remove_contact (EBookSqlite *ebsql,
			      const gchar *uid,
			      GError **error)
{
	GSList l;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	l.data = (gchar *) uid; /* Won't modify it, I promise :) */
	l.next = NULL;

	return e_book_sqlite_remove_contacts (
		ebsql, &l, error);
}

static gchar *
generate_delete_stmt (const gchar *table,
                      GSList *uids)
{
	GString *str = g_string_new (NULL);
	GSList  *l;

	ebsql_string_append_printf (str, "DELETE FROM %Q WHERE uid IN (", table);

	for (l = uids; l; l = l->next) {
		const gchar *uid = (const gchar *) l->data;

		/* First uid with no comma */
		if (l != uids)
			g_string_append_printf (str, ", ");

		ebsql_string_append_printf (str, "%Q", uid);
	}

	g_string_append_c (str, ')');

	return g_string_free (str, FALSE);
}

/**
 * e_book_sqlite_remove_contacts:
 * @ebsql: An #EBookSqlite
 * @uids: a #GSList of uids indicating which contacts to remove
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Removes the contacts indicated by @uids from @ebsql.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_remove_contacts (EBookSqlite *ebsql,
			       GSList *uids,
			       GError **error)
{
	gboolean success = TRUE;
	gint i;
	gchar *stmt;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);

	if (!ebsql_start_transaction (ebsql, TRUE, error)) {
		EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
		return FALSE;
	}

	/* Delete data from the auxiliary tables first */
	for (i = 0; success && i < ebsql->priv->n_summary_fields; i++) {
		SummaryField *field = &(ebsql->priv->summary_fields[i]);

		if (field->type != E_TYPE_CONTACT_ATTR_LIST)
			continue;

		stmt = generate_delete_stmt (field->aux_table, uids);
		success = ebsql_exec (ebsql, stmt, NULL, NULL, error);
		g_free (stmt);
	}

	/* Now delete the entry from the main contacts */
	if (success) {
		stmt = generate_delete_stmt (ebsql->priv->folderid, uids);
		success = ebsql_exec (ebsql, stmt, NULL, NULL, error);
		g_free (stmt);
	}

	if (success)
		success = ebsql_commit_transaction (ebsql, error);
	else
		/* The GError is already set. */
		ebsql_rollback_transaction (ebsql, NULL);

	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return success;
}

/**
 * e_book_sqlite_has_contact:
 * @ebsql: An #EBookSqlite
 * @uid: The uid of the contact to check for
 * @exists: (out): Return location to store whether the contact exists.
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Checks if a contact bearing the UID indicated by @uid is stored in @ebsql.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_has_contact (EBookSqlite *ebsql,
			   const gchar *uid,
			   gboolean *exists,
			   GError **error)
{
	gboolean local_exists = FALSE;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (exists != NULL, FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);
	success = ebsql_exec_printf (ebsql,
				     "SELECT uid FROM %Q WHERE uid = %Q",
				     get_exists_cb, &local_exists, error,
				     ebsql->priv->folderid, uid);
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	*exists = local_exists;

	return success;
}

/**
 * e_book_sqlite_get_contact:
 * @ebsql: An #EBookSqlite
 * @uid: The uid of the contact to fetch
 * @meta_contact: Whether an entire contact is desired, or only the metadata
 * @contact: (out): Return location to store the fetched contact
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Fetch the #EContact specified by @uid in @ebsql.
 *
 * If @meta_contact is specified, then a shallow #EContact will be created
 * holing only the %E_CONTACT_UID and %E_CONTACT_REV fields.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_get_contact (EBookSqlite *ebsql,
			   const gchar *uid,
			   gboolean meta_contact,	
			   EContact **contact,
			   GError **error)
{
	gboolean success = FALSE;
	gchar *vcard = NULL;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (contact != NULL && *contact == NULL, FALSE);

	success = e_book_sqlite_get_vcard (ebsql,
					   uid,
					   meta_contact,	
					   &vcard,
					   error);

	if (success && vcard) {
		*contact = e_contact_new_from_vcard_with_uid (vcard, uid);
		g_free (vcard);
	}

	return success;
}

/**
 * e_book_sqlite_get_vcard:
 * @ebsql: An #EBookSqlite
 * @uid: The uid of the contact to fetch
 * @meta_contact: Whether an entire contact is desired, or only the metadata
 * @vcard: (out): Return location to store the fetched vcard string
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Fetch a vcard string for @uid in @ebsql.
 *
 * If @meta_contact is specified, then a shallow vcard representation will be
 * created holing only the %E_CONTACT_UID and %E_CONTACT_REV fields.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_get_vcard (EBookSqlite *ebsql,
			 const gchar *uid,
			 gboolean meta_contact,	
			 gchar **vcard,
			 GError **error)
{
	gboolean success = FALSE;
	gchar *vcard_str = NULL;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (vcard != NULL && *vcard == NULL, FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);

	/* Try constructing contacts from only UID/REV first if that's requested */
	if (meta_contact) {
		GSList *vcards = NULL;

		success = ebsql_exec_printf (
			ebsql, "SELECT summary.uid, summary.Rev FROM %Q AS summary WHERE uid = %Q",
			collect_lean_results_cb, &vcards, error,
			ebsql->priv->folderid, uid);

		if (vcards) {
			EbSqlSearchData *search_data = (EbSqlSearchData *) vcards->data;

			vcard_str = search_data->vcard;
			search_data->vcard = NULL;

			g_slist_free_full (vcards, (GDestroyNotify)e_book_sqlite_search_data_free);
			vcards = NULL;
		}

	} else {
		success = ebsql_exec_printf (
			ebsql, "SELECT %s FROM %Q AS summary WHERE summary.uid = %Q",
			get_string_cb, &vcard_str, error, 
			EBSQL_VCARD_FRAGMENT (ebsql), ebsql->priv->folderid, uid);
	}

	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	*vcard = vcard_str;

	if (success && !vcard_str) {
		/* Odd, but true */
		EBSQL_SET_ERROR (error,
				 E_BOOK_SQL_ERROR_CONTACT_NOT_FOUND,
				 _("Contact '%s' not found"), uid);
		success = FALSE;
	}

	return success;
}

/**
 * e_book_sqlitedb_search:
 * @ebsql: An #EBookSqlite
 * @sexp: (allow-none): search expression; use %NULL or an empty string to get all stored contacts.
 * @meta_contact: Whether entire contacts are desired, or only the metadata
 * @ret_list: (out) (transfer full): Return location to store a #GSList of #EbSqlSearchData structures
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Searching with summary fields is always supported. Search expressions
 * containing any other field is supported only if backend chooses to store
 * the vcard inside the db.
 *
 * If not configured otherwise, the default summary fields include:
 *   uid, rev, file_as, nickname, full_name, given_name, family_name,
 *   email, is_list, list_show_addresses, wants_html.
 *
 * Summary fields can be configured at addressbook creation time using
 * the #ESourceBackendSummarySetup source extension.
 *
 * The returned @ret_list list should be freed with g_slist_free()
 * and all elements freed with e_book_sqlite_search_data_free().
 *
 * If @meta_contact is specified, then shallow vcard representations will be
 * created holing only the %E_CONTACT_UID and %E_CONTACT_REV fields.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_search (EBookSqlite *ebsql,
		      const gchar *sexp,
		      gboolean meta_contacts,
		      GSList **ret_list,
		      GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (ret_list != NULL && *ret_list == NULL, FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);
	success = ebsql_search_query (ebsql, sexp,
				      meta_contacts ? 
				      SEARCH_UID_AND_REV : SEARCH_FULL,
				      ret_list,
				      error);	
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return success;
}

/**
 * e_book_sqlite_search_uids:
 * @ebsql: An #EBookSqlite
 * @sexp: (allow-none): search expression; use %NULL or an empty string to get all stored contacts.
 * @ret_list: (out) (transfer full): Return location to store a #GSList of contact uids
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Similar to e_book_sqlitedb_search(), but fetches only a list of contact UIDs.
 *
 * The returned @ret_list list should be freed with g_slist_free() and all
 * elements freed with g_free().
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_search_uids (EBookSqlite *ebsql,
			   const gchar *sexp,
			   GSList **ret_list,
			   GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (ret_list != NULL && *ret_list == NULL, FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);
	success = ebsql_search_query (ebsql, sexp, SEARCH_UID, ret_list, error);
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return success;
}

/**
 * e_book_sqlite_get_uids_and_rev:
 * @ebsql: An #EBookSqlite
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Gets hash table of all uids (key) and rev (value) pairs stored
 * for each contact in the cache. The hash table should be freed
 * with g_hash_table_destroy(), if not needed anymore. Each key
 * and value is a newly allocated string.
 *
 * Returns: (transfer full): The #GHashTable containing all uids and revisions
 *
 * Since: 3.12
 **/
GHashTable *
e_book_sqlite_get_uids_and_rev (EBookSqlite *ebsql,
				GError **error)
{
	GHashTable *uids_and_rev;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), NULL);

	uids_and_rev = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);
	ebsql_exec_printf (ebsql, "SELECT uid, rev FROM %Q",
			   collect_uids_and_rev_cb, uids_and_rev, error,
			   ebsql->priv->folderid);
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return uids_and_rev;
}

/**
 * e_book_sqlite_get_key_value:
 * @ebsql: An #EBookSqlite
 * @key: The key to fetch a value for
 * @value: (out) (transfer full): A return location to store the value for @key
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Fetches the value for @key and stores it in @value
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_get_key_value (EBookSqlite *ebsql,
			     const gchar *key,
			     gchar **value,
			     GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL && *value == NULL, FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);
	success = ebsql_exec_printf (ebsql,
				     "SELECT value FROM keys WHERE folder_id = %Q AND key = %Q",
				     get_string_cb, value, error, ebsql->priv->folderid, key);
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return success;
}

/**
 * e_book_sqlite_set_key_value:
 * @ebsql: An #EBookSqlite
 * @key: The key to fetch a value for
 * @value: The new value for @key
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * Sets the value for @key to be @value
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_set_key_value (EBookSqlite *ebsql,
			     const gchar *key,
			     const gchar *value,
			     GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);

	if (!ebsql_start_transaction (ebsql, TRUE, error)) {
		EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
		return FALSE;
	}

	success = ebsql_exec_printf (
		ebsql, "INSERT or REPLACE INTO keys (key, value, folder_id) values (%Q, %Q, %Q)",
		NULL, NULL, error, key, value, ebsql->priv->folderid);

	if (success)
		success = ebsql_commit_transaction (ebsql, error);
	else
		/* The GError is already set. */
		ebsql_rollback_transaction (ebsql, NULL);

	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return success;
}

/**
 * e_book_sqlite_get_key_value_int:
 * @ebsql: An #EBookSqlite
 * @key: The key to fetch a value for
 * @value: (out): A return location to store the value for @key
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * A convenience function to fetch the value of @key as an integer.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_get_key_value_int (EBookSqlite *ebsql,
				 const gchar *key,
				 gint *value,
				 GError **error)
{
	gboolean success;
	gchar *str_value = NULL;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	success = e_book_sqlite_get_key_value (ebsql, key, &str_value, error);

	if (success) {

		if (str_value)
			*value = g_ascii_strtoll (str_value, NULL, 10);
		else
			*value = 0;

		g_free (str_value);
	}

	return success;
}

/**
 * e_book_sqlite_set_key_value_int:
 * @ebsql: An #EBookSqlite
 * @key: The key to fetch a value for
 * @value: The new value for @key
 * @error: (allow-none): A location to store any error that may have occurred.
 *
 * A convenience function to set the value of @key as an integer.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 **/
gboolean
e_book_sqlite_set_key_value_int (EBookSqlite *ebsql,
				 const gchar *key,
				 gint value,
				 GError **error)
{
	gboolean success;
	gchar *str_value = NULL;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	str_value = g_strdup_printf ("%d", value);
	success = e_book_sqlite_set_key_value (ebsql,
						       key,
						       str_value,
						       error);
	g_free (str_value);

	return success;
}

/**
 * e_book_sqlite_search_data_free:
 * @data: An #EbSqlSearchData
 *
 * Frees an #EbSqlSearchData
 *
 * Since: 3.12
 **/
void
e_book_sqlite_search_data_free (EbSqlSearchData *data)
{
	if (data) {
		g_free (data->uid);
		g_free (data->vcard);
		g_free (data->extra);
		g_slice_free (EbSqlSearchData, data);
	}
}

/**
 * e_book_sqlite_set_locale:
 * @ebsql: An #EBookSqlite
 * @lc_collate: The new locale for the addressbook
 * @callback: (scope call): A callback to call for changed vcards
 * @user_data: User data to pass to @callback
 * @error: A location to store any error that may have occurred
 *
 * Relocalizes any locale specific data in the specified
 * new @lc_collate locale.
 *
 * The @lc_collate locale setting is stored and remembered on
 * subsequent accesses of the addressbook, changing the locale
 * will store the new locale and will modify sort keys and any
 * locale specific data in the addressbook.
 *
 * As a side effect, it's possible that changing the locale
 * will cause stored vcards to change. The @callback, of provided,
 * will be called for each vcard which changes as a result
 * of the locale setting.
 *
 * Returns: Whether the new locale was successfully set.
 *
 * Since: 3.12
 */
gboolean
e_book_sqlite_set_locale (EBookSqlite        *ebsql,
			  const gchar        *lc_collate,
			  EbSqlChangeCallback callback,
			  gpointer            user_data,
			  GError            **error)
{
	gboolean success;
	gchar *stored_lc_collate = NULL;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);

	if (!ebsql_start_transaction (ebsql, TRUE, error)) {
		EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
		return FALSE;
	}

	if (!ebsql_set_locale_internal (ebsql, lc_collate, error)) {
		EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
		return FALSE;
	}

	success = ebsql_exec_printf (
		ebsql, "SELECT lc_collate FROM folders WHERE folder_id = %Q",
		get_string_cb, &stored_lc_collate, error, ebsql->priv->folderid);

	if (success && g_strcmp0 (stored_lc_collate, lc_collate) != 0)
		success = ebsql_upgrade (ebsql, callback, user_data, error);

	/* If for some reason we failed, then reset the collator to use the old locale */
	if (!success && stored_lc_collate && stored_lc_collate[0])
		ebsql_set_locale_internal (ebsql, stored_lc_collate, NULL);

	if (success)
		success = ebsql_commit_transaction (ebsql, error);
	else
		/* The GError is already set. */
		ebsql_rollback_transaction (ebsql, NULL);

	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	g_free (stored_lc_collate);

	return success;
}

/**
 * e_book_sqlite_get_locale:
 * @ebsql: An #EBookSqlite
 * @locale_out: (out) (transfer full): The location to return the current locale
 * @error: A location to store any error that may have occurred
 *
 * Fetches the current locale setting for the address-book.
 *
 * Upon success, @lc_collate_out will hold the returned locale setting,
 * otherwise %FALSE will be returned and @error will be updated accordingly.
 *
 * Returns: Whether the locale was successfully fetched.
 *
 * Since: 3.12
 */
gboolean
e_book_sqlite_get_locale (EBookSqlite  *ebsql,
			  gchar       **locale_out,
			  GError      **error)
{
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (locale_out != NULL && *locale_out == NULL, FALSE);

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);

	success = ebsql_exec_printf (
		ebsql, "SELECT lc_collate FROM folders WHERE folder_id = %Q",
		get_string_cb, locale_out, error, ebsql->priv->folderid);

	if (!locale_out || !locale_out[0]) {

		/* This is bad, it merits a warning */
		g_warning ("EBookSqlite has no active locale");
		EBSQL_SET_ERROR_LITERAL (
			error, E_BOOK_SQL_ERROR_OTHER,
			_("EBookSqlite has no active locale"));
		success = FALSE;
	}

	if (success && !ebsql_set_locale_internal (ebsql, *locale_out, &local_error)) {
		g_warning ("Error loading new locale: %s", local_error->message);
		g_clear_error (&local_error);
	}

	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return success;
}

/**
 * e_book_sqlite_cursor_new:
 * @ebsql: An #EBookSqlite
 * @folderid: folder id of the address-book
 * @sexp: search expression; use NULL or an empty string to get all stored contacts.
 * @sort_fields: (array length=n_sort_fields): An array of #EContactFields as sort keys in order of priority
 * @sort_types: (array length=n_sort_fields): An array of #EBookCursorSortTypes, one for each field in @sort_fields
 * @n_sort_fields: The number of fields to sort results by.
 * @error: A return location to store any error that might be reported.
 *
 * Creates a new #EbSqlCursor.
 *
 * The cursor should be freed with e_book_sqlite_cursor_free().
 *
 * Returns: (transfer full): A newly created #EbSqlCursor
 *
 * Since: 3.12
 */
EbSqlCursor *
e_book_sqlite_cursor_new (EBookSqlite               *ebsql,
			  const gchar               *sexp,
			  const EContactField       *sort_fields,
			  const EBookCursorSortType *sort_types,
			  guint                      n_sort_fields,
			  GError                   **error)
{
	EbSqlCursor *cursor;
	gint i;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), NULL);

	/* We don't like '\0' sexps, prefer NULL */
	if (sexp && !sexp[0])
		sexp = NULL;

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);

	/* Need one sort key ... */
	if (n_sort_fields == 0) {
		EBSQL_SET_ERROR_LITERAL (
			error, E_BOOK_SQL_ERROR_INVALID_QUERY,
			_("At least one sort field must be specified to use an EbSqlCursor"));
		EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
		return NULL;
	}

	/* We only support string fields to sort the cursor */
	for (i = 0; i < n_sort_fields; i++) {

		if (e_contact_field_type (sort_fields[i]) != G_TYPE_STRING) {
			EBSQL_SET_ERROR_LITERAL (
				error, E_BOOK_SQL_ERROR_INVALID_QUERY,
				_("Cannot sort by a field that is not a string type"));
			EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
			return NULL;
		}
	}

	/* Now we need to create the cursor instance before setting up the query
	 * (not really true, but more convenient that way).
	 */
	cursor = ebsql_cursor_new (ebsql, sexp, sort_fields, sort_types, n_sort_fields);

	/* Setup the cursor's query expression which might fail */
	if (!ebsql_cursor_setup_query (ebsql, cursor, sexp, error)) {
		ebsql_cursor_free (cursor);
		cursor = NULL;
	}

	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return cursor;
}

/**
 * e_book_sqlite_cursor_free:
 * @ebsql: An #EBookSqlite
 * @cursor: The #EbSqlCursor to free
 *
 * Frees @cursor.
 *
 * Since: 3.12
 */
void
e_book_sqlite_cursor_free (EBookSqlite *ebsql,
			   EbSqlCursor *cursor)
{
	g_return_if_fail (E_IS_BOOK_SQLITE (ebsql));

	ebsql_cursor_free (cursor);
}

typedef struct {
	GSList *results;
	gchar *alloc_vcard;
	const gchar *last_vcard;

	gboolean collect_results;
	gint n_results;
} CursorCollectData;

static gint
collect_results_for_cursor_cb (gpointer ref,
			       gint ncol,
			       gchar **cols,
			       gchar **names)
{
	CursorCollectData *data = ref;

	if (data->collect_results) {
		EbSqlSearchData *search_data;

		search_data = search_data_from_results (ncol, cols, names);

		data->results = g_slist_prepend (data->results, search_data);

		data->last_vcard = search_data->vcard;
	} else {
		g_free (data->alloc_vcard);
		data->alloc_vcard = g_strdup (cols[1]);

		data->last_vcard = data->alloc_vcard;
	}

	data->n_results++;

	return 0;
}

/**
 * e_book_sqlite_cursor_step:
 * @ebsql: An #EBookSqlite
 * @cursor: The #EbSqlCursor to use
 * @flags: The #EbSqlCursorStepFlags for this step
 * @origin: The #EbSqlCursorOrigin from whence to step
 * @count: A positive or negative amount of contacts to try and fetch
 * @results: (out) (allow-none) (element-type EbSqlSearchData) (transfer full):
 *   A return location to store the results, or %NULL if %EBSQL_CURSOR_STEP_FETCH is not specified in %flags.
 * @error: A return location to store any error that might be reported.
 *
 * Steps @cursor through it's sorted query by a maximum of @count contacts
 * starting from @origin.
 *
 * If @count is negative, then the cursor will move through the list in reverse.
 *
 * If @cursor reaches the beginning or end of the query results, then the
 * returned list might not contain the amount of desired contacts, or might
 * return no results if the cursor currently points to the last contact. 
 * Reaching the end of the list is not considered an error condition. Attempts
 * to step beyond the end of the list after having reached the end of the list
 * will however trigger an %E_BOOK_SQL_ERROR_END_OF_LIST error.
 *
 * If %EBSQL_CURSOR_STEP_FETCH is specified in %flags, a pointer to 
 * a %NULL #GSList pointer should be provided for the @results parameter.
 *
 * The result list will be stored to @results and should be freed with g_slist_free()
 * and all elements freed with e_book_sqlite_search_data_free().
 *
 * Returns: The number of contacts traversed if successful, otherwise -1 is
 * returned and @error is set.
 *
 * Since: 3.12
 */
gint
e_book_sqlite_cursor_step (EBookSqlite          *ebsql,
			   EbSqlCursor          *cursor,
			   EbSqlCursorStepFlags  flags,
			   EbSqlCursorOrigin     origin,
			   gint                  count,
			   GSList              **results,
			   GError              **error)
{
	CursorCollectData data = { NULL, NULL, NULL, FALSE, 0 };
	CursorState *state;
	GString *query;
	gboolean success;
	EbSqlCursorOrigin try_position;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), -1);
	g_return_val_if_fail (cursor != NULL, -1);
	g_return_val_if_fail ((flags & EBSQL_CURSOR_STEP_FETCH) == 0 ||
			      (results != NULL && *results == NULL), -1);


	/* Check if this step should result in an end of list error first */
	try_position = cursor->state.position;
	if (origin != EBSQL_CURSOR_ORIGIN_CURRENT)
		try_position = origin;

	/* Report errors for requests to run off the end of the list */
	if (try_position == EBSQL_CURSOR_ORIGIN_BEGIN && count < 0) {
		EBSQL_SET_ERROR_LITERAL (
			error, E_BOOK_SQL_ERROR_END_OF_LIST,
			_("Tried to step a cursor in reverse, "
			  "but cursor is already at the beginning of the contact list"));
		
		return -1;
	} else if (try_position == EBSQL_CURSOR_ORIGIN_END && count > 0) {
		EBSQL_SET_ERROR_LITERAL (
			error, E_BOOK_SQL_ERROR_END_OF_LIST,
			_("Tried to step a cursor forwards, "
			  "but cursor is already at the end of the contact list"));

		return -1;
	}

	/* Nothing to do, silently return */
	if (count == 0 && try_position == EBSQL_CURSOR_ORIGIN_CURRENT)
		return 0;

	/* If we're not going to modify the position, just use
	 * a copy of the current cursor state.
	 */
	if ((flags & EBSQL_CURSOR_STEP_MOVE) != 0)
		state = &(cursor->state);
	else
		state = cursor_state_copy (cursor, &(cursor->state));

	/* Every query starts with the STATE_CURRENT position, first
	 * fix up the cursor state according to 'origin'
	 */
	switch (origin) {
	case EBSQL_CURSOR_ORIGIN_CURRENT:
		/* Do nothing, normal operation */
		break;

	case EBSQL_CURSOR_ORIGIN_BEGIN:
	case EBSQL_CURSOR_ORIGIN_END:

		/* Prepare the state before executing the query */
		cursor_state_clear (cursor, state, origin);
		break;
	}

	/* If count is 0 then there is no need to run any
	 * query, however it can be useful if you just want
	 * to move the cursor to the beginning or ending of
	 * the list.
	 */
	if (count == 0) {

		/* Free the state copy if need be */
		if ((flags & EBSQL_CURSOR_STEP_MOVE) == 0)
			cursor_state_free (cursor, state);

		return 0;
	}

	query = g_string_new (cursor->select_vcards);

	/* Add the filter constraints (if any) */
	if (cursor->query) {
		g_string_append (query, " WHERE ");

		g_string_append_c (query, '(');
		g_string_append (query, cursor->query);
		g_string_append_c (query, ')');
	}

	/* Add the cursor constraints (if any) */
	if (state->values[0] != NULL) {
		gchar *constraints = NULL;

		if (!cursor->query)
			g_string_append (query, " WHERE ");
		else
			g_string_append (query, " AND ");

		constraints = ebsql_cursor_constraints (ebsql,
							cursor,
							state,
							count < 0,
							FALSE);

		g_string_append_c (query, '(');
		g_string_append (query, constraints);
		g_string_append_c (query, ')');

		g_free (constraints);
	}

	/* Add the sort order */
	g_string_append_c (query, ' ');
	if (count > 0)
		g_string_append (query, cursor->order);
	else
		g_string_append (query, cursor->reverse_order);

	/* Add the limit */
	g_string_append_printf (query, " LIMIT %d", ABS (count));

	/* Specify whether we really want results or not */
	data.collect_results = (flags & EBSQL_CURSOR_STEP_FETCH) != 0;

	/* Execute the query */
	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);
	success = ebsql_exec (ebsql, query->str,
			      collect_results_for_cursor_cb, &data,
			      error);
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	g_string_free (query, TRUE);

	/* If there was no error, update the internal cursor state */
	if (success) {

		if (data.n_results < ABS (count)) {

			/* We've reached the end, clear the current state */
			if (count < 0)
				cursor_state_clear (cursor, state, EBSQL_CURSOR_ORIGIN_BEGIN);
			else
				cursor_state_clear (cursor, state, EBSQL_CURSOR_ORIGIN_END);

		} else if (data.last_vcard) {

			/* Set the cursor state to the last result */
			cursor_state_set_from_vcard (ebsql, cursor, state, data.last_vcard);
		} else
			/* Should never get here */
			g_warn_if_reached ();

		/* Assign the results to return (if any) */
		if (results) {
			/* Correct the order of results at the last minute */
			*results = g_slist_reverse (data.results);
			data.results = NULL;
		}
	}

	/* Cleanup what was allocated by collect_results_for_cursor_cb() */
	if (data.results)
		g_slist_free_full (data.results,
				   (GDestroyNotify)e_book_sqlite_search_data_free);
	g_free (data.alloc_vcard);

	/* Free the copy state if we were working with a copy */
	if ((flags & EBSQL_CURSOR_STEP_MOVE) == 0)
		cursor_state_free (cursor, state);

	if (success)
		return data.n_results;

	return -1;
}

/**
 * e_book_sqlite_cursor_set_target_alphabetic_index:
 * @ebsql: An #EBookSqlite
 * @cursor: The #EbSqlCursor to modify
 * @idx: The alphabetic index
 *
 * Sets the @cursor position to an
 * <link linkend="cursor-alphabet">Alphabetic Index</link>
 * into the alphabet active in @ebsql's locale.
 *
 * After setting the target to an alphabetic index, for example the
 * index for letter 'E', then further calls to e_book_sqlite_cursor_step()
 * will return results starting with the letter 'E' (or results starting
 * with the last result in 'D', if moving in a negative direction).
 *
 * The passed index must be a valid index in the active locale, knowledge
 * on the currently active alphabet index must be obtained using #ECollator
 * APIs.
 *
 * Use e_book_sqlite_ref_collator() to obtain the active collator for @ebsql.
 *
 * Since: 3.12
 */
void
e_book_sqlite_cursor_set_target_alphabetic_index (EBookSqlite *ebsql,
						  EbSqlCursor *cursor,
						  gint         idx)
{
	gint n_labels = 0;

	g_return_if_fail (E_IS_BOOK_SQLITE (ebsql));
	g_return_if_fail (cursor != NULL);
	g_return_if_fail (idx >= 0);

	e_collator_get_index_labels (ebsql->priv->collator, &n_labels,
				     NULL, NULL, NULL);
	g_return_if_fail (idx < n_labels);

	cursor_state_clear (cursor, &(cursor->state), EBSQL_CURSOR_ORIGIN_CURRENT);
	if (cursor->n_sort_fields > 0) {
		SummaryField *field;
		gchar *index_key;

		index_key = e_collator_generate_key_for_index (ebsql->priv->collator, idx);
		field     = summary_field_get (ebsql, cursor->sort_fields[0]);

		if (field && (field->index & INDEX_FLAG (SORT_KEY)) != 0) {
			cursor->state.values[0] = index_key;
		} else {
			cursor->state.values[0] = 
				ebsql_encode_vcard_sort_key (index_key);
			g_free (index_key);
		}
	}
}

/**
 * e_book_sqlite_cursor_set_sexp:
 * @ebsql: An #EBookSqlite
 * @cursor: The #EbSqlCursor
 * @sexp: The new query expression for @cursor
 * @error: A return location to store any error that might be reported.
 *
 * Modifies the current query expression for @cursor. This will not
 * modify @cursor's state, but will change the outcome of any further
 * calls to e_book_sqlite_cursor_calculate() or
 * e_book_sqlite_cursor_step().
 *
 * Returns: %TRUE if the expression was valid and accepted by @ebsql
 *
 * Since: 3.12
 */
gboolean
e_book_sqlite_cursor_set_sexp (EBookSqlite  *ebsql,
			       EbSqlCursor  *cursor,
			       const gchar  *sexp,
			       GError      **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (cursor != NULL, FALSE);

	/* We don't like '\0' sexps, prefer NULL */
	if (sexp && !sexp[0])
		sexp = NULL;

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);
	success = ebsql_cursor_setup_query (ebsql, cursor, sexp, error);
	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	return success;
}

/**
 * e_book_sqlite_cursor_calculate:
 * @ebsql: An #EBookSqlite
 * @cursor: The #EbSqlCursor
 * @total: (out) (allow-none): A return location to store the total result set for this cursor
 * @position: (out) (allow-none): A return location to store the total results before the cursor value
 * @error: (allow-none): A return location to store any error that might be reported.
 *
 * Calculates the @total amount of results for the @cursor's query expression,
 * as well as the current @position of @cursor in the results. @position is
 * represented as the amount of results which lead up to the current value
 * of @cursor, if @cursor currently points to an exact contact, the position
 * also includes the cursor contact.
 *
 * Returns: Whether @total and @position were successfully calculated.
 *
 * Since: 3.12
 */
gboolean
e_book_sqlite_cursor_calculate (EBookSqlite  *ebsql,
				EbSqlCursor  *cursor,
				gint         *total,
				gint         *position,
				GError      **error)
{
	gboolean success = TRUE;
	gint local_total = 0;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), FALSE);
	g_return_val_if_fail (cursor != NULL, FALSE);

	/* If we're in a clear cursor state, then the position is 0 */
	if (position && cursor->state.values[0] == NULL) {

		if (cursor->state.position == EBSQL_CURSOR_ORIGIN_BEGIN) {
			/* Mark the local pointer NULL, no need to calculate this anymore */
			*position = 0;
			position = NULL;
		} else if (cursor->state.position == EBSQL_CURSOR_ORIGIN_END) {

			/* Make sure that we look up the total so we can
			 * set the position to 'total + 1'
			 */
			if (!total)
				total = &local_total;
		}
	}

	/* Early return if there is nothing to do */
	if (!total && !position)
		return TRUE;

	EBSQL_LOCK_MUTEX (&ebsql->priv->lock);

	/* Start a read transaction, it's important our two queries are atomic */
	if (!ebsql_start_transaction (ebsql, FALSE, error)) {
		EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);
		return FALSE;
	}

	if (total)
		success = cursor_count_total_locked (ebsql, cursor, total, error);

	if (success && position)
		success = cursor_count_position_locked (ebsql, cursor, position, error);

	if (success)
		success = ebsql_commit_transaction (ebsql, error);
	else
		/* The GError is already set. */
		ebsql_rollback_transaction (ebsql, NULL);


	EBSQL_UNLOCK_MUTEX (&ebsql->priv->lock);

	/* In the case we're at the end, we just set the position
	 * to be the total + 1
	 */
	if (success && position && total &&
	    cursor->state.position == EBSQL_CURSOR_ORIGIN_END)
		*position = *total + 1;

	return success;
}

/**
 * e_book_sqlite_cursor_compare_contact:
 * @ebsql: An #EBookSqlite
 * @cursor: The #EbSqlCursor
 * @contact: The #EContact to compare
 * @matches_sexp: (out) (allow-none): Whether the contact matches the cursor's search expression
 *
 * Compares @contact with @cursor and returns whether @contact is less than, equal to, or greater
 * than @cursor.
 *
 * Returns: A value that is less than, equal to, or greater than zero if @contact is found,
 * respectively, to be less than, to match, or be greater than the current value of @cursor.
 *
 * Since: 3.12
 */
gint
e_book_sqlite_cursor_compare_contact (EBookSqlite *ebsql,
				      EbSqlCursor *cursor,
				      EContact    *contact,
				      gboolean    *matches_sexp)
{
	EBookSqlitePrivate *priv;
	gint i;
	gint comparison = 0;

	g_return_val_if_fail (E_IS_BOOK_SQLITE (ebsql), -1);
	g_return_val_if_fail (E_IS_CONTACT (contact), -1);
	g_return_val_if_fail (cursor != NULL, -1);

	priv = ebsql->priv;

	if (matches_sexp) {
		if (cursor->sexp == NULL)
			*matches_sexp = TRUE;
		else
			*matches_sexp =
				e_book_backend_sexp_match_contact (cursor->sexp, contact);
	}

	for (i = 0; i < cursor->n_sort_fields && comparison == 0; i++) {
		SummaryField *field;
		gchar *contact_key = NULL;
		const gchar *cursor_key = NULL;
		const gchar *field_value;
		gchar *freeme = NULL;

		field_value = (const gchar *)e_contact_get_const (contact, cursor->sort_fields[i]);
		if (field_value)
			contact_key = e_collator_generate_key (priv->collator, field_value, NULL);
			
		field = summary_field_get (ebsql, cursor->sort_fields[i]);

		if (field && (field->index & INDEX_FLAG (SORT_KEY)) != 0) {
			cursor_key = cursor->state.values[i];
		} else {

			if (cursor->state.values[i])
				freeme = ebsql_decode_vcard_sort_key (cursor->state.values[i]);

			cursor_key = freeme;
		}

		/* Empty state sorts below any contact value, which means the contact sorts above cursor */
		if (cursor_key == NULL)
			comparison = 1;
		else
			/* Check if contact sorts below, equal to, or above the cursor */
			comparison = g_strcmp0 (contact_key, cursor_key);

		g_free (contact_key);
		g_free (freeme);
	}

	/* UID tie-breaker */
	if (comparison == 0) {
		const gchar *uid;

		uid = (const gchar *)e_contact_get_const (contact, E_CONTACT_UID);

		if (cursor->state.last_uid == NULL)
			comparison = 1;
		else if (uid == NULL)
			comparison = -1;
		else
			comparison = strcmp (uid, cursor->state.last_uid);
	}

	return comparison;
}
