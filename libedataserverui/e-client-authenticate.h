/*
 * e-client-authenticate.h
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

#ifndef E_CLIENT_AUTHENTICATE_H
#define E_CLIENT_AUTHENTICATE_H

#include <glib.h>
#include <gtk/gtk.h>

#include <libedataserver/e-client.h>

G_BEGIN_DECLS

gboolean e_client_authenticate_handler (EClient *client, ECredentials *credentials);
gboolean e_credentials_authenticate_helper (ECredentials *credentials, GtkWindow *parent, gboolean *remember_password);

G_END_DECLS

#endif /* E_CLIENT_AUTHENTICATE_H */