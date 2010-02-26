/* EDBus - GLib D-Bus Library
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#if !defined (__E_DBUS_H_INSIDE__) && !defined (E_DBUS_COMPILATION)
#error "Only <edbus/edbus.h> can be included directly."
#endif

#ifndef __E_DBUS_NAME_WATCHING_H__
#define __E_DBUS_NAME_WATCHING_H__

#include <edbus/edbustypes.h>

G_BEGIN_DECLS

/**
 * GBusNameAppearedCallback:
 * @connection: The #EDBusConnection the name is being watched on.
 * @name: The name being watched.
 * @name_owner: Unique name of the owner of the name being watched.
 * @user_data: User data passed to e_bus_watch_name().
 *
 * Invoked when the name being watched is known to have to have a owner.
 */
typedef void (*GBusNameAppearedCallback) (EDBusConnection *connection,
                                          const gchar     *name,
                                          const gchar     *name_owner,
                                          gpointer         user_data);

/**
 * GBusNameVanishedCallback:
 * @connection: The #EDBusConnection the name is being watched on.
 * @name: The name being watched.
 * @user_data: User data passed to e_bus_watch_name().
 *
 * Invoked when the name being watched is known not to have to have a owner.
 */
typedef void (*GBusNameVanishedCallback) (EDBusConnection *connection,
                                          const gchar     *name,
                                          gpointer         user_data);


guint e_bus_watch_name   (GBusType                  bus_type,
                          const gchar              *name,
                          GBusNameAppearedCallback  name_appeared_handler,
                          GBusNameVanishedCallback  name_vanished_handler,
                          gpointer                  user_data,
                          GDestroyNotify            user_data_free_func);
void  e_bus_unwatch_name (guint                     watcher_id);

G_END_DECLS

#endif /* __E_DBUS_NAME_WATCHING_H__ */
