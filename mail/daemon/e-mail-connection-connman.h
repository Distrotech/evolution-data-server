
/* e-mail-connection-connman.h */

#ifndef _E_MAIL_CONNECTION_CONNMAN
#define _E_MAIL_CONNECTION_CONNMAN

#include <glib-object.h>

G_BEGIN_DECLS

#define E_MAIL_TYPE_CONNECTION_CONNMAN e_mail_connection_connman_get_type()

#define E_MAIL_CONNECTION_CONNMAN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_MAIL_TYPE_CONNECTION_CONNMAN, EMailConnectionConnMan))

#define E_MAIL_CONNECTION_CONNMAN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), E_MAIL_TYPE_CONNECTION_CONNMAN, EMailConnectionConnManClass))

#define E_MAIL_IS_CONNECTION_CONNMAN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_MAIL_TYPE_CONNECTION_CONNMAN))

#define E_MAIL_IS_CONNECTION_CONNMAN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), E_MAIL_TYPE_CONNECTION_CONNMAN))

#define E_MAIL_CONNECTION_CONNMAN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_MAIL_TYPE_CONNECTION_CONNMAN, EMailConnectionConnManClass))

typedef struct {
  GObject parent;
} EMailConnectionConnMan;

typedef struct {
  GObjectClass parent_class;
} EMailConnectionConnManClass;

GType e_mail_connection_connman_get_type (void);

EMailConnectionConnMan* e_mail_connection_connman_new (void);

G_END_DECLS

#endif /* _E_MAIL_CONNECTION_CONNMAN */
