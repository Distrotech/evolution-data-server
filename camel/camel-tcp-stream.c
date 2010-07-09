/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-tcp-stream.h"

#define w(x)

typedef struct _CamelTcpStreamPrivate CamelTcpStreamPrivate;

struct _CamelTcpStreamPrivate {
	gchar *socks_host;
	gint socks_port;
};

static CamelStreamClass *parent_class = NULL;

/* Returns the class for a CamelTcpStream */
#define CTS_CLASS(so) CAMEL_TCP_STREAM_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static gint tcp_connect    (CamelTcpStream *stream, const char *host, const char *service, gint fallback_port, CamelException *ex);
static gint tcp_getsockopt (CamelTcpStream *stream, CamelSockOptData *data);
static gint tcp_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data);
static struct sockaddr *tcp_get_local_address (CamelTcpStream *stream, socklen_t *len);
static struct sockaddr *tcp_get_remote_address (CamelTcpStream *stream, socklen_t *len);

static void
camel_tcp_stream_class_init (CamelTcpStreamClass *camel_tcp_stream_class)
{
	/*CamelStreamClass *camel_stream_class = CAMEL_STREAM_CLASS (camel_tcp_stream_class);*/

	parent_class = CAMEL_STREAM_CLASS (camel_type_get_global_classfuncs (CAMEL_STREAM_TYPE));

	/* tcp stream methods */
	camel_tcp_stream_class->connect            = tcp_connect;
	camel_tcp_stream_class->getsockopt         = tcp_getsockopt;
	camel_tcp_stream_class->setsockopt         = tcp_setsockopt;
	camel_tcp_stream_class->get_local_address  = tcp_get_local_address;
	camel_tcp_stream_class->get_remote_address = tcp_get_remote_address;
}

static void
camel_tcp_stream_init (gpointer o)
{
	CamelTcpStream *stream = o;
	CamelTcpStreamPrivate *priv;

	priv = g_slice_new0 (CamelTcpStreamPrivate);
	stream->priv = priv;

	priv->socks_host = NULL;
	priv->socks_port = 0;
}

static void
camel_tcp_stream_finalize (CamelTcpStream *stream)
{
	CamelTcpStreamPrivate *priv;

	priv = stream->priv;
	g_free (priv->socks_host);

	g_slice_free (CamelTcpStreamPrivate, priv);
	stream->priv = NULL;
}

CamelType
camel_tcp_stream_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (CAMEL_STREAM_TYPE,
					    "CamelTcpStream",
					    sizeof (CamelTcpStream),
					    sizeof (CamelTcpStreamClass),
					    (CamelObjectClassInitFunc) camel_tcp_stream_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_tcp_stream_init,
					    (CamelObjectFinalizeFunc) camel_tcp_stream_finalize);
	}

	return type;
}

static gint
tcp_connect (CamelTcpStream *stream, const char *host, const char *service, gint fallback_port, CamelException *ex)
{
	w(g_warning ("CamelTcpStream::connect called on default implementation"));
	return -1;
}

/**
 * camel_tcp_stream_connect:
 * @stream: a #CamelTcpStream object
 * @host: Hostname for connection
 * @service: Service name or port number in string form
 * @fallback_port: Port number to retry if @service is not present in the system's services database, or 0 to avoid retrying.
 * @ex: a #CamelException
 *
 * Create a socket and connect based upon the data provided.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_connect (CamelTcpStream *stream, const char *host, const char *service, gint fallback_port, CamelException *ex)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);
	g_return_val_if_fail (host != NULL, -1);
	g_return_val_if_fail (service != NULL, -1);
	g_return_val_if_fail (ex == NULL || !camel_exception_is_set (ex), -1);

	return CTS_CLASS (stream)->connect (stream, host, service, fallback_port, ex);
}

static gint
tcp_getsockopt (CamelTcpStream *stream, CamelSockOptData *data)
{
	w(g_warning ("CamelTcpStream::getsockopt called on default implementation"));
	return -1;
}

/**
 * camel_tcp_stream_getsockopt:
 * @stream: a #CamelTcpStream object
 * @data: socket option data
 *
 * Get the socket options set on the stream and populate @data.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	return CTS_CLASS (stream)->getsockopt (stream, data);
}

static gint
tcp_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data)
{
	w(g_warning ("CamelTcpStream::setsockopt called on default implementation"));
	return -1;
}

/**
 * camel_tcp_stream_setsockopt:
 * @stream: a #CamelTcpStream object
 * @data: socket option data
 *
 * Set the socket options contained in @data on the stream.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), -1);

	return CTS_CLASS (stream)->setsockopt (stream, data);
}

/**
 * camel_tcp_stream_set_socks_proxy:
 * @stream: a #CamelTcpStream object
 * @socks_host: hostname to use for the SOCKS proxy
 * @socks_port: port number to use for the SOCKS proxy
 *
 * Configures a SOCKS proxy for the specified @stream.  Instead of direct connections,
 * this @stream will instead go through the proxy.
 */
void
camel_tcp_stream_set_socks_proxy (CamelTcpStream *stream, const gchar *socks_host, gint socks_port)
{
	CamelTcpStreamPrivate *priv;

	g_return_if_fail (CAMEL_IS_TCP_STREAM (stream));

	priv = stream->priv;

	g_free (priv->socks_host);

	if (socks_host && socks_host[0] != '\0') {
		priv->socks_host = g_strdup (socks_host);
		priv->socks_port = socks_port;
	} else {
		priv->socks_host = NULL;
		priv->socks_port = 0;
	}
}

void
camel_tcp_stream_peek_socks_proxy (CamelTcpStream *stream, const gchar **socks_host_ret, gint *socks_port_ret)
{
	CamelTcpStreamPrivate *priv;

	g_return_if_fail (CAMEL_IS_TCP_STREAM (stream));

	priv = stream->priv;

	if (socks_host_ret)
		*socks_host_ret = priv->socks_host;

	if (socks_port_ret)
		*socks_port_ret = priv->socks_port;
}

static struct sockaddr *
tcp_get_local_address (CamelTcpStream *stream, socklen_t *len)
{
	w(g_warning ("CamelTcpStream::get_local_address called on default implementation"));
	return NULL;
}

/**
 * camel_tcp_stream_get_local_address:
 * @stream: a #CamelTcpStream object
 * @len: pointer to address length which must be supplied
 *
 * Get the local address of @stream.
 *
 * Returns: the stream's local address (which must be freed with
 * #g_free) if the stream is connected, or %NULL if not
 *
 * Since: 2.22
 **/
struct sockaddr *
camel_tcp_stream_get_local_address (CamelTcpStream *stream, socklen_t *len)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), NULL);
	g_return_val_if_fail(len != NULL, NULL);

	return CTS_CLASS (stream)->get_local_address (stream, len);
}

static struct sockaddr *
tcp_get_remote_address (CamelTcpStream *stream, socklen_t *len)
{
	w(g_warning ("CamelTcpStream::get_remote_address called on default implementation"));
	return NULL;
}

/**
 * camel_tcp_stream_get_remote_address:
 * @stream: a #CamelTcpStream object
 * @len: pointer to address length, which must be supplied
 *
 * Get the remote address of @stream.
 *
 * Returns: the stream's remote address (which must be freed with
 * #g_free) if the stream is connected, or %NULL if not.
 *
 * Since: 2.22
 **/
struct sockaddr *
camel_tcp_stream_get_remote_address (CamelTcpStream *stream, socklen_t *len)
{
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM (stream), NULL);
	g_return_val_if_fail(len != NULL, NULL);

	return CTS_CLASS (stream)->get_remote_address (stream, len);
}
