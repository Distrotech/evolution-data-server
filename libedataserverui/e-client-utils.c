/*
 * e-client-utils.c
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
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>

#include <libedataserver/e-client.h>
#include "libedataserver/e-client-private.h"
#include <libebook/e-book-client.h>
#include <libecal/e-cal-client.h>

#include "e-passwords.h"
#include "e-client-utils.h"

/**
 * e_client_utils_new:
 *
 * Proxy function for e_book_client_utils_new() and e_cal_client_utils_new().
 **/
EClient	*
e_client_utils_new (ESource *source, EClientSourceType source_type, GError **error)
{
	EClient *res = NULL;

	g_return_val_if_fail (source != NULL, NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = E_CLIENT (e_book_client_new (source, error));
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = E_CLIENT (e_cal_client_new (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = E_CLIENT (e_cal_client_new (source, E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = E_CLIENT (e_cal_client_new (source, E_CAL_CLIENT_SOURCE_TYPE_TASKS, error));
		break;
	default:
		g_return_val_if_reached (NULL);
		break;
	}

	return res;
}

/**
 * e_client_utils_new_from_uri:
 *
 * Proxy function for e_book_client_utils_new_from_uri() and e_cal_client_utils_new_from_uri().
 **/
EClient *
e_client_utils_new_from_uri (const gchar *uri, EClientSourceType source_type, GError **error)
{
	EClient *res = NULL;

	g_return_val_if_fail (uri != NULL, NULL);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = E_CLIENT (e_book_client_new_from_uri (uri, error));
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = E_CLIENT (e_cal_client_new_from_uri (uri, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = E_CLIENT (e_cal_client_new_from_uri (uri, E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = E_CLIENT (e_cal_client_new_from_uri (uri, E_CAL_CLIENT_SOURCE_TYPE_TASKS, error));
		break;
	default:
		g_return_val_if_reached (NULL);
		break;
	}

	return res;
}

/**
 * e_client_utils_new_system:
 *
 * Proxy function for e_book_client_utils_new_system() and e_cal_client_utils_new_system().
 **/
EClient *
e_client_utils_new_system (EClientSourceType source_type, GError **error)
{
	EClient *res = NULL;

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = E_CLIENT (e_book_client_new_system (error));
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = E_CLIENT (e_cal_client_new_system (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = E_CLIENT (e_cal_client_new_system (E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = E_CLIENT (e_cal_client_new_system (E_CAL_CLIENT_SOURCE_TYPE_TASKS, error));
		break;
	default:
		g_return_val_if_reached (NULL);
		break;
	}

	return res;
}

/**
 * e_client_utils_new_default:
 *
 * Proxy function for e_book_client_utils_new_default() and e_cal_client_utils_new_default().
 **/
EClient *
e_client_utils_new_default (EClientSourceType source_type, GError **error)
{
	EClient *res = NULL;

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = E_CLIENT (e_book_client_new_default (error));
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = E_CLIENT (e_cal_client_new_default (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = E_CLIENT (e_cal_client_new_default (E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = E_CLIENT (e_cal_client_new_default (E_CAL_CLIENT_SOURCE_TYPE_TASKS, error));
		break;
	default:
		g_return_val_if_reached (NULL);
		break;
	}

	return res;
}

/**
 * e_client_utils_set_default:
 *
 * Proxy function for e_book_client_utils_set_default() and e_book_client_utils_set_default().
 **/
gboolean
e_client_utils_set_default (EClient *client, EClientSourceType source_type, GError **error)
{
	gboolean res = FALSE;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
		res = e_book_client_set_default (E_BOOK_CLIENT (client), error);
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
	case E_CLIENT_SOURCE_TYPE_MEMOS:
	case E_CLIENT_SOURCE_TYPE_TASKS:
		g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
		res = e_cal_client_set_default (E_CAL_CLIENT (client), error);
		break;
	default:
		g_return_val_if_reached (FALSE);
		break;
	}

	return res;
}

/**
 * e_client_utils_set_default_source:
 *
 * Proxy function for e_book_client_utils_set_default_source() and e_cal_client_utils_set_default_source().
 **/
gboolean
e_client_utils_set_default_source (ESource *source, EClientSourceType source_type, GError **error)
{
	gboolean res = FALSE;

	g_return_val_if_fail (source != NULL, FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = e_book_client_set_default_source (source, error);
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = e_cal_client_set_default_source (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error);
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = e_cal_client_set_default_source (source, E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error);
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = e_cal_client_set_default_source (source, E_CAL_CLIENT_SOURCE_TYPE_TASKS, error);
		break;
	default:
		g_return_val_if_reached (FALSE);
		break;
	}

	return res;
}

/**
 * e_client_utils_get_sources:
 *
 * Proxy function for e_book_client_utils_get_sources() and e_cal_client_utils_get_sources().
 **/
gboolean
e_client_utils_get_sources (ESourceList **sources, EClientSourceType source_type, GError **error)
{
	gboolean res = FALSE;

	g_return_val_if_fail (sources != NULL, FALSE);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = e_book_client_get_sources (sources, error);
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = e_cal_client_get_sources (sources, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error);
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = e_cal_client_get_sources (sources, E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error);
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = e_cal_client_get_sources (sources, E_CAL_CLIENT_SOURCE_TYPE_TASKS, error);
		break;
	default:
		g_return_val_if_reached (FALSE);
		break;
	}

	return res;
}

/* This function is suitable as a handler for EClient::authenticate signal.
   It takes care of all the password prompt and such and returns TRUE if
   credentials (password) were provided. Thus just connect it to that signal
   and it'll take care of everything else.
*/
gboolean
e_client_utils_authenticate_handler (EClient *client, ECredentials *credentials, gpointer unused_user_data)
{
	ESource *source;
	gboolean is_book, is_cal, res, remember_password = FALSE;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (credentials != NULL, FALSE);

	is_book = E_IS_BOOK_CLIENT (client);
	is_cal = !is_book && E_IS_CAL_CLIENT (client);
	g_return_val_if_fail (is_book || is_cal, FALSE);

	source = e_client_get_source (client);
	g_return_val_if_fail (source != NULL, FALSE);

	if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_USERNAME)) {
		e_credentials_set (credentials, E_CREDENTIALS_KEY_USERNAME, e_source_get_property (source, "username"));

		/* no username set on the source - deny authentication request until
		   username will be also enterable with e-passwords */
		if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_USERNAME))
			return FALSE;
	}

	if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_AUTH_DOMAIN))
		e_credentials_set (credentials, E_CREDENTIALS_KEY_AUTH_DOMAIN, is_book ? E_CREDENTIALS_AUTH_DOMAIN_ADDRESSBOOK : E_CREDENTIALS_AUTH_DOMAIN_CALENDAR);

	if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_TEXT)) {
		gchar *prompt;
		gchar *username_markup, *source_name_markup;

		username_markup = g_markup_printf_escaped ("<b>%s</b>", e_credentials_peek (credentials, E_CREDENTIALS_KEY_USERNAME));
		source_name_markup = g_markup_printf_escaped ("<b>%s</b>", e_source_peek_name (source));

		prompt = g_strdup_printf (_("Enter password for %s (user %s)"), source_name_markup, username_markup);

		e_credentials_set (credentials, E_CREDENTIALS_KEY_PROMPT_TEXT, prompt);

		g_free (username_markup);
		g_free (source_name_markup);
		g_free (prompt);
	}

	if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_KEY)) {
		SoupURI *suri;
		gchar *uri_str;

		suri = soup_uri_new (e_client_get_uri (client));
		g_return_val_if_fail (suri != NULL, FALSE);

		soup_uri_set_user (suri, e_credentials_peek (credentials, E_CREDENTIALS_KEY_USERNAME));
		soup_uri_set_password (suri, NULL);
		soup_uri_set_fragment (suri, NULL);

		uri_str = soup_uri_to_string (suri, FALSE);

		e_credentials_set (credentials, E_CREDENTIALS_KEY_PROMPT_KEY, uri_str);

		g_free (uri_str);
		soup_uri_free (suri);
	}

	remember_password = g_strcmp0 (e_source_get_property (source, "remember_password"), "true") == 0;

	res = e_credentials_authenticate_helper (credentials, NULL, &remember_password);

	if (res)
		e_source_set_property (source, "remember_password", remember_password ? "true" : NULL);

	e_credentials_clear_peek (credentials);

	return res;
}

/* Asks for a password based on the provided credentials information.
   Credentials should have set following keys:
      E_CREDENTIALS_KEY_USERNAME
      E_CREDENTIALS_KEY_AUTH_DOMAIN
      E_CREDENTIALS_KEY_PROMPT_TEXT
      E_CREDENTIALS_KEY_PROMPT_KEY
   all other keys are optional. If also E_CREDENTIALS_KEY_PASSWORD key is provided,
   then it implies a reprompt.

   When this returns TRUE, then the structure contains E_CREDENTIALS_KEY_PASSWORD set
   as entered by a user.
*/
gboolean
e_credentials_authenticate_helper (ECredentials *credentials, GtkWindow *parent, gboolean *remember_password)
{
	gboolean res, fake_remember_password = FALSE;
	guint prompt_flags;
	gchar *password = NULL;
	const gchar *title, *auth_domain, *prompt_key;

	g_return_val_if_fail (credentials != NULL, FALSE);
	g_return_val_if_fail (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_USERNAME), FALSE);
	g_return_val_if_fail (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_AUTH_DOMAIN), FALSE);
	g_return_val_if_fail (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_TEXT), FALSE);
	g_return_val_if_fail (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_KEY), FALSE);

	if (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_FLAGS)) {
		prompt_flags = e_credentials_util_string_to_prompt_flags (e_credentials_peek (credentials, E_CREDENTIALS_KEY_PROMPT_FLAGS));
	} else {
		prompt_flags = E_CREDENTIALS_PROMPT_FLAG_SECRET | E_CREDENTIALS_PROMPT_FLAG_ONLINE;
	}

	if (!remember_password) {
		prompt_flags |= E_CREDENTIALS_PROMPT_FLAG_DISABLE_REMEMBER;
		remember_password = &fake_remember_password;
	}

	if (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PASSWORD))
		prompt_flags |= E_CREDENTIALS_PROMPT_FLAG_REPROMPT;

	if (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_TITLE))
		title = e_credentials_peek (credentials, E_CREDENTIALS_KEY_PROMPT_TITLE);
	else if (prompt_flags & E_CREDENTIALS_PROMPT_FLAG_PASSPHRASE)
		title = _("Enter Passphrase");
	else
		title = _("Enter Password");

	auth_domain = e_credentials_peek (credentials, E_CREDENTIALS_KEY_AUTH_DOMAIN);
	prompt_key = e_credentials_peek (credentials, E_CREDENTIALS_KEY_PROMPT_KEY);

	if (!(prompt_flags & E_CREDENTIALS_PROMPT_FLAG_REPROMPT))
		password = e_passwords_get_password (auth_domain, prompt_key);

	if (!password)
		password = e_passwords_ask_password (title, auth_domain, prompt_key,
				e_credentials_peek (credentials, E_CREDENTIALS_KEY_PROMPT_TEXT),
				prompt_flags, remember_password, parent);

	res = password != NULL;

	if (res)
		e_credentials_set (credentials, E_CREDENTIALS_KEY_PASSWORD, password);

	e_credentials_util_safe_free_string (password);
	e_credentials_clear_peek (credentials);

	return res;
}
