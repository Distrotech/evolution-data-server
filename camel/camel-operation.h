/*
 * camel-operation.h
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_OPERATION_H
#define CAMEL_OPERATION_H

#include <gio/gio.h>

/* Standard GObject macros */
#define CAMEL_TYPE_OPERATION \
	(camel_operation_get_type ())
#define CAMEL_OPERATION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_OPERATION, CamelOperation))
#define CAMEL_OPERATION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_OPERATION, CamelOperationClass))
#define CAMEL_IS_OPERATION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_OPERATION))
#define CAMEL_IS_OPERATION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_OPERATION))
#define CAMEL_OPERATION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_OPERATION, CamelOperationClass))

G_BEGIN_DECLS

typedef struct _CamelOperation CamelOperation;
typedef struct _CamelOperationClass CamelOperationClass;
typedef struct _CamelOperationPrivate CamelOperationPrivate;

struct _CamelOperation {
	GCancellable parent;
	CamelOperationPrivate *priv;
};

struct _CamelOperationClass {
	GCancellableClass parent_class;

	/* Signals */
	void		(*status)		(CamelOperation *operation,
						 const gchar *what,
						 gint pc);
};

GType		camel_operation_get_type	(void);
GCancellable *	camel_operation_new		(void);
void		camel_operation_cancel_all	(void);

/* Since Camel methods pass around GCancellable pointers instead of
 * CamelOperation pointers, it's more convenient to callers to take
 * a GCancellable pointer and just return silently if the pointer is
 * NULL or the pointed to object actually is a plain GCancellable. */

void		camel_operation_push_message	(GCancellable *cancellable,
						 const gchar *format,
						 ...) G_GNUC_PRINTF (2, 3);
void		camel_operation_pop_message	(GCancellable *cancellable);
void		camel_operation_progress	(GCancellable *cancellable,
						 gint percent);

G_END_DECLS

#endif /* CAMEL_OPERATION_H */
