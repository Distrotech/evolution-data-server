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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include "camel-file-utils.h"
#include "camel-net-utils.h"
#include "camel-operation.h"
#include "camel-tcp-stream-raw.h"

typedef struct _CamelTcpStreamRawPrivate {
	gint sockfd;
#ifdef G_OS_WIN32
	gint is_nonblocking;
#endif
} CamelTcpStreamRawPrivate;

#ifndef G_OS_WIN32
#define SOCKET_ERROR_CODE() errno
#define SOCKET_CLOSE(fd) close (fd)
#define SOCKET_ERROR_IS_EINPROGRESS() (errno == EINPROGRESS)
#define SOCKET_ERROR_IS_EINTR() (errno == EINTR)
#else
#define SOCKET_ERROR_CODE() WSAGetLastError ()
#define SOCKET_CLOSE(fd) closesocket (fd)
#define SOCKET_ERROR_IS_EINPROGRESS() (WSAGetLastError () == WSAEWOULDBLOCK)
#define SOCKET_ERROR_IS_EINTR() 0 /* No WSAEINTR in WinSock2 */
#undef ETIMEDOUT		/* In case pthreads-win32's <pthread.h> bogusly defined it */
#define ETIMEDOUT EAGAIN
#endif

static CamelTcpStreamClass *parent_class = NULL;

/* Returns the class for a CamelTcpStreamRaw */
#define CTSR_CLASS(so) CAMEL_TCP_STREAM_RAW_CLASS (CAMEL_OBJECT_GET_CLASS (so))

static gssize stream_read (CamelStream *stream, gchar *buffer, gsize n);
static gssize stream_write (CamelStream *stream, const gchar *buffer, gsize n);
static gint stream_flush  (CamelStream *stream);
static gint stream_close  (CamelStream *stream);

static gint stream_connect (CamelTcpStream *stream, const char *host, const char *service, gint fallback_port, CamelException *ex);
static gint stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data);
static gint stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data);
static struct sockaddr *stream_get_local_address (CamelTcpStream *stream, socklen_t *len);
static struct sockaddr *stream_get_remote_address (CamelTcpStream *stream, socklen_t *len);

static void
camel_tcp_stream_raw_class_init (CamelTcpStreamRawClass *camel_tcp_stream_raw_class)
{
	CamelTcpStreamClass *camel_tcp_stream_class =
		CAMEL_TCP_STREAM_CLASS (camel_tcp_stream_raw_class);
	CamelStreamClass *camel_stream_class =
		CAMEL_STREAM_CLASS (camel_tcp_stream_raw_class);

	parent_class = CAMEL_TCP_STREAM_CLASS (camel_type_get_global_classfuncs (camel_tcp_stream_get_type ()));

	/* virtual method overload */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
	camel_stream_class->flush = stream_flush;
	camel_stream_class->close = stream_close;

	camel_tcp_stream_class->connect = stream_connect;
	camel_tcp_stream_class->getsockopt = stream_getsockopt;
	camel_tcp_stream_class->setsockopt  = stream_setsockopt;
	camel_tcp_stream_class->get_local_address  = stream_get_local_address;
	camel_tcp_stream_class->get_remote_address = stream_get_remote_address;
}

static void
camel_tcp_stream_raw_init (gpointer object, gpointer klass)
{
	CamelTcpStreamRaw *stream = CAMEL_TCP_STREAM_RAW (object);
	CamelTcpStreamRawPrivate *priv;

	stream->priv = g_new0 (CamelTcpStreamRawPrivate, 1);
	priv = stream->priv;

	priv->sockfd = -1;
}

static void
camel_tcp_stream_raw_finalize (CamelObject *object)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (object);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	if (priv->sockfd != -1)
		SOCKET_CLOSE (priv->sockfd);

	g_free (raw->priv);
	raw->priv = NULL;
}

CamelType
camel_tcp_stream_raw_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_tcp_stream_get_type (),
					    "CamelTcpStreamRaw",
					    sizeof (CamelTcpStreamRaw),
					    sizeof (CamelTcpStreamRawClass),
					    (CamelObjectClassInitFunc) camel_tcp_stream_raw_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_tcp_stream_raw_init,
					    (CamelObjectFinalizeFunc) camel_tcp_stream_raw_finalize);
	}

	return type;
}

#ifdef SIMULATE_FLAKY_NETWORK
static gssize
flaky_tcp_write (gint fd, const gchar *buffer, gsize buflen)
{
	gsize len = buflen;
	gssize nwritten;
	gint val;

	if (buflen == 0)
		return 0;

	val = 1 + (gint) (10.0 * rand () / (RAND_MAX + 1.0));

	switch (val) {
	case 1:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EINTR\n", fd, buflen);
		errno = EINTR;
		return -1;
	case 2:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EAGAIN\n", fd, buflen);
		errno = EAGAIN;
		return -1;
	case 3:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EWOULDBLOCK\n", fd, buflen);
		errno = EWOULDBLOCK;
		return -1;
	case 4:
	case 5:
	case 6:
		len = 1 + (gsize) (buflen * rand () / (RAND_MAX + 1.0));
		len = MIN (len, buflen);
		/* fall through... */
	default:
		printf ("flaky_tcp_write (%d, ..., %d): (%d) '%.*s'", fd, buflen, len, (gint) len, buffer);
		nwritten = write (fd, buffer, len);
		if (nwritten < 0)
			printf (" errno => %s\n", g_strerror (errno));
		else if (nwritten < len)
			printf (" only wrote %d bytes\n", nwritten);
		else
			printf ("\n");

		return nwritten;
	}
}

#define write(fd, buffer, buflen) flaky_tcp_write (fd, buffer, buflen)

static gssize
flaky_tcp_read (gint fd, gchar *buffer, gsize buflen)
{
	gsize len = buflen;
	gssize nread;
	gint val;

	if (buflen == 0)
		return 0;

	val = 1 + (gint) (10.0 * rand () / (RAND_MAX + 1.0));

	switch (val) {
	case 1:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EINTR\n", fd, buflen);
		errno = EINTR;
		return -1;
	case 2:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EAGAIN\n", fd, buflen);
		errno = EAGAIN;
		return -1;
	case 3:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EWOULDBLOCK\n", fd, buflen);
		errno = EWOULDBLOCK;
		return -1;
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
		len = 1 + (gsize) (10.0 * rand () / (RAND_MAX + 1.0));
		len = MIN (len, buflen);
		/* fall through... */
	default:
		printf ("flaky_tcp_read (%d, ..., %d): (%d)", fd, buflen, len);
		nread = read (fd, buffer, len);
		if (nread < 0)
			printf (" errno => %s\n", g_strerror (errno));
		else if (nread < len)
			printf (" only read %d bytes\n", nread);
		else
			printf ("\n");

		return nread;
	}
}

#define read(fd, buffer, buflen) flaky_tcp_read (fd, buffer, buflen)

#endif /* SIMULATE_FLAKY_NETWORK */

/**
 * camel_tcp_stream_raw_new:
 *
 * Create a new #CamelTcpStreamRaw object.
 *
 * Returns: a new #CamelTcpStream object
 **/
CamelStream *
camel_tcp_stream_raw_new (void)
{
	CamelTcpStreamRaw *stream;

	stream = CAMEL_TCP_STREAM_RAW (camel_object_new (camel_tcp_stream_raw_get_type ()));

	return CAMEL_STREAM (stream);
}

static gssize
stream_read (CamelStream *stream, gchar *buffer, gsize n)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return camel_read_socket (priv->sockfd, buffer, n);
}

static gssize
stream_write (CamelStream *stream, const gchar *buffer, gsize n)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return camel_write_socket (priv->sockfd, buffer, n);
}

static gint
stream_flush (CamelStream *stream)
{
	return 0;
}

static gint
stream_close (CamelStream *stream)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	if (SOCKET_CLOSE (priv->sockfd) == -1)
		return -1;

	priv->sockfd = -1;
	return 0;
}

/* this is a 'cancellable' connect, cancellable from camel_operation_cancel etc */
/* returns -1 & errno == EINTR if the connection was cancelled */
static gint
socket_connect(struct addrinfo *h)
{
	struct timeval tv;
	socklen_t len;
	gint cancel_fd;
	gint errnosav;
	gint ret, fd;

	/* see if we're cancelled yet */
	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}

	if (h->ai_socktype != SOCK_STREAM) {
		errno = EINVAL;
		return -1;
	}

	if ((fd = socket (h->ai_family, SOCK_STREAM, 0)) == -1)
		return -1;

	cancel_fd = camel_operation_cancel_fd (NULL);
	if (cancel_fd == -1) {
		if (connect (fd, h->ai_addr, h->ai_addrlen) == -1) {
			/* Yeah, errno is meaningless on Win32 after a
			 * Winsock call fails, but I doubt the callers
			 * check errno anyway.
			 */
			errnosav = errno;
			SOCKET_CLOSE (fd);
			errno = errnosav;
			return -1;
		}

		return fd;
	} else {
#ifndef G_OS_WIN32
		gint flags;
#endif
		gint fdmax, status;
		fd_set rdset, wrset;

#ifndef G_OS_WIN32
		flags = fcntl (fd, F_GETFL);
		fcntl (fd, F_SETFL, flags | O_NONBLOCK);
#else
		{
			u_long yes = 1;
			ioctlsocket (fd, FIONBIO, &yes);
		}
#endif
		if (connect (fd, h->ai_addr, h->ai_addrlen) == 0) {
#ifndef G_OS_WIN32
			fcntl (fd, F_SETFL, flags);
#else
			{
				u_long no = 0;
				ioctlsocket (fd, FIONBIO, &no);
			}
#endif
			return fd;
		}

		if (!SOCKET_ERROR_IS_EINPROGRESS ()) {
			errnosav = errno;
			SOCKET_CLOSE (fd);
			errno = errnosav;
			return -1;
		}

		do {
			FD_ZERO (&rdset);
			FD_ZERO (&wrset);
			FD_SET (fd, &wrset);
			FD_SET (cancel_fd, &rdset);
			fdmax = MAX (fd, cancel_fd) + 1;
			tv.tv_sec = 60 * 4;
			tv.tv_usec = 0;

			status = select (fdmax, &rdset, &wrset, NULL, &tv);
		} while (status == -1 && SOCKET_ERROR_IS_EINTR ());

		if (status <= 0) {
			SOCKET_CLOSE (fd);
			errno = ETIMEDOUT;
			return -1;
		}

		if (cancel_fd != -1 && FD_ISSET (cancel_fd, &rdset)) {
			SOCKET_CLOSE (fd);
			errno = EINTR;
			return -1;
		} else {
			len = sizeof (gint);

			if (getsockopt (fd, SOL_SOCKET, SO_ERROR, (gchar *) &ret, &len) == -1) {
				errnosav = errno;
				SOCKET_CLOSE (fd);
				errno = errnosav;
				return -1;
			}

			if (ret != 0) {
				SOCKET_CLOSE (fd);
				errno = ret;
				return -1;
			}
		}
#ifndef G_OS_WIN32
		fcntl (fd, F_SETFL, flags);
#else
		{
			u_long no = 0;
			ioctlsocket (fd, FIONBIO, &no);
		}
#endif
	}

	return fd;
}

/* Returns the FD of a socket, already connected to and validated by the SOCKS4
 * proxy that is configured in the stream.  Otherwise returns -1.  Assumes that
 * a proxy *is* configured with camel_tcp_stream_set_socks_proxy().
 */
static gint
connect_to_socks4_proxy (const gchar *proxy_host, gint proxy_port, struct addrinfo *connect_addr)
{
	struct addrinfo *ai, hints;
	gchar serv[16];
	gint fd;
	gchar request[9];
	struct sockaddr_in *sin;
	gchar reply[8];
	gint save_errno;

	g_assert (proxy_host != NULL);

	sprintf (serv, "%d", proxy_port);

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;

	ai = camel_getaddrinfo (proxy_host, serv, &hints, NULL); /* NULL-CamelException */
	if (!ai) {
		errno = EHOSTUNREACH; /* FIXME: this is not an accurate error; we should translate the CamelException to an errno */
		return -1;
	}

	fd = socket_connect (ai);
	save_errno = errno;

	camel_freeaddrinfo (ai);

	if (fd == -1) {
		errno = save_errno;
		goto error;
	}

	g_assert (connect_addr->ai_addr->sa_family == AF_INET); /* FMQ: check for AF_INET in the caller */
	sin = (struct sockaddr_in *) connect_addr->ai_addr;

	request[0] = 0x04;				/* SOCKS4 */
	request[1] = 0x01;				/* CONNECT */
	memcpy (request + 2, &sin->sin_port, 2);	/* port in network byte order */
	memcpy (request + 4, &sin->sin_addr.s_addr, 4);	/* address in network byte order */
	request[8] = 0x00;				/* terminator */

	if (camel_write_socket (fd, request, sizeof (request)) != sizeof (request))
		goto error;

	if (camel_read_socket (fd, reply, sizeof (reply)) != sizeof (reply))
		goto error;

	if (!(reply[0] == 0		/* first byte of reply is 0 */
	      && reply[1] == 90)) {	/* 90 means "request granted" */
		errno = ECONNREFUSED;
		goto error;
	}

	goto out;

error:
	if (fd != -1) {
		save_errno = errno;
		SOCKET_CLOSE (fd);
		errno = save_errno;
		fd = -1;
	}

out:

	return fd;
}

static gint
stream_connect (CamelTcpStream *stream, const char *host, const char *service, gint fallback_port, CamelException *ex)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	struct addrinfo *addr, *ai;
	struct addrinfo hints;
	CamelException my_ex;
	gint retval;
	const gchar *proxy_host;
	gint proxy_port;

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;

	camel_exception_init (&my_ex);
	addr = camel_getaddrinfo (host, service, &hints, &my_ex);
	if (addr == NULL && fallback_port != 0 && camel_exception_get_id (&my_ex) != CAMEL_EXCEPTION_USER_CANCEL) {
		char str_port[16];

		camel_exception_clear (&my_ex);
		sprintf (str_port, "%d", fallback_port);
		addr = camel_getaddrinfo (host, str_port, &hints, &my_ex);
	}

	if (addr == NULL) {
		camel_exception_xfer (ex, &my_ex);
		return -1;
	}

	camel_tcp_stream_peek_socks_proxy (stream, &proxy_host, &proxy_port);

	ai = addr;

	while (ai) {
		if (proxy_host)
			priv->sockfd = connect_to_socks4_proxy (proxy_host, proxy_port, ai);
		else
			priv->sockfd = socket_connect (ai);

		if (priv->sockfd != -1) {
			retval = 0;
			goto out;
		}

		ai = ai->ai_next;
	}

	retval = -1;

out:

	camel_freeaddrinfo (addr);

	return retval;
}

static gint
get_sockopt_level (const CamelSockOptData *data)
{
	switch (data->option) {
	case CAMEL_SOCKOPT_MAXSEGMENT:
	case CAMEL_SOCKOPT_NODELAY:
		return IPPROTO_TCP;
	default:
		return SOL_SOCKET;
	}
}

static gint
get_sockopt_optname (const CamelSockOptData *data)
{
	switch (data->option) {
#ifdef TCP_MAXSEG
	case CAMEL_SOCKOPT_MAXSEGMENT:
		return TCP_MAXSEG;
#endif
	case CAMEL_SOCKOPT_NODELAY:
		return TCP_NODELAY;
	case CAMEL_SOCKOPT_BROADCAST:
		return SO_BROADCAST;
	case CAMEL_SOCKOPT_KEEPALIVE:
		return SO_KEEPALIVE;
	case CAMEL_SOCKOPT_LINGER:
		return SO_LINGER;
	case CAMEL_SOCKOPT_RECVBUFFERSIZE:
		return SO_RCVBUF;
	case CAMEL_SOCKOPT_SENDBUFFERSIZE:
		return SO_SNDBUF;
	case CAMEL_SOCKOPT_REUSEADDR:
		return SO_REUSEADDR;
	case CAMEL_SOCKOPT_IPTYPEOFSERVICE:
		return SO_TYPE;
	default:
		return -1;
	}
}

static gint
stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	gint optname, optlen;

	if ((optname = get_sockopt_optname (data)) == -1)
		return -1;

	if (data->option == CAMEL_SOCKOPT_NONBLOCKING) {
#ifndef G_OS_WIN32
		gint flags;

		flags = fcntl (priv->sockfd, F_GETFL);
		if (flags == -1)
			return -1;

		data->value.non_blocking = flags & O_NONBLOCK ? TRUE : FALSE;
#else
		data->value.non_blocking = priv->is_nonblocking;
#endif
		return 0;
	}

	return getsockopt (priv->sockfd,
			   get_sockopt_level (data),
			   optname,
			   (gpointer) &data->value,
			   (socklen_t *) &optlen);
}

static gint
stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	gint optname;

	if ((optname = get_sockopt_optname (data)) == -1)
		return -1;

	if (data->option == CAMEL_SOCKOPT_NONBLOCKING) {
#ifndef G_OS_WIN32
		gint flags, set;

		flags = fcntl (priv->sockfd, F_GETFL);
		if (flags == -1)
			return -1;

		set = data->value.non_blocking ? O_NONBLOCK : 0;
		flags = (flags & ~O_NONBLOCK) | set;

		if (fcntl (priv->sockfd, F_SETFL, flags) == -1)
			return -1;
#else
		u_long fionbio = data->value.non_blocking ? 1 : 0;
		if (ioctlsocket (priv->sockfd, FIONBIO, &fionbio) == SOCKET_ERROR)
			return -1;
		priv->is_nonblocking = data->value.non_blocking ? 1 : 0;
#endif
		return 0;
	}

	return setsockopt (priv->sockfd,
			   get_sockopt_level (data),
			   optname,
			   (gpointer) &data->value,
			   sizeof (data->value));
}

static struct sockaddr *
stream_get_local_address (CamelTcpStream *stream, socklen_t *len)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
#ifdef ENABLE_IPv6
	struct sockaddr_in6 sin;
#else
	struct sockaddr_in sin;
#endif
	struct sockaddr *saddr = (struct sockaddr *)&sin;

	*len = sizeof(sin);
	if (getsockname (priv->sockfd, saddr, len) == -1)
		return NULL;

	saddr = g_malloc(*len);
	memcpy(saddr, &sin, *len);

	return saddr;
}

static struct sockaddr *
stream_get_remote_address (CamelTcpStream *stream, socklen_t *len)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
#ifdef ENABLE_IPv6
	struct sockaddr_in6 sin;
#else
	struct sockaddr_in sin;
#endif
	struct sockaddr *saddr = (struct sockaddr *)&sin;

	*len = sizeof(sin);
	if (getpeername (priv->sockfd, saddr, len) == -1)
		return NULL;

	saddr = g_malloc(*len);
	memcpy(saddr, &sin, *len);

	return saddr;
}

gint
camel_tcp_stream_raw_get_fd (CamelTcpStreamRaw *raw)
{
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return priv->sockfd;
}
